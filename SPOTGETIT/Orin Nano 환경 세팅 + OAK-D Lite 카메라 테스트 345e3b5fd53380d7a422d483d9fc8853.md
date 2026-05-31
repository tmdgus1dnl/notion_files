# Orin Nano 환경 세팅 + OAK-D Lite 카메라 테스트

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
| pip | 22.0.2 |
| 카메라 | OAK-D Lite (DepthAI v3.5.0) |

---

## 1. 환경 세팅

### 1-1. pip 주의사항

JetPack 6.2의 pip(22.0.2)는 `--break-system-packages` 옵션을 지원하지 않습니다.
해당 옵션 없이 설치합니다.

```bash
# 올바른 설치 방법
pip install <패키지명>
```

### 1-2. 패키지 설치

```bash
# DepthAI
pip install depthai

# OpenCV
sudo apt install python3-opencv -y

# NumPy
pip install numpy
```

### 1-3. USB 권한 설정

OAK-D Lite 연결을 위한 udev 룰을 등록합니다. **최초 1회만 실행**합니다.

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="03e7", MODE="0666"' | sudo tee /etc/udev/rules.d/80-movidius.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

설정 후 OAK-D Lite를 USB에 재연결합니다.

### 1-4. PATH 설정

TensorRT의 `trtexec`를 CLI에서 사용하기 위해 PATH에 등록합니다.

```bash
echo 'export PATH=$PATH:/usr/src/tensorrt/bin' >> ~/.bashrc
source ~/.bashrc
```

---

## 2. OAK-D Lite 구성

OAK-D Lite는 3개의 카메라로 구성됩니다.

| 소켓 | 종류 | 센서 | 역할 |
| --- | --- | --- | --- |
| CAM_A | RGB | IMX378 | 컬러 영상 출력 |
| CAM_B | Mono (좌) | OV9282 | Stereo Depth 입력 |
| CAM_C | Mono (우) | OV9282 | Stereo Depth 입력 |

### CAM_A (RGB) 지원 해상도

IMX378 센서는 1080P 미만 해상도를 지원하지 않습니다.
출력 크기는 `requestOutput()`으로 별도 지정합니다.

| 옵션 | 해상도 |
| --- | --- |
| THE_1080_P | 1920x1080 (사용 가능한 최소) |
| THE_4_K | 3840x2160 |
| THE_12_MP | 4056x3040 |
| THE_13_MP | 4208x3120 (미지정 시 기본값 → 크래시 발생) |

---

## 3. 파이프라인 구조

```
CAM_A (RGB)  → requestOutput(480x270, BGR888p) → rgb_queue  → Host
CAM_B (Mono) → requestOutput(640x400, GRAY8)   ─┐
                                                  ├─ StereoDepth → depth_queue → Host
CAM_C (Mono) → requestOutput(640x400, GRAY8)   ─┘
```

### StereoDepth 주요 설정

| 설정 | 값 | 비고 |
| --- | --- | --- |
| PresetMode | FAST_DENSITY | v3에서 HIGH_DENSITY 제거됨 |
| DepthAlign | CAM_A | RGB-Depth 정렬 |
| OutputSize | 640x400 | 모노 카메라 출력 크기와 반드시 일치, 16의 배수 |
| LeftRightCheck | True | 정확도 향상 |
| Subpixel | False | 연산 부하 감소 |

---

## 4. DepthAI v3 API 핵심 변경사항

v2 코드를 참고할 경우 아래 항목을 반드시 v3 방식으로 수정해야 합니다.

| 항목 | v2 | v3 |
| --- | --- | --- |
| 소켓 지정 | `cam.setBoardSocket(소켓)` | `pipeline.create(dai.node.Camera).build(소켓)` |
| 출력 요청 | `cam.preview` / `cam.video` | `cam.requestOutput((w, h), type)` |
| 모노→스테레오 연결 | `cam.out.link(stereo.left)` | `cam.requestOutput(...).link(stereo.left)` |
| XLink 노드 | `pipeline.create(dai.node.XLinkOut)` | **제거됨** |
| 출력 큐 생성 | `device.getOutputQueue("name")` | `output.createOutputQueue()` |
| 파이프라인 시작 | `with dai.Device(pipeline) as device:` | `pipeline.start()` |
| 파이프라인 종료 | context manager 자동 종료 | `pipeline.stop()` |

---

## 5. 카메라 테스트

### 5-1. 카메라 연결 확인

```bash
lsusb | grep 03e7
```

출력 예시:

```
Bus 001 Device 002: ID 03e7:2485 Intel Movidius MyriadX
```

### 5-2. 테스트 실행

```bash
python3 oak_camera_test.py
```

### 5-3. 출력 결과

- **RGB 창** : 컬러 영상 + 중앙 픽셀 거리값 (단위 : m)
- **Depth 창** : Depth Map 컬러맵 시각화
    - 파란색 : 먼 거리
    - 빨간색 : 가까운 거리

![image.png](image%2020.png)

### 5-4. 종료

실행 중인 창에서 `q` 입력

---

## 6. 트러블슈팅

| 증상 | 원인 | 해결 방법 | 상태 |
| --- | --- | --- | --- |
| `ColorCamera node is deprecated` | v3에서 ColorCamera/MonoCamera deprecated | 경고이므로 동작에 무관. Camera 노드 전환 권장 | ✅ 해결 |
| `expected 1920x1080, received 2104x1560` | RGB 해상도 미지정으로 13MP 기본값 적용 | `requestOutput(size, type)` 으로 출력 크기 명시 | ✅ 해결 |
| `XLinkOut has no attribute` | v3에서 XLinkOut 제거됨 | `output.createOutputQueue()` 사용 | ✅ 해결 |
| `PresetType has no attribute HIGH_DENSITY` | v3에서 PresetType → PresetMode로 변경 | `PresetMode.FAST_DENSITY` 사용 | ✅ 해결 |
| `stride should be equal to width` | 모노 카메라 출력 크기와 StereoDepth 크기 불일치 | `requestOutput` 크기와 `setOutputSize` 크기 일치 필요 | ✅ 해결 |
| `Disparity width must be multiple of 16` | StereoDepth 출력 크기가 16의 배수 아님 | `setOutputSize`를 16의 배수로 설정 | ✅ 해결 |
| `--break-system-packages` 옵션 에러 | pip 22.0.2 미지원 | 옵션 제거 후 `pip install <패키지명>` 으로 설치 | ✅ 해결 |