# [Localization] 좌표 변환 TF 체인 설계

상태: Localization

- 
    
    TF 체인 구조
    
    ```cpp
    [STM]
      └─ odom_x, odom_y, imu_yaw 전송
    
    [Jetson ROS 2]
      └─ stm_odom_bridge_node
            └─ STM 데이터 수신
    
      └─ localization_node
            ├─ mission_map → odom publish
            ├─ odom → base_link publish
            └─ /localization/pose publish
    
    [LiDAR Perception]
      └─ base_link → laser_frame 사용
      └─ 장애물/FreeSpace는 base_link 기준으로 판단
    
    [UI / Digital Twin]
      └─ /localization/pose 구독
      └─ mission_map 기준 Spot 위치 표시
    ```
    
    패키지 설계 구조
    
    ```cpp
    localization/
     ├── include/localization/
     │    ├── stm_state_bridge_node.hpp
     │    ├── odom_imu_localization_node.hpp
     │    └── mission_map_adapter_node.hpp
     │
     ├── src/
     │    ├── stm_state_bridge_node.cpp
     │    ├── odom_imu_localization_node.cpp
     │    └── mission_map_adapter_node.cpp
     │
     ├── config/
     │    └── localization.param.yaml
     │
     └── launch/
          └── localization_base.launch.py
    ```
    
    [좌표 구조]
    
    ROS TF 트리 구조 :
    
    ```cpp
    mission_map
        ↓
      odom
        ↓
    base_link
        ↓
    laser_frame
    ```
    
    ```cpp
    mission_map
    = 우리가 설계한 공통좌표계
    = GPS 대체용 기준 좌표계
    = UI, 디지털트윈, 다중 로봇 위치 표시 기준
    
    odom
    = STM odometry가 누적되는 로봇 시작점 기준 좌표계
    = 연속적이지만 시간이 지나면 오차 누적
    
    base_link
    = 실제 Spot 몸체 중심 좌표계
    = LiDAR obstacle/free-space 모델이 사용하는 기준
    
    laser_frame
    = CygLiDAR 센서 좌표계
    = 이미 base_link에 static TF로 붙어 있음
    ```
    
    ---
    
    ```cpp
    좌표축 규칙:
    - 전방 이동: +x
    - 좌측 이동: +y
    - 반시계 방향 회전: +yaw
    
    단위:
    - 거리: meter
    - 각도: radian
    - 시간: millisecond
    
    yaw 범위:
    - -pi ~ +pi 또는 0 ~ 2pi 중 하나로 통일
    
    delta 기준:
    - delta_forward_m은 직전 패킷 이후 이동량
    - imu_yaw_rad는 현재 절대 yaw 또는 시작 기준 상대 yaw 중 하나로 명확히 정의
    ```
    
    - timestamp_ms
    - delta_forward_m: 직전 패킷 이후 로봇 전방 이동 추정 거리
    - delta_lateral_m: 좌우 이동 추정 거리, 없으면 0
    - imu_yaw_rad: IMU에서 계산한 현재 yaw
    - angular_velocity_z_rad_s: 가능하면 gyro z축 각속도
    - motion_state: STOP, WALK_FORWARD, TURN_LEFT 등 현재 동작 상태
    
    ---
    

## 0. Localization Pipeline 구조

> 공통좌표계(mission_map) 기준 현재 Spot(base_link)의 pose(x, y, yaw)를 추정하는 과정
> 

#### [TF Tree 구조]

```cpp

        **mission_map ───────────────────────────────────┐**
             │                                         │
             │  (start_x, start_y, start_yaw)          │ 
             ▼                                         │ 
            **odom**                                  **Localization**
             │                                         │
             │  (odom_x, odom_y, odom_yaw)             │
             ▼                                         │
         **base_link ────────────────────────────────────┘**
             │
             │  (static transform)
             ▼
         laser_frame
```

#### [좌표계 정의]

```cpp
**1. mission_map**
- 우리가 만든 **공통좌표계**
- UI / 디지털트윈 / 다중 로봇 위치 표시 기준

**2. odom**
- 로봇이 **localization을 시작한 지점 기준의 로컬 좌표계**
- STM/IMU 기반 이동량이 누적되는 좌표계

**3. base_link**
- **Spot 몸체 중심 좌표계**
- 현재 Spot의 위치를 대표하는 좌표계

**4. laser_frame**
- **LiDAR 센서 좌표계**
- Spot 위치 추정용이 아니라 LiDAR point를 base_link 기준으로 바꾸기 위해 필요
```

#### [Localization Pipeline]

```cpp
[STM]
  └─ 보행/IMU source data 전송
      ├─ timestamp_ms
      ├─ motion_state
      ├─ gait_phase
      ├─ gait_cycle_count
			├─ delta_body_x_m
			├─ delta_body_y_m
			├─ delta_yaw_rad
			├─ imu_yaw_rad
      └─ gyro_z_rad_s

                ↓
                
[Jetson - stm_motion_bridge_node]
  └─ Serial 수신 / ROS 메시지화
      └─ /stm/motion_state

                ↓

[Jetson - odom_estimator_node]
  └─ gait_phase + motion_state + imu_yaw 이용
      ├─ delta_body_x / delta_body_y 계산
      ├─ odom_x / odom_y / odom_yaw 누적
      ├─ /localization/odometry publish
      └─ TF: odom → base_link publish

                ↓

[Jetson - mission_tf_node]
  └─ 시작 위치(start_x, start_y, start_yaw) 기반
      └─ TF: mission_map → odom publish

                ↓

[Jetson - mission_pose_node]
  └─ TF lookup(mission_map → base_link)
      ├─ /localization/pose publish
      └─ /digital_twin/robot_pose publish

                ↓

[UI / Digital Twin / Monitoring]
  └─ mission_map 기준 현재 Spot 위치 시각화
```

---

## 1. 설계 과정

### 1-1. STM에서 source motion data 수신 - gait 기반 odometry

> 
> 
> 
> 현재 로봇이 **1) 어떤 동작(Motion State)**을 하고 있고, **2) 보행 cycle이 어디까지 진행(cycle 진행률)**됐고, **3) IMU yaw가 얼마인지** Jetson에 알려주는 것
> 
> ```cpp
> - Input  : gait_phase, motion_state, imu_yaw
> - Output : /stm/motion_state
> ```
> 

#### 데이터 종류 :

```cpp
**timestamp_ms
seq**

**motion_state
gait_phase
gait_cycle_count**

delta_body_x_m
delta_body_y_m
delta_yaw_rad

**imu_yaw_rad
gyro_z_rad_s**

motion_quality
slip_suspected
```

| 필드명 | 타입 예시 | 단위 | 의미 | 필요한 이유 | 우선순위 |
| --- | --- | --- | --- | --- | --- |
| **`timestamp_ms`** | `uint32` 또는 `uint64` | ms | STM 기준 패킷 생성 시간 | Jetson에서 시간 순서, 지연, 누락, 로그 분석에 필요 | 필수1 |
| `seq` | `uint32` | - | 패킷 순번 | Serial 패킷 누락/중복 감지 | 선택1 |
| **`motion_state`** | `uint8` | - | 현재 동작 상태 | 전진/후진/좌회전/우회전/정지에 따라 odom 계산 방식이 달라짐 | 필수1 |
| **`gait_phase`** | `float` | 0.0~1.0 | 현재 보행 cycle 진행률 | 한 cycle 이동량을 부드럽게 나눠서 odom에 누적하기 위해 필요 - 4개 다리 전체 보행 패턴 기준 | 필수1 |
| **`gait_cycle_count`** | `uint32` | cycle | 완료된 gait cycle 누적 수 | 실제 이동거리 측정 후 step length 보정에 필요 | 필수1 |
| **`imu_yaw_rad`** | `float` | rad | IMU에서 추정한 현재 로봇의 yaw 방향각 | 현재 Spot heading 추정에 필요 | 필수1 |
| **`gyro_z_rad_s`** | `float` | rad/s | z축 각속도 | 회전 상태 검증, yaw 노이즈 판단, 정지 중 drift 억제에 필요 | 필수1 |
| `delta_body_x_m` | `float` | m | 직전 패킷 이후 로봇 전방 기준 이동 추정량 | Jetson이 odom_x/y를 누적하는 직접 재료 |  |
| `delta_body_y_m` | `float` | m | 직전 패킷 이후 로봇 좌우 기준 이동 추정량 | 좌우 이동 또는 회전 중 옆밀림 보정에 필요 |  |
| `delta_yaw_rad` | `float` | rad | 직전 패킷 이후 yaw 변화량 | IMU yaw와 비교 검증 또는 fallback에 사용 |  |
| `motion_quality` | `uint8` 또는 `float` | - | 현재 이동 추정 신뢰도 | 전환 구간/불안정 보행/미끄러짐 상황에서 odom 누적을 보수적으로 처리 |  |
| `slip_suspected` | `bool` | - | 미끄러짐 의심 여부 | odom drift가 커지는 상황 감지 |  |

---

#### motion_state 정의 예시 :

```
0: STOP
1: WALK_FORWARD
2: WALK_BACKWARD
3: TURN_LEFT
4: TURN_RIGHT
5: STRAFE_LEFT
6: STRAFE_RIGHT
7: TRANSITION
8: UNKNOWN
```

각 상태의 의미는 다음과 같이 고정하면 된다.

| motion_state | 의미 | Jetson에서의 처리 |
| --- | --- | --- |
| `STOP` | 정지 | `delta_body_x/y = 0`, yaw drift 억제 |
| `WALK_FORWARD` | 전진 | `+x` 방향 이동량 누적 |
| `WALK_BACKWARD` | 후진 | `-x` 방향 이동량 누적 |
| `TURN_LEFT` | 좌회전 | IMU yaw 증가 방향 반영 |
| `TURN_RIGHT` | 우회전 | IMU yaw 감소 방향 반영 |
| `STRAFE_LEFT` | 왼쪽 옆이동 | `+y` 방향 이동량 누적 |
| `STRAFE_RIGHT` | 오른쪽 옆이동 | `-y` 방향 이동량 누적 |
| `TRANSITION` | 동작 전환 중 | odom 누적 보수 처리 |
| `UNKNOWN` | 상태 불명 | odom 누적 중지 또는 low quality 처리 |

#### 좌표/부호 규칙 합의

```cpp
base_link 기준

+x : 로봇 전방
+y : 로봇 왼쪽
+z : 위쪽

+yaw : 반시계 방향, 좌회전
단위:
거리 = meter
각도 = radian
시간 = millisecond
```

따라서 :

```cpp
delta_body_x_m > 0  : 전진
delta_body_x_m < 0  : 후진
delta_body_y_m > 0  : 왼쪽 옆이동
delta_body_y_m < 0  : 오른쪽 옆이동
delta_yaw_rad > 0   : 좌회전
delta_yaw_rad < 0   : 우회전
```

---

### 1-2. body-frame delta 계산

> odom(로봇의 시작점 기준 누적 로컬 좌표계) 좌표계를 만들기 위해 **“이번 주기 동안 로봇 몸체 기준으로 얼마나 움직였는가?”**를 계산하는 과정
> 
> 
> ```cpp
> - Input  : motion_state, gait_phase
> - Output : delta_body_x/y
> ```
> 

산출 데이터 :

```cpp
delta_body_x     # 로봇 전방 기준 이동량
delta_body_y     # 로봇 좌우 기준 이동량
delta_yaw        # 로봇이 z축 기준으로 회전한 각도 변화량
```

해당 산출 데이터를 계산하기 위한 과정은 아래와 같다.

예를 들어 `motion_state = WALK_FORWARD` 이고, **전진 1 cycle당 이동량이 0.03m(실측 필요)**라고 가정하자.

```cpp
forward_step_length_m = 0.03     # 전진 1 cycle당 이동량
```

이전 phase가 0.25였고, 현재 phase가 0.50이면 :

```cpp
delta_phase = 0.50 - 0.25 = 0.25        # phase 변위 값
delta_body_x = 0.03 * 0.25 = 0.0075m    # 전방 이동량 = (1cycle 이동량) x (phase 변위량)
```

- 즉, 한 cycle 전체 이동량을 phase 진행률만큼 나눠서 적용해 이동량을 계산한다.
- 이는 동작별로 아래와 같이 분리해야함. 왜냐하면 **전진/후진/좌/우 간의 1 cycle 이동량은 다르기 때문**

[동작별 이동량 계산 과정] 

```cpp
WALK_FORWARD:    # 전진
  delta_body_x = +forward_step_length * delta_phase

WALK_BACKWARD:   # 후진
  delta_body_x = -backward_step_length * delta_phase

STRAFE_LEFT:     # 헤더 방향 유지한 채 몸체 왼쪽으로 이동
  delta_body_y = +strafe_left_step_length * delta_phase

STRAFE_RIGHT:    # 헤더 방향 유지한 채 몸체 오른쪽으로 이동
  delta_body_y = -strafe_right_step_length * delta_phase

TURN_LEFT / TURN_RIGHT:   # 제자리에서 왼쪽 회전 / 오른쪽 회전
  위치 이동은 0 또는 drift 보정값
  yaw는 IMU yaw 우선 사용
```

---

### 1-3. odom 좌표계 누적 (odom → base_link)

> 이전에 계산한 **동작별 이동량**를 바탕으로 로봇이 시작한 지점에서부터 odom 좌표계를 계산하는 과정
> 
> 
> *odom : 로봇이 시작한 지점에 고정된 로컬 좌표계
> 
> ```cpp
> - Input  : delta_body, imu_yaw
> - Output : /localization/odometry, odom → base_link
> ```
> 

odom 좌표계의 시작점(원점)은 **해당 노드를 실행한 순간**부터 아래와 같이 정의된다.

```cpp
odom_x = 0
odom_y = 0
odom_yaw = 0
imu_yaw_start = 첫 번째 IMU yaw
```

- 이후 매 주기마다 `delta_body_x` 와 `delta_body_y`를 현재 yaw 방향에 맞춰 odm 좌표계로 누적

[odom 누적 과정]

```cpp
yaw = imu_yaw_current - imu_yaw_start + yaw_offset

odom_x += delta_body_x * cos(yaw) - delta_body_y * sin(yaw)
odom_y += delta_body_x * sin(yaw) + delta_body_y * cos(yaw)
odom_yaw = yaw
```

결과 :

```cpp
odom 기준 base_link 위치
= odom_x, odom_y, odom_yaw
```

[publish 데이터]

```cpp
/localization/odometry
/tf: odom → base_link
```

```cpp
header.frame_id = "odom"
child_frame_id  = "base_link"

pose.position.x = odom_x
pose.position.y = odom_y
pose.orientation = odom_yaw를 quaternion으로 변환한 값
```

- 이 과정까지 진행된다면 `odom → base_link` 가 완성된다.

---

### 1-4. mission_map에 odom 시작점 배치

> odom 좌표계를 공통좌표계(mission_map) 안에 배치하는 과정
> 
> 
> ```cpp
> - Input  : start_x/y/yaw
> - Output : mission_map → odom
> ```
> 

예를 들어 로봇을 시연장 공통좌표계에서 다음 위치에 놓고 시작한다고 하자.

```
start_x = 2.0m
start_y = 1.0m
start_yaw = 90deg
```

이 값은 다음 transform이 된다.

```
mission_map → odom
```

의미는:

```
odom 원점이 mission_map 기준으로 x=2.0, y=1.0에 있고,
odom x축이 mission_map 기준으로 90도 회전해 있다.
```

이 단계에서 publish해야 할 TF는:

```
/tf: mission_map → odom
```

초기 MVP에서는 이 값은 고정값으로 둬도 된다.

```
mission_frame:"mission_map"
odom_frame:"odom"
base_frame:"base_link"

start_x: 2.0
start_y: 1.0
start_yaw_deg: 90.0
```

이 단계까지 하면 TF 체인이 완성된다.

```
mission_map → odom → base_link
```

---

### 1-5. mission_map 기준 현재 Spot pose 산출

> ROS TF 과정(`mission_map → odom → base_link`)의 최종 결과를 publish하는 과정
> 
> 
> ```cpp
> - Input  : TF chain
> - Output : /localization/pose
> ```
> 

ROS TF에서 다음 transform을 조회하면 된다.

```
mission_map → base_link
```

이 값이 바로 우리가 원하는 최종 결과

```
현재 Spot pose in mission_map
```

출력 토픽 :

```
/localization/pose
```

메시지는 `geometry_msgs/PoseStamped` 사용

```
header.frame_id = "mission_map"

pose.position.x = mission_map 기준 현재 Spot x
pose.position.y = mission_map 기준 현재 Spot y
pose.orientation = mission_map 기준 현재 Spot yaw
```

이후 UI나 디지털트윈은 이 토픽만 구독하면 된다.

```
/digital_twin/robot_pose
```

로 별도 publish해도 된다.