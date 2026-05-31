# Orin Nano 환경 세팅 + OAK-D Lite 객체 인식 테스트

상태: VISION

> 작성일 : 2026-04-17
프로젝트 : SPOT Get IT
> 

---

## 환경 정보

| 항목 | 내용 |
| --- | --- |
| 보드 | NVIDIA Jetson Orin Nano |
| JetPack | 6.2 (R36.4.3) |
| OS | Ubuntu 22.04 LTS (aarch64) |
| Python | 3.10 (시스템 Python, 가상환경 없음) |
| CUDA | 12.6 (`/usr/local/cuda-12.6`) |
| TensorRT | 10.x (JetPack 내장) |
| 카메라 | OAK-D Lite (DepthAI v3.5.0) |
| 모델 | YOLOv11n (imgsz=480, 단일 클래스 - person) |

---

## 1. 환경 세팅

### 1-1. PATH 설정

CUDA 및 TensorRT CLI 도구를 사용하기 위해 PATH에 등록합니다. **최초 1회만 실행**합니다.

```bash
echo 'export PATH=$PATH:/usr/src/tensorrt/bin' >> ~/.bashrc
echo 'export PATH=$PATH:/usr/local/cuda-12.6/bin' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/cuda-12.6/lib64' >> ~/.bashrc
source ~/.bashrc
```

등록 확인

```bash
nvcc --version
trtexec --help
```

### 1-2. 패키지 설치

> pip 22.0.2는 `--break-system-packages` 옵션을 지원하지 않습니다. 옵션 없이 설치합니다.
> 

```bash
pip install pycuda
pip install "numpy<2"
```

> **numpy 버전 주의** : pycuda는 NumPy 1.x 기준으로 빌드됩니다. NumPy 2.x가 설치되어 있으면 `_ARRAY_API not found` 에러가 발생하므로 반드시 1.x로 다운그레이드합니다.
> 

```bash
# opencv-python이 numpy>=2를 요구하는 경우 아래와 같이 처리
pip uninstall opencv-python -y
sudo apt install python3-opencv -y
```

---

## 2. 아키텍처

### 2-1. 역할 분리 구조

객체 인식은 Orin Nano GPU에서, 거리 추정은 OAK-D Lite Depth Map을 활용합니다.

| 역할 | 담당 |
| --- | --- |
| 객체 인식 | Orin Nano (YOLO + TensorRT INT8) |
| 거리 추정 | OAK-D Lite Depth Map 참조 |

OAK-D Lite의 Myriad X VPU 온보드 추론은 사용하지 않습니다.

### 2-2. 처리 파이프라인

```
OAK-D Lite
├── RGB 스트림 출력 (480x270, BGR888p)
└── Depth Map 지속 생성 (640x400, RGB-Depth Align 활성화)
         ↓ USB
Jetson Orin Nano
├── RGB → 480x480 리사이즈 → YOLO 추론 (TensorRT INT8) → bbox 생성
└── Depth Map 참조 → bbox 하단 중심 ROI → Median Depth → 거리 결정 (m)
         ↓
관제 시스템 송신 (클래스, confidence, 거리)
```

### 2-3. 거리 추정 전략

| 항목 | 내용 |
| --- | --- |
| ROI 영역 | bbox 하단 중심 (margin 15%) |
| 거리 계산 | ROI 내 유효 픽셀의 Median Depth |
| 안정화 | N-frame Smoothing (N=5, 이동 평균) |
| 단위 | mm → m 변환 |

---

## 3. TensorRT Engine 변환

### 3-1. ONNX 변환 (시뮬 PC에서 수행)

학습 완료된 `.pt` 모델을 ONNX로 변환합니다. (imgsz=480, opset=17, dynamic=False)

Orin Nano로 `best.onnx` 파일을 전송합니다.

### 3-2. TensorRT Engine 변환 (Orin Nano에서 수행)

```bash
trtexec --onnx=best.onnx --saveEngine=best.engine --int8 --memPoolSize=workspace:512M
```

> **주의** : TensorRT 10에서 `--workspace` 옵션이 제거됐습니다. `--memPoolSize=workspace:512M` 으로 대체합니다.
> 

### 3-3. 엔진 입출력 텐서 확인

```python
import tensorrt as trt

TRT_LOGGER = trt.Logger(trt.Logger.WARNING)
with open('best.engine', 'rb') as f, trt.Runtime(TRT_LOGGER) as runtime:
    engine = runtime.deserialize_cuda_engine(f.read())
    for i in range(engine.num_io_tensors):
        name = engine.get_tensor_name(i)
        mode = engine.get_tensor_mode(name)
        shape = engine.get_tensor_shape(name)
        print(f'{mode}: {name} {shape}')
```

확인된 텐서 정보 (YOLOv11n, imgsz=480 기준)

| 구분 | 이름 | Shape |
| --- | --- | --- |
| INPUT | images | (1, 3, 480, 480) |
| OUTPUT | output0 | (1, 5, 4725) |

> output shape의 마지막 차원(4725)은 imgsz=480 기준 anchor 수입니다. 640이면 8400, 480이면 4725입니다.
> 

---

## 4. 추론 코드 주요 설정

### 4-1. TensorRT 추론 (execute_async_v3)

TensorRT 10에서 `execute_async_v2`가 제거됐습니다. `execute_async_v3`와 텐서 이름 기반 방식으로 변경합니다.

```python
# v2 방식 (TensorRT 10에서 동작 안 함)
context.execute_async_v2(
    bindings=[int(d_input), int(d_output)],
    stream_handle=stream.handle
)

# v3 방식 (올바른 방법)
context.set_tensor_address("images", int(d_input))
context.set_tensor_address("output0", int(d_output))
context.execute_async_v3(stream_handle=stream.handle)
```

### 4-2. 후처리 output shape

```python
output_shape = (1, 5, 4725)  # imgsz=480 기준
pred = output[0].T            # (4725, 5) → cx, cy, w, h, conf
```

---

## 5. 객체 인식 테스트 실행

### 5-1. 실행

```bash
python3 oak_inference_test.py --engine best.engine
```

### 5-2. 출력 결과

- 영상 창에 bbox, confidence, 거리(m) 표시
- 탐지 수 좌상단 표시

### 5-3. 종료

실행 중인 창에서 `q` 입력

---

## 6. 현재 테스트 결과 및 한계

### 6-1. 완료된 것

- TensorRT INT8 engine 변환 완료
- pycuda 기반 추론 파이프라인 동작 확인
- OAK-D Lite RGB + Depth 연동 및 거리 추정 파이프라인 동작 확인
- 영상 출력 정상 동작

### 6-2. 미완료 - 모델 과적합 문제

학습/검증 데이터에서는 탐지가 되나 실제 환경 이미지에서 탐지 실패합니다.

**원인** : 데이터셋 도메인 편향 및 다양성 부족

**확인 방법**

```python
# 추론 루프 내 디버그 출력
pred = output[0].T
print(f"conf max: {pred[:, 4].max():.4f}, conf mean: {pred[:, 4].mean():.4f}")
```

실제 환경 이미지에서 conf max가 0에 수렴하면 모델 과적합 또는 도메인 갭 문제입니다.

**해결 계획**

- 데이터셋 다양성 보강 (다양한 거리·앵글·조명·자세)
- Ultralytics 학습 증강 설정 강화 (degrees, fliplr, hsv 계열 활성화)
- OAK-D Lite로 직접 촬영한 데이터로 파인튜닝

---

## 7. 트러블슈팅

| 증상 | 원인 | 해결 방법 | 상태 |
| --- | --- | --- | --- |
| `nvcc not in path` | CUDA PATH 미등록 | `~/.bashrc`에 `/usr/local/cuda-12.6/bin` 추가 | ✅ 해결 |
| `trtexec: command not found` | TensorRT PATH 미등록 | `~/.bashrc`에 `/usr/src/tensorrt/bin` 추가 | ✅ 해결 |
| `Unknown option: --workspace` | TensorRT 10에서 옵션 제거됨 | `--memPoolSize=workspace:512M` 으로 대체 | ✅ 해결 |
| `_ARRAY_API not found` | NumPy 2.x와 pycuda 호환 안됨 | `pip install "numpy<2"` 로 다운그레이드 | ✅ 해결 |
| opencv-python numpy 충돌 | opencv-python이 numpy>=2 요구 | pip 버전 제거 후 `sudo apt install python3-opencv` 사용 | ✅ 해결 |
| `execute_async_v2` AttributeError | TensorRT 10에서 v2 제거됨 | `execute_async_v3` + `set_tensor_address()` 방식으로 변경 | ✅ 해결 |
| `pycuda` 빌드 실패 | nvcc PATH 미등록 상태로 설치 시도 | CUDA PATH 등록 후 재설치 | ✅ 해결 |
| conf max가 0에 수렴 | 모델 과적합 또는 도메인 갭 | 데이터셋 보강 후 재학습 예정 | ❌ 미해결 |