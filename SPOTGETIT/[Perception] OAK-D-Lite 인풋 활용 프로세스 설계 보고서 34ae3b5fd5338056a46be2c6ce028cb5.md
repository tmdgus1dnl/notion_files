# [Perception] OAK-D-Lite 인풋 활용 프로세스 설계 보고서

상태: Perception

> 작성일 : 2026-04-23
프로젝트 : SPOT Get IT
담당 파트 : SIM / APP (인명 탐지)
> 

---

## 1. 개요

OAK-D Lite 카메라로부터 RGB 스트림을 수신하여 YOLOv11 + TensorRT 기반 인명 탐지를 수행하고 결과를 ROS2 토픽으로 publish하는 프로세스 설계입니다.

---

## 2. 설계 목표

- 실시간 인명 탐지 (Jetson Orin Nano 타겟)
- 카메라 제어와 추론 로직의 책임 분리
- 관제 영상 송출과 추론을 독립적으로 운용
- 단일 책임 원칙 준수 (ROS2 개발 컨벤션 기반)

---

## 3. 패키지 구조

### 3-1. 워크스페이스 내 위치

```
robot_ws/src/
├── vendor/
│   └── oak_camera_driver/        # OAK-D Lite 카메라 드라이버 패키지
│
├── perception/
│   └── camera_perception/        # 인명 탐지 패키지
│
└── network/
    └── image_stream_sender/      # 관제 영상 송출 패키지
```

> `vendor`, `perception`은 계층 분류용 디렉토리이며 패키지가 아닙니다.
실제 패키지는 `oak_camera_driver`, `camera_perception`, `image_stream_sender`입니다.
> 

### 3-2. oak_camera_driver 내부 구조

```
vendor/oak_camera_driver/
├── package.xml
├── setup.py
├── setup.cfg
├── oak_camera_driver/
│   ├── __init__.py
│   └── oak_camera_driver_node.py   # OAK-D Lite SDK 직접 제어
├── launch/
│   └── oak_camera_driver.launch.py
├── config/
│   └── oak_camera_driver.param.yaml
└── README.md
```

### 3-3. camera_perception 내부 구조

```
perception/camera_perception/
├── package.xml
├── setup.py
├── setup.cfg
├── camera_perception/
│   ├── __init__.py
│   ├── camera_perception_node.py   # subscribe + 추론 + publish
│   └── trt_inference.py            # TRT / ONNX 추론 모듈
├── launch/
│   └── camera_perception.launch.py
├── config/
│   └── camera_perception.param.yaml
└── README.md
```

### 3-4. image_stream_sender 내부 구조

```
network/image_stream_sender/
├── package.xml
├── setup.py
├── setup.cfg
├── image_stream_sender/
│   ├── __init__.py
│   └── image_stream_sender_node.py   # encoded subscribe + 커스텀 프로토콜 UDP 송출
├── launch/
│   └── image_stream_sender.launch.py
├── config/
│   └── image_stream_sender.param.yaml
└── README.md
```

---

## 4. 토픽 흐름

```
[OAK-D Lite 카메라]
        ↓ DepthAI SDK
[oak_camera_driver_node]
        ├── /perception/camera/image_raw  (480x270, RAW, 15fps)
        └── /perception/camera/encoded    (H.264, 활성화)
                    ↓
        ┌───────────┴───────────────┐
        ↓                           ↓
[camera_perception_node]   [image_stream_sender]  ← 통신팀 담당
        ↓ TensorRT FP16 추론        ↓ UDP 송출 → 관제
[/perception/camera/victim_detection]
        ↓
[decision 노드]
```

---

## 5. 토픽 명세

| 토픽 | 타입 | Publisher | Subscriber | 비고 |
| --- | --- | --- | --- | --- |
| `/perception/camera/image_raw` | `sensor_msgs/Image` | oak_camera_driver | camera_perception, image_stream_sender | 480x270, RAW, 15fps |
| `/perception/camera/encoded` | `sensor_msgs/CompressedImage` (format=h264) | oak_camera_driver | image_stream_sender | H.264 임시 결정, 확정 시 변경 예정 |
| `/perception/camera/victim_detection` | `robot_interfaces/VictimDetection` | camera_perception | decision, network | 탐지 결과 |

---

## 6. 커스텀 메시지 명세

### VictimDetection.msg

```
# robot_interfaces/msg/VictimDetection.msg

std_msgs/Header header

float32 bbox_x          # bbox 좌측 상단 x (정규화, 0~1)
float32 bbox_y          # bbox 좌측 상단 y (정규화, 0~1)
float32 bbox_w          # bbox 너비 (정규화, 0~1)
float32 bbox_h          # bbox 높이 (정규화, 0~1)
float32 distance        # 추정 거리 (m) - Stereo 비활성화로 현재 0.0 고정
float32 confidence      # 탐지 신뢰도 (개발 중 사용, 배포 시 제거 예정)
```

> **bbox 좌표 정규화** : 해상도 변경에 독립적으로 동작하기 위해 0~1 정규화 좌표 사용. 기준 해상도는 `oak_camera_driver.param.yaml`에서 관리.
> 
> 
> **distance 처리 방침** : Stereo Depth를 OAK-D Lite 메모리 부족으로 제거했습니다. RGB + H.264 + Stereo 동시 운용이 불가능하여 Stereo를 우선 제거했습니다. 현재는 0.0으로 고정 발행하며 추후 해결 방안 검토 예정입니다.
> 
> **confidence 처리 방침** : 개발 단계에서는 임계값 튜닝 및 디버깅 용도로 포함. 배포 시점에 confidence 임계값을 통과한 결과만 publish하므로 필드 제거 예정.
> 

---

## 7. 파라미터 명세

### oak_camera_driver.param.yaml

```yaml
oak_camera_driver_node:
  ros__parameters:
    fps: 15
    # victim_detection bbox 좌표 정규화 기준 해상도
    rgb_width: 480
    rgb_height: 270
    show_preview: false   # true로 변경하면 미리보기 창 활성화
    preview_width: 1280
    preview_height: 720
```

### camera_perception.param.yaml

```yaml
camera_perception_node:
  ros__parameters:
    engine_path: "/home/jetson/models/best_fp16.engine"
    conf_threshold: 0.25
    iou_threshold: 0.45
    infer_size: 480
    depth_roi_size: 5        # 거리 추정 ROI 크기 (픽셀, 현재 미사용)
    depth_smooth_frames: 5   # 거리 스무딩 프레임 수 (현재 미사용)
    show_preview: false      # true로 변경하면 bbox 미리보기 창 활성화
    preview_width: 640
    preview_height: 480
```

---

## 8. 모델 파일 관리

TensorRT 엔진 파일은 패키지 내부에 포함하지 않습니다. 크기가 크고 버전 관리가 별도로 필요하기 때문입니다.

```
/home/jetson/models/
└── best_fp16.engine
```

경로는 파라미터로 관리하며 코드 수정 없이 모델 교체가 가능합니다.

로컬 PC 개발 시에는 `.onnx` 파일 경로로 변경하면 ONNX Runtime으로 자동 분기합니다.

---

## 9. 의존성

| 패키지 | 용도 |
| --- | --- |
| `depthai` | OAK-D Lite SDK (v3.5.0) |
| `rclpy` | ROS2 Python 클라이언트 |
| `sensor_msgs` | Image, CompressedImage 메시지 타입 |
| `cv_bridge` | OpenCV ↔ ROS2 이미지 변환 |
| `robot_interfaces` | VictimDetection 커스텀 메시지 |
| `tensorrt` | TensorRT FP16 추론 (Orin Nano) |
| `onnxruntime` | ONNX 추론 (로컬 PC 개발용) |
| `cuda` | GPU 추론 가속 |
| `numpy` | 배열 연산 |
| `opencv-python` | 이미지 처리 |

---

## 10. 설계 결정 및 이유

### 10-1. vendor에 카메라 드라이버 분리

**결정** : OAK-D Lite 제어 노드를 `vendor/oak_camera_driver`로 분리하여 토픽으로 publish

**이유** :

- camera_perception과 image_stream_sender 노드가 동일한 카메라 스트림을 독립적으로 subscribe 가능
- 카메라 제어 로직이 추론 로직에 종속되지 않아 교체 및 유지보수 용이
- 단일 책임 원칙 준수

### 10-2. 영상 송출 노드 분리

**결정** : 관제 영상 송출을 `network/image_stream_sender`로 분리. `oak_camera_driver`에서 RAW 스트림과 인코딩 스트림을 동시에 publish

**이유** :

- OAK-D Lite 디바이스는 단일 노드에서만 점유 가능
- oak_camera_driver가 RAW(추론용)와 인코딩(송출용) 두 스트림을 동시 publish
- image_stream_sender는 encoded subscribe → 커스텀 프로토콜 UDP 송출만 담당
- 추론과 영상 송출이 독립적으로 동작하므로 서로 블로킹 없음
- 인코딩 포맷은 H.264로 임시 결정, 확정 시 VideoEncoder Profile 수정만으로 전환 가능

### 10-3. H.264 인코더 임시 결정

**결정** : H.264로 임시 결정하여 개발 시작. RGB + H.264 동시 운용 중

**이유** :

- OAK-D Lite 온보드 메모리 한계로 RGB + H.264 + Stereo 동시 운용 불가
- Stereo를 제거한 상태에서 RGB + H.264 동시 운용 가능 확인
- 통신팀 확정 시 H.265/MJPEG으로 변경 가능 (VideoEncoder Profile 수정만으로 전환)

### 10-4. Stereo Depth 제거

**결정** : Stereo Depth를 제거하고 거리 추정 기능 보류

**이유** :

- 프로젝트에서 Stereo 활용처가 victim 거리 추정 외에 없음
- 장애물 회피, 로컬라이제이션은 LiDAR 담당
- OAK-D Lite 메모리 한계로 RGB + H.264 + Stereo 동시 운용 불가
- 거리 추정보다 영상 송출(H.264) 우선순위가 높다고 판단

### 10-5. depthai-ros 공식 드라이버 미사용

**결정** : depthai-ros 공식 ROS2 드라이버 대신 SDK 직접 사용

**이유** :

- DepthAI v3 API 기준 공식 ROS2 드라이버 지원 여부 불확실
- SDK 기반 카메라 동작이 이미 검증됨 (카메라 테스트, 추론 테스트 완료)
- 추가 의존성 없이 빠르게 개발 가능

### 10-6. queue_size=1 설정

**결정** : subscriber의 queue_size를 1로 설정

**이유** :

- 추론 시간이 프레임 간격보다 길 경우 큐가 쌓여 레이턴시 누적 발생
- queue_size=1로 설정하면 항상 최신 프레임만 유지하고 나머지는 버림
- 험지 탐색 로봇 특성상 이동 속도가 느려 프레임 일부 손실이 탐지 성능에 영향 없음

### 10-7. camera_perception과 image_stream_sender 독립 운용

**결정** : 두 노드가 동기화 없이 각자 최신 프레임을 독립적으로 subscribe

**이유** :

- 추론 대기로 인해 영상 송출이 지연되는 문제 방지
- image_stream_sender는 추론 완료를 기다리지 않으므로 영상 송출이 부드럽게 유지
- 탐지 결과는 별도 토픽(`victim_detection`)으로 전달되므로 영상과 결과가 분리되어도 무관
- 노드 간 블로킹 없음

### 10-8. 이미지 해상도 480x270

**결정** : 토픽 이미지 해상도를 480x270으로 설정

**이유** :

- 추론 입력 크기가 480x480이므로 그 이상의 해상도는 불필요
- 16:9 비율 유지
- 토픽 데이터량 최소화로 통신 부하 감소
- 관제 영상 송출 용도로도 충분한 해상도

### 10-9. Letterbox 전처리

**결정** : 추론 입력 전처리 시 letterbox 방식 적용 (회색 패딩 114)

**이유** :

- 480x270 프레임을 480x480으로 단순 리사이즈 시 비율이 깨져 탐지 성능 저하
- letterbox 방식으로 비율 유지 후 남는 영역을 회색(114)으로 패딩
- YOLO 학습 시 기본 letterbox 패딩 색상과 동일하게 맞춤

### 10-10. 엔진 파일 외부 경로 관리

**결정** : TensorRT 엔진 파일을 `/home/jetson/models/`에 두고 파라미터로 경로 관리

**이유** :

- 엔진 파일은 크기가 크고 버전이 별도로 관리되므로 패키지 내부에 포함 부적절
- yaml 파라미터로 경로를 관리하면 모델 교체 시 코드 수정 없이 yaml만 변경하면 됨
- 학습 버전이 올라가도 패키지 재빌드 불필요

### 10-11. TRT / ONNX 자동 분기

**결정** : `engine_path` 확장자 기준으로 TensorRT / ONNX Runtime 자동 분기

**이유** :

- Orin Nano(`.engine`)와 로컬 PC(`.onnx`) 환경을 yaml 경로 변경만으로 전환 가능
- 코드 수정 없이 환경별 추론 백엔드 교체 가능

### 10-12. TensorRT FP16 사용

**결정** : TensorRT INT8 대신 FP16으로 엔진 변환

**이유** :

- INT8은 캘리브레이션 데이터 없이 변환 시 정밀도 손실로 탐지 실패 (conf ≈ 0) 확인
- FP16은 변환 후 정상 탐지 확인 (conf max 0.79)
- Orin Nano가 FP16 연산을 하드웨어 가속으로 지원

---

## 11. Todo

### 1단계 - 인터페이스 정의

- [x]  `VictimDetection.msg` 초안 작성
- [ ]  팀 전체 리뷰 후 확정

### 2단계 - 드라이버 구현

- [x]  `oak_camera_driver_node` 구현 (image_raw, H.264 encoded 포함)
- [ ]  토픽 출력 확인 (image_raw, encoded)

### 3단계 - 추론 노드 구현

- [x]  `trt_inference.py` 모듈 작성 (TRT/ONNX 자동 분기, letterbox 전처리)
- [x]  `camera_perception_node` 구현 (subscribe + 추론 + publish)
- [ ]  victim_detection 토픽 출력 확인 (Orin Nano TRT 기준)

### 4단계 - 송출 노드 구현

- [ ]  `image_stream_sender_node` 구현 (커스텀 프로토콜 UDP 송출)
- [ ]  H.264 인코딩 스트림 토픽 출력 확인 (/perception/camera/encoded)
- [ ]  인코딩 포맷 확정 후 반영 (변경 시 VideoEncoder Profile 수정)
- [ ]  관제 수신 확인

### 5단계 - 통합 테스트

- [ ]  oak_camera_driver ↔ camera_perception 연동 테스트 (Orin Nano)
- [ ]  oak_camera_driver ↔ image_stream_sender 연동 테스트
- [ ]  network 노드와의 연동 테스트

### 6단계 - 최적화 및 배포

- [ ]  실제 운용 시 fps 및 해상도 조정
- [ ]  confidence 필드 제거 (배포 시점)
- [ ]  H.264 + RGB 동시 운용 안정화 확인 후 Stereo 재검토