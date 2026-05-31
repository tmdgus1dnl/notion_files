# [Vender] oak_camera_driver 패키지 보고서

상태: VISION

> 작성일 : 2026-04-23
프로젝트 : SPOT Get IT
담당 파트 : SIM / APP (인명 탐지)
> 

---

## 1. 패키지 개요

| 항목 | 내용 |
| --- | --- |
| 패키지명 | `oak_camera_driver` |
| 위치 | `robot_ws/src/vendor/oak_camera_driver/` |
| 역할 | OAK-D Lite 카메라 제어 및 스트림 publish |
| 계층 | vendor (외부 디바이스 드라이버) |
| 언어 | Python |
| 빌드 시스템 | ament_python |

OAK-D Lite 디바이스를 직접 점유하고 RGB RAW 스트림과 H.264 인코딩 스트림을 ROS2 토픽으로 publish합니다. 카메라 제어 로직을 분리하여 `camera_perception`과 `image_stream_sender`가 독립적으로 스트림을 subscribe할 수 있습니다.

---

## 2. 패키지 구조

```
vendor/oak_camera_driver/
├── package.xml
├── setup.py
├── setup.cfg
├── oak_camera_driver/
│   ├── __init__.py
│   └── oak_camera_driver_node.py
├── launch/
│   └── oak_camera_driver.launch.py
├── config/
│   └── oak_camera_driver.param.yaml
└── README.md
```

---

## 3. 노드 명세

### oak_camera_driver_node

### Subscribe

없음 (카메라 직접 제어)

### Publish

| 토픽 | 타입 | 내용 |
| --- | --- | --- |
| `/perception/camera/image_raw` | `sensor_msgs/Image` | RAW BGR 스트림 |
| `/perception/camera/encoded` | `sensor_msgs/CompressedImage` | H.264 인코딩 스트림 (format=h264) |

### 파라미터

| 파라미터 | 타입 | 기본값 | 내용 |
| --- | --- | --- | --- |
| `fps` | int | 15 | 카메라 프레임 레이트 |
| `rgb_width` | int | 480 | RGB 출력 너비 (정규화 좌표 기준 해상도) |
| `rgb_height` | int | 270 | RGB 출력 높이 (정규화 좌표 기준 해상도) |
| `show_preview` | bool | false | imshow 미리보기 활성화 여부 |
| `preview_width` | int | 1280 | 미리보기 창 너비 |
| `preview_height` | int | 720 | 미리보기 창 높이 |

---

## 4. 의존성

| 패키지 | 용도 |
| --- | --- |
| `depthai` | OAK-D Lite SDK (v3.5.0) - **반드시 v3 사용. v2와 API 호환 불가** |
| `rclpy` | ROS2 Python 클라이언트 |
| `sensor_msgs` | Image, CompressedImage 메시지 타입 |
| `cv_bridge` | OpenCV ↔ ROS2 이미지 변환 |
| `opencv-python` | 이미지 처리 |

---

## 5. 빌드 및 실행

### 빌드

```bash
cd ~/robot_ws
colcon build --packages-select oak_camera_driver
source install/setup.bash
```

### 실행

```bash
ros2 launch oak_camera_driver oak_camera_driver.launch.py
```

### 토픽 확인

```bash
ros2 topic list
ros2 topic hz /perception/camera/image_raw
ros2 topic hz /perception/camera/encoded
```

---

## 6. 설계 결정

### depthai-ros 공식 드라이버 미사용

DepthAI v3 API 기준 공식 ROS2 드라이버 지원 여부가 불확실합니다. SDK 기반 카메라 동작이 이미 검증됐고 추가 의존성 없이 빠르게 개발할 수 있어 SDK 직접 사용 방식을 채택했습니다.

### H.264 인코딩 임시 결정

인코딩 포맷을 H.264로 임시 결정하여 개발을 시작했습니다. OAK-D Lite `VideoEncoder` 노드의 Profile만 수정하면 H.265, MJPEG으로 전환할 수 있습니다. 통신팀 확정 시 변경 예정입니다.

### Stereo Depth 미포함

프로젝트에서 Stereo Depth 활용처가 victim 거리 추정 외에 없으며 (장애물 회피, 로컬라이제이션은 LiDAR 담당), OAK-D Lite 온보드 메모리 한계로 RGB + H.264 + Stereo 동시 운용이 불가능합니다. 거리 추정보다 영상 송출 우선순위가 높다고 판단하여 Stereo를 제외했습니다.

---

## 7. 트러블슈팅

| 증상 | 원인 | 해결 방법 | 상태 |
| --- | --- | --- | --- |
| `X_LINK_DEVICE_NOT_FOUND` | OAK-D Lite 미연결 또는 udev rules 미설정 | udev rules 추가 후 재연결 | ✅ 해결 |
| H.264 인코더 해상도 오류 | H264는 width 32의 배수, height 8의 배수 필요 | 인코더 입력 해상도 자동 보정 | ✅ 해결 |
| Stereo 메모리 부족 | RGB + H.264 + Stereo 동시 운용 불가 | Stereo 제거 | ✅ 해결 |
| 원격 접속 끊김 | 1080P 스트림 + imshow 부하 | imshow 해상도 파라미터화, 캡쳐 해상도 조정 | ✅ 해결 |

---

## 8. Todo

- [ ]  토픽 출력 확인 (image_raw, encoded) - Orin Nano 기준
- [ ]  image_stream_sender 연동 테스트
- [ ]  통신팀 인코딩 포맷 확정 후 반영