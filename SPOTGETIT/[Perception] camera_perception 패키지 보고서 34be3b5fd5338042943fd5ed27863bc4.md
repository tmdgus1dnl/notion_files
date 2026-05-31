# [Perception] camera_perception 패키지 보고서

상태: Perception

> 작성일 : 2026-04-23
프로젝트 : SPOT Get IT
담당 파트 : SIM / APP (인명 탐지)
> 

---

## 1. 패키지 개요

| 항목 | 내용 |
| --- | --- |
| 패키지명 | `camera_perception` |
| 위치 | `robot_ws/src/perception/camera_perception/` |
| 역할 | RGB 스트림 수신 → YOLOv11 추론 → 인명 탐지 결과 publish |
| 계층 | perception |
| 언어 | Python |
| 빌드 시스템 | ament_python |

`oak_camera_driver`가 publish하는 RGB 스트림을 subscribe하여 YOLOv11 + TensorRT(Orin Nano) 또는 ONNX Runtime(로컬 PC) 기반으로 인명 탐지를 수행하고 결과를 `victim_detection` 토픽으로 publish합니다.

---

## 2. 패키지 구조

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

---

## 3. 노드 명세

### camera_perception_node

### Subscribe

| 토픽 | 타입 | 내용 |
| --- | --- | --- |
| `/perception/camera/image_raw` | `sensor_msgs/Image` | RAW BGR 스트림 (oak_camera_driver 발행) |

### Publish

| 토픽 | 타입 | 내용 |
| --- | --- | --- |
| `/perception/camera/victim_detection` | `robot_interfaces/VictimDetection` | 인명 탐지 결과 |

> `robot_interfaces` 패키지 빌드 전까지 `victim_detection` publish는 주석 처리 상태. 현재는 탐지 결과를 로그로 출력합니다.
> 

### 파라미터

| 파라미터 | 타입 | 기본값 | 내용 |
| --- | --- | --- | --- |
| `engine_path` | str | `/home/jetson/models/best_fp16.engine` | 모델 파일 경로 (.engine 또는 .onnx) |
| `conf_threshold` | float | 0.25 | 탐지 confidence 임계값 |
| `iou_threshold` | float | 0.45 | NMS IOU 임계값 |
| `infer_size` | int | 480 | 추론 입력 크기 (정사각형) |
| `depth_roi_size` | int | 5 | 거리 추정 ROI 크기 (현재 미사용) |
| `depth_smooth_frames` | int | 5 | 거리 스무딩 프레임 수 (현재 미사용) |
| `show_preview` | bool | false | imshow 미리보기 활성화 여부 |
| `preview_width` | int | 640 | 미리보기 창 너비 |
| `preview_height` | int | 480 | 미리보기 창 높이 |

---

## 4. trt_inference 모듈

`engine_path` 확장자를 기준으로 추론 백엔드를 자동 분기합니다.

| 확장자 | 백엔드 | 환경 |
| --- | --- | --- |
| `.engine` | TensorRT FP16 | Orin Nano |
| `.onnx` | ONNX Runtime | 로컬 PC |

yaml에서 `engine_path`만 변경하면 환경 전환이 가능합니다.

### 전처리 (Letterbox)

입력 프레임을 비율 유지하면서 추론 입력 크기(480x480)로 리사이즈합니다. 남는 영역은 회색(114)으로 패딩합니다. YOLO 학습 시 기본 letterbox 패딩 색상과 동일하게 맞췄습니다.

```
480x270 입력
        ↓ letterbox
480x480 (상하 회색 패딩)
        ↓ 추론
```

### 후처리

1. confidence 임계값 필터링
2. NMS (`cv2.dnn.NMSBoxes`)
3. bbox 정규화 좌표 변환 (0~1)

---

## 5. 커스텀 메시지 명세

### VictimDetection.msg

```
# robot_interfaces/msg/VictimDetection.msg

std_msgs/Header header

float32 bbox_x          # bbox 좌측 상단 x (정규화, 0~1)
float32 bbox_y          # bbox 좌측 상단 y (정규화, 0~1)
float32 bbox_w          # bbox 너비 (정규화, 0~1)
float32 bbox_h          # bbox 높이 (정규화, 0~1)
float32 distance        # 추정 거리 (m) - Stereo 미포함으로 현재 0.0 고정
float32 confidence      # 탐지 신뢰도 (개발 중 사용, 배포 시 제거 예정)
```

> bbox 좌표는 정규화(0~1) 좌표를 사용합니다. 기준 해상도는 `oak_camera_driver.param.yaml`의 `rgb_width`, `rgb_height`입니다.
> 

---

## 6. 모델 파일 관리

TensorRT 엔진 파일은 패키지 내부에 포함하지 않습니다.

```
/home/jetson/models/
└── best_fp16.engine
```

| 환경 | engine_path |
| --- | --- |
| Orin Nano | `/home/jetson/models/best_fp16.engine` |
| 로컬 PC | `/home/ubuntu/models/best.onnx` |

---

## 7. 의존성

| 패키지 | 용도 |
| --- | --- |
| `rclpy` | ROS2 Python 클라이언트 |
| `sensor_msgs` | Image 메시지 타입 |
| `cv_bridge` | OpenCV ↔ ROS2 이미지 변환 |
| `robot_interfaces` | VictimDetection 커스텀 메시지 |
| `tensorrt` | TensorRT FP16 추론 (Orin Nano) |
| `onnxruntime` | ONNX 추론 (로컬 PC 개발용) |
| `cuda` | GPU 추론 가속 |
| `numpy` | 배열 연산 |
| `opencv-python` | 이미지 처리, NMS |

---

## 8. 빌드 및 실행

### 빌드

```bash
cd ~/robot_ws
colcon build --packages-select camera_perception
source install/setup.bash
```

### 실행

```bash
ros2 launch camera_perception camera_perception.launch.py
```

### 토픽 확인

```bash
ros2 topic list
ros2 topic echo /perception/camera/victim_detection
```

---

## 9. 설계 결정

### queue_size=1

추론 시간이 프레임 간격보다 길 경우 큐가 쌓여 레이턴시가 누적됩니다. queue_size=1로 설정하여 항상 최신 프레임만 유지하고 나머지는 버립니다. 험지 탐색 로봇 특성상 이동 속도가 느려 프레임 손실이 탐지 성능에 영향을 미치지 않습니다.

### Letterbox 전처리

480x270 프레임을 480x480으로 단순 리사이즈하면 종횡비가 깨져 탐지 성능이 저하됩니다. Letterbox 방식으로 비율을 유지하면서 회색 패딩을 추가합니다.

### TRT / ONNX 자동 분기

Orin Nano와 로컬 PC 환경을 yaml의 `engine_path`만 변경하여 전환할 수 있습니다. 코드 수정 없이 환경별 추론 백엔드를 교체할 수 있습니다.

---

## 10. 트러블슈팅

---

## 11. Todo

- [ ]  `robot_interfaces` 패키지 빌드 후 `victim_detection` publish 활성화
- [ ]  victim_detection 토픽 출력 확인 (Orin Nano TRT 기준)
- [ ]  oak_camera_driver ↔ camera_perception 연동 테스트 (Orin Nano)
- [ ]  network 노드와의 연동 테스트
- [ ]  confidence 필드 제거 (배포 시점)