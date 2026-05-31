# [Interface] VictimDetection.msg 보고서

상태: VISION

> 작성일 : 2026-04-23
프로젝트 : SPOT Get IT
담당 파트 : SIM / APP (인명 탐지)
> 

---

## 1. 개요

| 항목 | 내용 |
| --- | --- |
| 메시지명 | `VictimDetection` |
| 위치 | `robot_ws/src/interfaces/robot_interfaces/msg/VictimDetection.msg` |
| Publisher | `camera_perception_node` |
| Subscriber | decision 노드, network 노드 |
| 토픽 | `/perception/camera/victim_detection` |

인명 탐지 결과를 전달하는 메시지입니다. 탐지된 사람의 bbox 위치와 거리 정보를 포함합니다.

---

## 2. 메시지 정의

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

---

## 3. 필드 상세

| 필드 | 타입 | 범위 | 설명 |
| --- | --- | --- | --- |
| `header` | `std_msgs/Header` | - | 타임스탬프, frame_id |
| `bbox_x` | float32 | 0~1 | bbox 좌측 상단 x 좌표 (정규화) |
| `bbox_y` | float32 | 0~1 | bbox 좌측 상단 y 좌표 (정규화) |
| `bbox_w` | float32 | 0~1 | bbox 너비 (정규화) |
| `bbox_h` | float32 | 0~1 | bbox 높이 (정규화) |
| `distance` | float32 | 0.0~ | 추정 거리 (m), 현재 0.0 고정 |
| `confidence` | float32 | 0~1 | 탐지 신뢰도, 배포 시 제거 예정 |

---

## 4. 설계 결정

### bbox 정규화 좌표

픽셀 좌표 대신 정규화 좌표(0~1)를 사용합니다. 해상도가 변경되어도 bbox 값이 동일하게 유지되어 수신 측에서 해상도에 독립적으로 처리할 수 있습니다. 기준 해상도는 `oak_camera_driver.param.yaml`의 `rgb_width`, `rgb_height`에서 관리합니다.

### detected 필드 미포함

탐지 결과가 없으면 토픽 자체를 publish하지 않는 방식입니다. `detected` 필드를 별도로 두지 않아 메시지를 단순하게 유지합니다.

### distance 필드 현재 0.0 고정

OAK-D Lite 메모리 한계로 Stereo Depth를 제거하여 거리 추정이 불가능합니다. 향후 해결 방안 검토 시 활성화 예정입니다.

### confidence 필드 배포 시 제거

개발 단계에서 임계값 튜닝 및 디버깅 용도로 포함했습니다. 배포 시점에는 confidence 임계값을 통과한 결과만 publish하므로 필드가 불필요합니다.

---

## 5. 사용 예시

```python
# Publisher (camera_perception_node)
from robot_interfaces.msg import VictimDetection

msg = VictimDetection()
msg.header.stamp = self.get_clock().now().to_msg()
msg.bbox_x      = 0.25
msg.bbox_y      = 0.10
msg.bbox_w      = 0.50
msg.bbox_h      = 0.80
msg.distance    = 0.0
msg.confidence  = 0.87
self.victim_pub.publish(msg)
```

```python
# Subscriber
from robot_interfaces.msg import VictimDetection

def callback(msg):
    # 픽셀 좌표 복원 (해상도 480x270 기준)
    w, h = 480, 270
    x1 = int(msg.bbox_x * w)
    y1 = int(msg.bbox_y * h)
    x2 = int((msg.bbox_x + msg.bbox_w) * w)
    y2 = int((msg.bbox_y + msg.bbox_h) * h)
```

---

## 6. Todo

- [ ]  팀 전체 리뷰 후 확정
- [ ]  `robot_interfaces` 패키지 빌드 후 `camera_perception_node` publish 활성화
- [ ]  distance 필드 활성화 방안 검토 (Stereo 재도입 또는 대안)
- [ ]  confidence 필드 제거 (배포 시점)