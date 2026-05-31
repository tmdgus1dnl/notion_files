# 객체 인식, Jetson orin nano에서 할 것

상태: VISION

# 1안 : OAK-D-Lite 단일 처리 구조

- 객체 인식 : oak-d-lite 위에서
- 거리 측정 : oak-d-lite 위에서

# 2안: 역할 분리 구조

- 객체 인식 : jetson orin nano 위에서
- 거리 측정 : oak-d-lite 위에서

 

# AI 의견 ⇒ 2안 승

### 지피티 : 2안 oak-d-lite는 거리 측정 센서로 써라

# OAK-D-Lite + Jetson Orin Nano 아키텍처 비교 보고서

**주제:** 객체 인식 + 거리 추정 파이프라인 설계

---

## 1. 아키텍처 정의

### ■ 1안: OAK-D-Lite 단일 처리 구조

- 객체 인식: OAK-D-Lite 내부 (on-device NN)
- 거리 계산: OAK-D-Lite (stereo depth + spatial)
- Orin Nano: 결과 수신 및 제어 로직 수행

---

### ■ 2안: 역할 분리 구조

- 객체 인식: Jetson Orin Nano
- 거리 계산: OAK-D-Lite (depth)
- Orin Nano: bbox + depth 융합 및 판단

---

# 2. 1안 (OAK-D-Lite에서 객체 인식 + 거리 모두 수행)

## ■ 장점

### 1) 낮은 시스템 부하 (Host Offloading)

- 영상 처리 + 추론 + depth 계산이 카메라 내부에서 수행됨
- Orin Nano의 GPU/CPU 부하 감소
- USB 대역폭 감소 (raw frame 전송 불필요)

→ **이유:**

OAK-D-Lite는 Myriad X 기반 VPU에서 inference + depth를 동시에 수행하도록 설계됨

---

### 2) 낮은 전력 소비

- 전체 시스템 소비 전력 감소
- 모바일/로봇 환경에 유리

→ **이유:**

Jetson GPU 대신 VPU 사용 시 전력 효율 우수

---

### 3) 파이프라인 단순성

- 단일 디바이스에서 결과 생성
- 동기화 문제 최소화

→ **이유:**

RGB + depth + detection이 내부에서 이미 time-aligned 상태

---

### 4) 낮은 지연 (Low Latency)

- 데이터 전송 없이 내부 처리

---

## ■ 단점

### 1) 모델 선택 및 확장성 제한

- 사용 가능한 모델 크기 제한
- TensorRT, PyTorch 등 고급 최적화 불가

→ **이유:**

Myriad X는 경량 NN 전용 (복잡한 모델 처리 어려움)

---

### 2) 디버깅 및 커스터마이징 어려움

- 내부 파이프라인 제어 제한
- 중간 feature 접근 어려움

---

### 3) 성능 한계 (Accuracy / FPS)

- 대형 모델 적용 불가
- 복잡한 환경에서 detection 품질 저하 가능

---

### 4) 향후 기능 확장 어려움

- tracking, multi-task, sensor fusion 확장 제한

---

## ■ 구현 방법

### 1) DepthAI 파이프라인 구성

- `ColorCamera`
- `StereoDepth`
- `SpatialDetectionNetwork`

### 2) 처리 흐름

```
RGB → Detection (NN) → bbox 생성
         ↓
Depth → bbox ROI 적용 → XYZ 계산
```

### 3) 출력

- 클래스
- confidence
- (X, Y, Z) 거리

---

# 3. 2안 (Orin Nano에서 객체 인식 + OAK에서 거리 계산)

## ■ 장점

### 1) 높은 객체 인식 성능

- 대형 모델 사용 가능 (YOLOv5/YOLOv8, custom model 등)
- TensorRT 최적화 가능

→ **이유:**

Jetson Orin Nano는 CUDA 기반 GPU 가속 환경 제공

---

### 2) 높은 확장성

- Tracking, SLAM, sensor fusion, multi-model 적용 가능
- AI 파이프라인 유연성 확보

---

### 3) 모델 변경 및 실험 용이

- PyTorch, ONNX 등 자유로운 개발 환경
- 빠른 iteration 가능

---

### 4) 정밀한 거리 추정 가능

- bbox 기반 ROI를 직접 제어
- median depth, 하단 중심 ROI 등 고급 전략 적용 가능

---

## ■ 단점

### 1) 시스템 부하 증가

- Orin Nano에서 inference 수행
- GPU/CPU 사용량 증가

---

### 2) 동기화 문제 발생 가능

- RGB (Orin) vs Depth (OAK) 타이밍 불일치

→ **이유:**

두 장치가 별도로 프레임 생성

---

### 3) 구현 복잡도 증가

- 데이터 흐름 설계 필요
- 좌표계 및 alignment 처리 필요

---

### 4) 전력 소비 증가

- GPU 사용 증가로 발열 상승

---

## ■ 구현 방법 (권장 구조)

### 1) OAK-D-Lite 구성

- RGB stream 출력
- Depth map 지속 생성
- RGB-Depth alignment

---

### 2) Orin Nano 구성

- RGB 입력
- 객체 검출 수행 (YOLO 등)
- bbox 생성

---

### 3) 거리 계산 방식 (핵심)

```
depth map 유지 (continuous)

검출 발생 →
    bbox → ROI 변환 →
        ROI 내 depth 추출 →
            median depth 계산 → 거리 결정
```

---

### 4) ROI 선택 전략

- bbox 전체 ❌
- 중앙 영역 ⭕
- 하단 중심 영역 ⭕ (지면 기준 객체에 특히 유효)

---

### 5) 추가 안정화

- N-frame smoothing (moving average)
- depth outlier 제거

---

# 4. 핵심 설계 원칙

## ■ 잘못된 접근

```
객체 검출 → 그 순간 depth 측정 시작
```

→ 프레임 불일치, 노이즈 증가

---

## ■ 권장 접근

```
depth는 항상 유지
객체 검출 시 해당 위치의 depth를 참조
```

---

# 5. 최종 비교 표

| 항목 | 1안 (OAK 단독) | 2안 (Orin + OAK 분리) |
| --- | --- | --- |
| 객체 인식 성능 | 낮음 (경량 모델 제한) | 높음 (대형 모델 가능) |
| 거리 정확도 | 보통 | 높음 (ROI 제어 가능) |
| 지연 | 낮음 | 중간 |
| 시스템 부하 | 낮음 | 높음 |
| 전력 소비 | 낮음 | 높음 |
| 구현 난이도 | 낮음 | 높음 |
| 동기화 문제 | 없음 | 있음 (보완 필요) |
| 확장성 | 낮음 | 매우 높음 |
| 디버깅 | 어려움 | 용이 |
| 유지보수 | 제한적 | 유연 |
| 로봇 적용 적합성 | 제한적 | 매우 적합 |

---

# 6. 최종 결론

- 단순한 시스템 또는 저전력 제품화 목적 → **1안 적합**
- **로봇, 특히 4족 보행 로봇과 같은 동적 환경 → 2안이 명확히 우세**

---

## ■ 최종 권장

> **객체 인식은 Orin Nano에서 수행하고,거리 계산은 OAK-D-Lite의 depth를 활용하는 구조가 가장 합리적이다.**
> 

이 구조는

- 성능
- 확장성
- 안정성

세 가지를 모두 만족하는 균형점입니다.

### 제미나이 :
1안(타겟 디바이스 몰라서 그랬다.) → 2안(타겟 디바이스 들으니 이게 합당하다.)

제시해주신 보고서의 분석이 타당합니다. **호스트 기기가 Jetson Orin Nano라는 전제하에서는 제2안(역할 분리 구조)이 압도적으로 유리합니다.**

이전 답변은 호스트 기기의 사양을 알 수 없는 상태에서 일반적인 저전력 기기(Raspberry Pi 등)나 단순 PC 환경을 가정한 원론적인 권장이었습니다. 호스트가 강력한 GPU를 탑재한 Orin Nano라면 시스템 설계 기준이 완전히 달라져야 합니다.

이에 대한 제 의견을 보고서 형식으로 정리합니다.

---

### OAK-D Lite + Jetson Orin Nano 아키텍처 재평가 보고서

**1. 최종 결론**

- **제2안(역할 분리 구조) 채택을 강력히 권장합니다.**
- OAK-D Lite의 VPU는 최신 비전 모델을 감당하기에 한계가 명확하며, Orin Nano의 강력한 GPU 성능을 유휴 상태로 방치할 이유가 없습니다.

**2. 제1안(OAK-D 단독)에서 제2안(역할 분리)으로 의견을 수정하는 핵심 이유**

- **연산 능력의 압도적 차이:** OAK-D Lite(Myriad X)의 AI 가속 성능은 1.4 TOPS입니다. 반면 Jetson Orin Nano는 최대 40 TOPS의 연산 능력을 제공합니다.
- **최신 모델 호환성:** YOLO 최신 버전(v8, v11 등)을 Myriad X에서 구동하려면 극단적인 모델 경량화 및 포맷 변환이 강제되며, 이 과정에서 정확도 손실이 크게 발생합니다. 반면 Orin Nano는 TensorRT 엔진을 통해 원본에 가까운 정확도로 고속 추론(Inference)이 가능합니다.
- **로봇 프레임워크 연동성:** 로봇 제어를 위해 ROS2를 사용할 경우, 호스트(Orin Nano) 메모리 상에서 객체 인식 결과를 직접 생성하고 관리하는 것이 라이다(LiDAR) 등 타 센서와의 퓨전(Sensor Fusion) 및 제어 로직 설계에 훨씬 유리합니다.

**3. GPT 보고서에 대한 기술적 보완 (2안 구현 시 한계 극복 방안)**

제시된 보고서의 2안 단점(동기화, 부하)은 소프트웨어 아키텍처 설계로 충분히 상쇄할 수 있습니다.

- **동기화 (Synchronization) 문제 해결:**
    - RGB 프레임과 Depth 맵이 각각 따로 전송되면서 발생하는 타임스탬프 불일치 문제는 ROS2의 `message_filters`를 적용하여 해결합니다.
    - `ApproximateTimeSynchronizer` 정책을 사용하여, 수신된 RGB 이미지와 Depth 이미지 중 타임스탬프가 가장 근접한 쌍을 묶어 객체 인식 및 거리 산출 노드(Node)로 전달하도록 파이프라인을 구축합니다.
- **USB 대역폭 및 호스트 부하 감소 방안:**
    - OAK-D에서 Orin Nano로 Raw Video(Uncompressed)를 전송하면 USB 병목 현상이 발생합니다.
    - OAK-D 내부의 비디오 인코더를 활성화하여 H.264 또는 H.265 형태로 압축 전송하고, Orin Nano에 내장된 하드웨어 디코더를 사용하여 압축을 해제하면 통신 지연과 CPU 부하를 동시에 낮출 수 있습니다.

**4. 요약**

Jetson Orin Nano가 확보된 로봇 개발 환경에서 OAK-D Lite는 '지능형 카메라'가 아닌 '고성능 심도(Depth) 센서'로 역할을 축소하는 것이 전체 시스템 효율을 극대화합니다. 객체 인식은 Orin Nano의 GPU와 TensorRT에 전담시키고, 거리 추정에만 OAK-D의 Depth Map을 활용하는 설계가 실무적으로 가장 타당한 접근입니다.

### 클로드 : 2안 지피티의 의견과 제미나이의 보완이면 충분하다.

구현 방법에 기재.

# OAK-D Lite + Jetson Orin Nano 아키텍처 설계 보고서

> 작성일 : 2026-04-16
> 

---

## 1. 결론 요약

**객체 인식은 Jetson Orin Nano (TensorRT), 거리 추정은 OAK-D Lite (Depth Map)** 으로 역할을 분리하는 **2안(역할 분리 구조)** 을 채택한다.

---

## 2. 아키텍처 비교

| 항목 | 1안 : OAK 단독 | 2안 : 역할 분리 (채택) |
| --- | --- | --- |
| 객체 인식 위치 | OAK-D Lite (Myriad X VPU) | Jetson Orin Nano (GPU) |
| 거리 계산 위치 | OAK-D Lite (Stereo Depth) | OAK-D Lite (Depth Map) |
| 연산 성능 | ~1.4 TOPS | 40 TOPS |
| 모델 자유도 | .blob 변환 필수, 경량 모델 제한 | TensorRT, ONNX 등 자유로움 |
| 객체 인식 품질 | 낮음 | 높음 |
| 거리 정확도 | 보통 | 높음 (ROI 제어 가능) |
| 동기화 문제 | 없음 | 있음 → 해결 가능 |
| 확장성 | 낮음 | 매우 높음 |
| ROS2 연동 | 제한적 | 자유로움 |

---

## 3. 2안 채택 근거

### 3-1. 연산 성능 차이

OAK-D Lite에 탑재된 Myriad X VPU의 AI 가속 성능은 약 1.4 TOPS다. Jetson Orin Nano는 최대 40 TOPS로 약 28배 차이가 난다. 여기에 TensorRT INT8 양자화 최적화까지 더하면 실질적인 추론 속도 차이는 더 벌어진다. Orin Nano의 GPU를 유휴 상태로 두고 VPU에서 추론하는 것은 자원 낭비다.

### 3-2. 모델 품질

YOLO 최신 버전(v8, v11 등)을 Myriad X에서 구동하려면 .blob 형식으로의 변환과 극단적인 경량화가 강제되며, 이 과정에서 정확도 손실이 크다. Orin Nano는 TensorRT로 원본에 가까운 정확도를 유지하면서 고속 추론이 가능하다.

### 3-3. 시스템 확장성

추후 LiDAR 등 타 센서와의 Sensor Fusion, Tracking, Multi-task 확장을 고려할 때 Orin Nano 메모리 상에서 객체 인식 결과를 직접 생성하고 관리하는 구조가 훨씬 유리하다.

---

## 4. 처리 파이프라인

```
OAK-D Lite
├── RGB 스트림 출력
└── Depth Map 지속 생성 (RGB-Depth Hardware Align 활성화)
         ↓ USB
Jetson Orin Nano
├── RGB 수신 → YOLO 추론 (TensorRT INT8) → bbox 생성
└── Depth Map 참조 → bbox ROI 내 median depth 계산 → 거리 결정
         ↓
관제 시스템으로 송신 (클래스, confidence, 거리)
```

> **핵심 원칙** : Depth Map은 항상 유지한다. 객체 검출 발생 시 해당 시점의 Depth를 참조하는 구조로 설계한다. 검출 발생 후 depth 측정을 시작하는 방식은 프레임 불일치와 노이즈를 유발하므로 금지한다.
> 

---

## 5. 동기화 문제 및 해결 전략

2안의 주요 단점은 RGB와 Depth 스트림 간 타임스탬프 불일치다. 두 단계로 해결한다.

### 5-1. 1차 : OAK 하드웨어 Align (근본 해결)

OAK-D Lite의 DepthAI SDK는 하드웨어 레벨에서 RGB와 Depth를 정렬하는 기능을 제공한다.

```python
stereo.setDepthAlign(dai.CameraBoardSocket.RGB)
```

이 옵션을 활성화하면 두 스트림이 OAK 내부에서 이미 정렬된 채로 출력된다. 동기화 문제의 상당 부분이 소프트웨어 처리 전에 사전 제거된다.

### 5-2. 2차 : ROS2 message_filters (보험)

하드웨어 align 이후에도 잔여 타임스탬프 오차가 발생할 수 있다. ROS2의 `ApproximateTimeSynchronizer`를 적용하여 타임스탬프가 가장 근접한 RGB-Depth 쌍을 묶어 추론 노드로 전달한다.

```python
from message_filters import ApproximateTimeSynchronizer, Subscriber

ts = ApproximateTimeSynchronizer(
    [rgb_sub, depth_sub], queue_size=10, slop=0.05
)
ts.registerCallback(callback)
```

---

## 6. 거리 추정 전략

### ROI 선택

bbox 전체 영역의 depth를 평균하면 배경 노이즈가 섞인다. 다음 전략을 적용한다.

| 전략 | 설명 | 권장 여부 |
| --- | --- | --- |
| bbox 전체 평균 | 배경 포함 가능 | ❌ |
| bbox 중앙 영역 | 노이즈 감소 | ⭕ |
| bbox 하단 중심 영역 | 지면 기준 객체에 특히 유효 | ⭕ (권장) |

### 안정화

- **Median Depth** : 평균 대신 중앙값으로 outlier 제거
- **N-frame Smoothing** : 연속 N프레임의 이동 평균으로 깜빡임 방지

---

## 7. USB 대역폭 최적화

Raw 비압축 영상을 USB로 전송하면 대역폭 병목이 발생한다. OAK-D Lite 내부 비디오 인코더를 활성화하여 H.264/H.265로 압축 전송하고, Orin Nano의 하드웨어 디코더로 압축을 해제한다. 통신 지연과 CPU 부하를 동시에 낮출 수 있다.

---

## 8. OAK-D Lite 운용 범위 정의

이 프로젝트에서 OAK-D Lite는 다음과 같이 역할을 제한한다.

| 기능 | 사용 여부 |
| --- | --- |
| RGB 스트림 출력 | ✅ |
| Depth Map 출력 | ✅ |
| RGB-Depth Hardware Align | ✅ |
| Myriad X 온보드 추론 | ❌ (Orin Nano에서 수행) |

> OAK-D Lite는 **고성능 Depth 센서**로 역할을 한정한다. 온보드 추론은 사용하지 않는다.
>