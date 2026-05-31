# [Localization] TF 체인 구현

상태: Localization

## 1. STM32 ↔ Jetson (SPI 통신 기반 bridge 노드)

SPI로 들어오는 STM 데이터를 ROS2에서 바로 토픽으로 사용할 수 없기에 SPI 통신 데이터를 **ROS2 메시지로 변환해주는 Bridge Node**가 필요함

구조 :

```cpp
[STM]
  └─ SPI packet 송신/응답
        ↓
[Jetson Linux SPI driver: /dev/spidevX.Y]
        ↓
[ROS 2 spi_stm_bridge_node]
        ↓
/localization/stm_motion
        ↓
[gait_odom_estimator_node]
        ↓
/localization/odometry
TF: odom → base_link
```

### 1-1. SPI 데이터 ROS2 활용

Jetson은 Ubuntu/Linux 위에서 동작하고, SPI 장치는 보통 아래 같은 디바이스 파일로 노출돼.

```
/dev/spidev0.0
/dev/spidev0.1
/dev/spidev1.0
```

ROS 2 노드는 결국 일반 Linux 프로그램이니까, C++ 노드 안에서 이 파일을 열고 SPI 통신을 수행하면 돼.

즉 구조는 이거야.

```
ROS 2 node
= rclcpp 기반 C++ 프로그램
+ 내부에서 Linux SPI device read/write
+ 받은 binary packet을 파싱
+ ROS 2 msg로 publish
```

그래서 새로 만들 노드는 이런 역할이야.

```
spi_stm_bridge_node

역할:
- Jetson SPI device open
- STM으로부터 motion packet 수신
- CRC/seq/timestamp 검증
- robot_interfaces/msg/StmMotion으로 변환
- /localization/stm_motion publish
```

### 1-2. SPI packet format 정의

```cpp
#pragma pack(push, 1)
struct StmMotionPacket {
  uint16_t magic;            // 0xA55A
  uint8_t version;           // protocol version
  uint8_t payload_len;       // payload size

  uint32_t timestamp_ms;
  uint32_t seq;

  uint8_t motion_state;
  float gait_phase;
  uint32_t gait_cycle_count;

  float imu_yaw_rad;
  float gyro_z_rad_s;

  uint16_t crc16;
};
#pragma pack(pop)
```

| 필드 | 이유 |
| --- | --- |
| `magic` | packet 시작 확인 |
| `version` | 추후 프로토콜 변경 대비 |
| `payload_len` | packet 길이 검증 |
| `seq` | 누락/중복 확인 |
| `crc16` | SPI 데이터 깨짐 확인 |
| payload | 실제 localization source |

### 1-3. Jetson 쪽 SPI bridge node 동작 흐름

`spi_stm_bridge_node`는 이렇게 동작하면 돼.

```
1. /dev/spidevX.Y open
2. SPI mode, speed, bits 설정
3. ROS timer 생성
4. timer callback마다 SPI transfer 수행
5. 수신 packet 검증
6. StmMotion 메시지로 변환
7. /localization/stm_motion publish
```

의사코드로 보면:

```
voidtimerCallback()
{
StmMotionPacketpacket;

boolok =spiTransfer(packet);

if (!ok) {
RCLCPP_WARN(...,"SPI transfer failed");
return;
  }

if (packet.magic!=0xA55A) {
RCLCPP_WARN(...,"Invalid packet magic");
return;
  }

if (!checkCrc(packet)) {
RCLCPP_WARN(...,"CRC error");
return;
  }

  robot_interfaces::msg::StmMotionmsg;
msg.header.stamp =this->now();
msg.header.frame_id ="stm";

msg.timestamp_ms =packet.timestamp_ms;
msg.seq =packet.seq;
msg.motion_state =packet.motion_state;
msg.gait_phase =packet.gait_phase;
msg.gait_cycle_count =packet.gait_cycle_count;
msg.imu_yaw_rad =packet.imu_yaw_rad;
msg.gyro_z_rad_s =packet.gyro_z_rad_s;

publisher_->publish(msg);
}
```

#### [SPI polling 주기]

Localization odom용이면 처음에는 30~50Hz 정도면 충분해.

추천:

```
초기 개발:
20Hz 또는 30Hz

실제 odom:
50Hz

너무 빠른 주기:
100Hz 이상은 STM/SPI/Jetson 부하 보고 결정
```

CygLiDAR가 약 15Hz 수준이고, 보행 odom은 30~50Hz면 충분히 부드럽게 처리 가능해.

#### [STM32’s double buffer]

STM은 제어 루프에서 계속 motion data를 갱신하고 있을 거야.

SPI 응답 중에 데이터가 반쯤 바뀌면 packet이 깨질 수 있어.

그래서 STM 쪽은 이런 구조가 좋다.

```
control loop:
current_motion_data 갱신

SPI 응답 직전:
current_motion_data를 tx_packet_buffer에 복사
CRC 계산
SPI slave response buffer로 제공
```

즉 SPI가 읽는 동안 값들이 바뀌지 않게 해야 한다.

---

## 2. Localization 패키지

개발 순서 :

```cpp
1. StmMotion.msg 정의
2. stm_motion_mock_node 개발
3. gait_odom_estimator_node 개발
4. mission_tf_node 개발
5. localization_base.launch.py 작성
6. mock 데이터로 TF/odom 검증
7. 실제 SPI bridge 완성 후 mock을 bridge로 교체
```

```cpp
spot_localization/
├── include/spot_localization/
│   ├── gait_odom_estimator_node.hpp
│   └── mission_tf_node.hpp
├── src/
│   ├── stm_motion_mock_node.cpp
│   ├── gait_odom_estimator_node.cpp
│   └── mission_tf_node.cpp
├── config/
│   ├── gait_odom.param.yaml
│   └── mission_tf.param.yaml
├── launch/
│   └── localization_base.launch.py
├── CMakeLists.txt
└── package.xml
```

| 노드 | 역할 |
| --- | --- |
| `spi_stm_bridge_node` | SPI로 STM 데이터 수신 후 ROS 2 topic publish |
| `stm_motion_mock_node` | STM 없이 테스트용 fake 데이터 publish |
| `gait_odom_estimator_node` | `/localization/stm_motion` 구독 후 odom 계산 |
| `mission_tf_node` | 초기 `mission_map → odom` publish |

```
SPI 통신 처리와 odometry 계산을 분리한다.
```

즉 `spi_stm_bridge_node`는 통신만 담당하고, `gait_odom_estimator_node`는 ROS 메시지만 보고 odom을 계산하게 만드는 게 좋아.

이렇게 하면 나중에 SPI가 UART, CAN, UDP로 바뀌어도 odom 계산 노드는 안 바꿔도 돼.

---

### 2-0. `stm_motion_mock_node` - 더미 데이터 생성기

더미 데이터 반복 생성 시나리오 :

```cpp
0 ~ 2초    : STOP
2 ~ 7초    : WALK_FORWARD
7 ~ 9초    : TURN_LEFT
9 ~ 14초   : WALK_FORWARD
14 ~ 16초  : STOP
```

### 2-1. `StmMotion.msg`

```cpp
# robot_interfaces/msg/StmMotion.msg

std_msgs/Header header

uint32 timestamp_ms
uint32 seq

uint8 motion_state

float32 gait_phase
uint32 gait_cycle_count

float32 imu_yaw_rad
float32 gyro_z_rad_s

uint8 STOP=0
uint8 WALK_FORWARD=1
uint8 WALK_BACKWARD=2
uint8 TURN_LEFT=3
uint8 TURN_RIGHT=4
uint8 STRAFE_LEFT=5
uint8 STRAFE_RIGHT=6
uint8 TRANSITION=7
uint8 UNKNOWN=8
```

SPI packet을 받은 뒤, bridge node에서 이 메시지로 변환해서 publish한다.

```
SPI binary packet
→ parse
→ StmMotion msg
→ /localization/stm_motion
```

### 2-2. gait odom estimator node

> 
> 
> 
> 로봇 시작 지점을 원점으로 하는 local odom 좌표계 안에서 현재 base_link가 어디까지 이동했는지 누적 추정하는 노드
> 

`gait_odom_estimator_node`는 SPI로 왔는지 UART로 왔는지 몰라도 된다. 즉, 통신과 위치 처리는 서로 별개의 계층으로 아래의 topic만 구독한 채로 개발 진행

```cpp
Subscribe:
/localization/stm_motion

Publish:
/localization/odometry

TF:
odom → base_link
```

통신 계층과 odom 계층을 분리 :

```cpp
SPI 문제 발생
→ spi_stm_bridge_node만 수정

odom 계산 문제 발생
→ gait_odom_estimator_node만 수정
```

---

#### [odom estimator 계산 흐름]

```cpp
1. /localization/stm_motion 수신
2. 첫 메시지면 초기화
   - imu_yaw_start 저장
   - prev_total_phase 저장
3. total_phase 계산
4. delta_phase 계산
5. motion_state에 따라 body 이동량 계산
6. imu_yaw_rad로 odom_yaw 계산
7. body 이동량을 odom frame으로 변환
8. odom_x, odom_y 누적
9. /localization/odometry publish
10. TF odom → base_link publish
```

**[odometry 계산 구조]**

```bash
WALK_FORWARD
→ base_link +x 이동량 생성
→ 현재 yaw 반영
→ odom_x, odom_y 누적

WALK_BACKWARD
→ base_link -x 이동량 생성
→ 현재 yaw 반영
→ odom_x, odom_y 누적

STRAFE_LEFT
→ base_link +y 이동량 생성
→ 현재 yaw 반영
→ odom_x, odom_y 누적

STRAFE_RIGHT
→ base_link -y 이동량 생성
→ 현재 yaw 반영
→ odom_x, odom_y 누적

TURN_LEFT / TURN_RIGHT
→ x/y 이동량은 0
→ yaw만 IMU 기준으로 갱신
```

현재 mock node와 연결하면 기대 동작은 다음처럼 나와야 해.

```cpp
STOP          → x, y, yaw 유지
WALK_FORWARD  → x 증가
TURN_LEFT     → yaw 증가, x/y 거의 유지
WALK_FORWARD  → 회전된 yaw 방향으로 이동
STOP          → pose 유지
```

![image.png](image%2042.png)

빨간 화살표의 머리 방향은 **그 시점에 로봇이 바라보는 방향**

1. double yaw_offset_rad_ 값이 IMU yaw와 실제 base_link yaw 사이의 고정 offset 보정값이라고 했는데, IMU yaw 값과 base_link yaw 값을 두개 비교한다는 건데, 어떻게 비교하는거지? STM에서 보내주는 IMU yaw 값은 IMU yaw와 실제 base_link yaw 중에 어디에 해당된다는 거지?
2. 그리고 imu_yaw_sign_ 값은 STM IMU yaw 부호가 ROS 기준 반대일 경우 -1.0 기준으로 설정한다고 했는데, 이미 팀원과 합의를 봤다면 필요 없는거 아닌가? 그래도 필요한가?
3. max_delta_phase_ 값이 한 callback에서 허용할 최대 gait phase 변화량이라고 했는데, 이 변화량은 내가 정해주는 거라면 어떤 값으로 정해줘야되는거지? 그리고 odom -> base_link TF publish 여부를 나타내는 publish_tf_ 변수는 왜 필요한거지?

### 2-3. mission_tf_node

```cpp
gait_odom_estimator_node
= STM 데이터 기반 odom -> base_link 계산

mission_tf_node
= 시작 위치 기반 mission_map -> odom 고정

최종 pose
= TF 합성 결과인 mission_map -> base_link
```

최종 구조 :

```cpp
stm_bridge_node
    ↓ /localization/stm_motion

gait_odom_estimator_node
    ↓ /localization/odometry
    ↓ TF: odom -> base_link

mission_tf_node
    ↓ TF: mission_map -> odom

localization_pose_node
    ↓ /localization/pose
```

최중 TF Tree 구조 :

```cpp
mission_map
   └── odom
        └── base_link
             └── laser_frame
```

![image.png](image%2043.png)

![image.png](image%2044.png)

### 2-4. 검증 확인 방법

launch 실행 :

```cpp
ros2 launch spot_localization localization_tf_chain.launch.py
```

1️⃣ **Topic list 확인**

```cpp
/localization/stm_motion
/localization/odometry
/localization/pose
```

2️⃣ **주기 확인**

```cpp
ros2 topic hz /localization/stm_motion
ros2 topic hz /localization/odometry
ros2 topic hz /localization/pose
```

- `stm_motion`, `odometry` 은 약 50Hz
- `pose` 는 약 30Hz가 떠야함

![image.png](image%2045.png)

**3️⃣ pose 확인**

```cpp
ros2 topic echo /localization/pose robot_interfaces/msg/LocalizedRobotPose
```

**4️⃣ TF 확인**

```bash
# TF Chain 1: mission_map → odom
ros2 run tf2_ros tf2_echo mission_map odom

# TF Chain 2: odom → base_link
ros2 run tf2_ros tf2_echo odom base_link

# TF Chain 3: mission_map → base_link
ros2 run tf2_ros tf2_echo mission_map base_link
```

[TF Chain 1 : mission_map → odom]

![image.png](image%2046.png)

```bash
Translation: [5.000, 5.000, 0.000]
Rotation RPY degree: [0.000, -0.000, 0.000]
```

- odom 좌표계의 원점이 mission_map 기준 (5.0, 5.0)에 고정되어 있다.
- odom의 방향은 mission_map과 동일하다.

---

[TF Chain 2 : odom → base_link]

![image.png](image%2047.png)

```bash
Translation: [0.150, 0.150, 0.000]
Rotation RPY degree: [0.000, -0.000, 89.999]
```

- odom 기준에서 현재  base_link가 x_odom = 0.150 m, y_odom =  0.150 m, yaw_odom = 90도 근처에 있다는 의미

---

[TF Chain 3 : mission_map → base_link]

![image.png](image%2048.png)

```bash
Translation: [5.000, 5.000, 0.000]
Rotation RPY degree: [0.000, 0.000, -0.001]
```

---

## 3. 검증 시나리오

시나리오 경로 :

![image.png](image%2049.png)

관제 UI :

![image.png](image%2050.png)

![image.png](image%2051.png)