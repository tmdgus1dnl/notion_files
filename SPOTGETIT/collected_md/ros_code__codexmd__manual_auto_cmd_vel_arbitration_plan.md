# Manual Control and Autonomous cmd_vel Arbitration Plan

작성일: 2026-05-29
대상: Jetson ROS2 제어부, Rapa Qt 프론트/네트워크 브릿지 연동
Jetson workspace: `/home/jetson/robot_ws`

## 1. 현재 확인된 사실

현재 Jetson 쪽 수동/자동 제어에서 핵심 최종 속도 토픽은 다음이다.

```text
/control/cmd_vel/spot_01
geometry_msgs/msg/Twist
```

`classic_control_node`와 `actuator_bridge_node`가 이 토픽을 기준으로 동작한다.

현재 `cmd_receiver`는 UDP packet payload에서 아래 값을 unpack한다.

```text
cmd_type, vx, vy, omega, seq
```

하지만 현재 구현은 `cmd_type == MOVE(0x03)`일 때 `/control/behavior/mode`에 `CLASSIC`만 publish하고, `vx/vy/omega`는 아직 `/control/cmd_vel/spot_01`로 publish하지 않는다.

따라서 현재 상태 그대로는 Rapa 쪽에서 속도값을 보내도 로봇 이동까지 연결되지 않는다. Jetson 쪽에 `vx/vy/omega -> Twist` publish 경로가 추가되어야 한다.

## 2. 횡 이동 가능 여부

횡 이동 개념은 코드에 있다.

`classic_control_node`는 `Twist.linear.y`를 `vy`로 읽고, config에도 lateral 제한값이 있다.

```text
vy_min_mps: -0.05
vy_max_mps:  0.05
lateral_deadband_mps: 0.01
```

`actuator_bridge_node`도 `vy`만 nonzero일 때 STM motion state를 `STRAFE_LEFT` 또는 `STRAFE_RIGHT`로 분류한다.

다만 주의할 점이 있다.

현재 `actuator_bridge_node`의 motion state 분류는 `vx`와 `omega`를 우선한다. 따라서 순수 횡 이동을 보내려면 다음처럼 보내는 것이 좋다.

```text
left strafe:  vx=0, vy>0, omega=0
right strafe: vx=0, vy<0, omega=0
```

`vx`와 `vy`를 동시에 섞는 대각선 횡 이동은 classic gait 계산에는 들어가지만, STM motion state 분류에서는 순수 strafe로 보지 않을 수 있다. 첫 구현은 전후진 + yaw 회전 위주로 만들고, 횡 이동은 별도 버튼/모드로 분리하는 쪽이 안전하다.

## 3. 조이스틱/8방향 제어 해석

수동제어 UI는 결국 아래 세 값을 계속 보내면 된다.

```text
vx    -> 전진/후진 선속도, m/s
vy    -> 좌/우 횡이동 선속도, m/s
omega -> 좌/우 yaw 각속도, rad/s
```

예시 매핑:

| UI 입력 | vx | vy | omega |
|---|---:|---:|---:|
| 정지 | 0 | 0 | 0 |
| 전진 | + | 0 | 0 |
| 후진 | - | 0 | 0 |
| 제자리 좌회전 | 0 | 0 | + |
| 제자리 우회전 | 0 | 0 | - |
| 전진 좌회전 | + | 0 | + |
| 전진 우회전 | + | 0 | - |
| 좌 횡이동 | 0 | + | 0 |
| 우 횡이동 | 0 | - | 0 |

조이스틱을 얼마나 밀었는지는 속도 크기로 반영하면 된다.

예시:

```text
살짝 전진: vx=0.03
강한 전진: vx=0.08 또는 vx=0.10
살짝 좌회전: omega=0.05
강한 좌회전: omega=0.15
```

초기 테스트 권장 제한값:

```text
vx:    -0.03 ~ 0.08 m/s
vy:    -0.02 ~ 0.02 m/s
omega: -0.10 ~ 0.10 rad/s
```

현재 CLASSIC 설정상 최대 제한값:

```text
vx:    -0.05 ~ 0.20 m/s
vy:    -0.05 ~ 0.05 m/s
omega: -0.35 ~ 0.35 rad/s
```

## 4. 자동제어와 수동제어 cmd_vel 충돌 문제

사용자 우려가 맞다. 자동제어 노드와 수동제어 노드가 동시에 `/control/cmd_vel/spot_01`에 publish하면 안 된다.

현재 자동제어 경로는 다음과 같다.

```text
path_follower_node
  -> /control/cmd_vel_raw/spot_01
  -> safety_supervisor_node
  -> /control/cmd_vel/spot_01
```

즉 현재 `safety_supervisor_node`가 최종 `/control/cmd_vel/spot_01`를 publish한다.

수동제어를 `cmd_receiver`에서 바로 `/control/cmd_vel/spot_01`로 publish하면, 자동제어의 `safety_supervisor_node`와 수동제어의 `cmd_receiver`가 같은 토픽을 동시에 publish하게 된다. ROS2에서는 같은 토픽에 여러 publisher가 있을 수 있지만, 제어 명령에서는 마지막으로 도착한 메시지에 따라 로봇이 흔들리거나 의도치 않은 방향으로 움직일 수 있다.

따라서 최종 cmd_vel에는 publisher가 하나만 있어야 한다.

## 5. 권장 구조: cmd_vel mux / control authority

권장 구조는 `cmd_vel` 중재 노드를 하나 두는 것이다.

```text
자동제어 경로:
path_follower_node
  -> /control/cmd_vel_raw/spot_01
  -> safety_supervisor_node
  -> /control/cmd_vel_auto/spot_01

수동제어 경로:
cmd_receiver
  -> /control/cmd_vel_manual/spot_01

중재 경로:
cmd_vel_mux_node
  -> /control/cmd_vel/spot_01
```

`classic_control_node`와 `actuator_bridge_node`는 계속 최종 `/control/cmd_vel/spot_01`만 보면 된다.

중요한 원칙:

```text
/control/cmd_vel/spot_01 publisher는 cmd_vel_mux_node 하나만 둔다.
```

## 6. 수동/자동 선택 기준

중재 노드는 아래 중 하나를 기준으로 source를 선택하면 된다.

### Option A: `/control/behavior/active_mode` 기준

현재 `joint_target_mux_node`는 `/control/behavior/mode`를 받아 active mode를 publish한다.

허용 모드:

```text
RL, CLASSIC, STAND, SIT, DETECT
```

수동 조종은 `CLASSIC` 모드로 보고, `active_mode == CLASSIC`일 때 manual cmd_vel을 선택할 수 있다.

장점:

- 기존 mode 구조와 잘 맞는다.
- Rapa가 이미 `MOVE`로 CLASSIC 전환을 유도하고 있다.

주의:

- 자동주행도 classic controller를 사용할 수 있다면 `CLASSIC == manual`로 단순 판정하면 부족할 수 있다.
- 이 경우 별도 authority topic이 더 명확하다.

### Option B: 별도 authority topic 추가

예시:

```text
/control/cmd_vel/source = "AUTO" | "MANUAL"
/control/manual/enabled = true | false
```

권장 우선순위:

```text
E_STOP > MANUAL(deadman active) > AUTO > ZERO
```

장점:

- 자동주행과 수동주행이 둘 다 CLASSIC gait를 써도 구분 가능하다.
- UI deadman 상태와 직접 연결하기 쉽다.

권장 방향은 Option B다. 다만 첫 구현은 `/control/behavior/mode`와 manual deadman을 같이 보는 방식으로 시작할 수 있다.

## 7. 수동 우선권과 deadman

수동제어는 자동제어보다 우선순위가 높아야 한다.

권장 규칙:

1. Rapa UI에서 deadman/enable이 눌려 있고 MOVE packet이 일정 주기 안에 들어오면 MANUAL active.
2. MANUAL active 중에는 자동제어 cmd_vel을 무시한다.
3. MOVE packet이 timeout되면 즉시 zero Twist를 publish한다.
4. deadman이 release되면 zero Twist를 몇 번 보내고 MANUAL inactive로 전환한다.
5. 수동이 끝난 뒤 자동 복귀는 즉시 하지 말고, 명시적인 AUTO 전환 버튼이나 mission resume 명령으로만 복귀한다.

권장 timeout:

```text
manual command timeout: 0.2 ~ 0.5 sec
send rate from Rapa: 10 ~ 20 Hz
```

## 8. 구현 단계 제안

### Step 1. Jetson cmd_receiver 확장

현재 `cmd_receiver`는 `MOVE` 수신 시 `CLASSIC`만 publish한다.

향후 변경 방향:

```text
on MOVE:
  publish /control/behavior/mode = "CLASSIC"
  publish /control/cmd_vel_manual/spot_01 = Twist(vx, vy, omega)
  update last_manual_command_time

on STOP or timeout:
  publish /control/cmd_vel_manual/spot_01 = zero Twist
```

중요: 바로 `/control/cmd_vel/spot_01`로 publish하지 말고, manual 전용 topic으로 publish하는 것이 좋다.

### Step 2. safety_supervisor output remap

현재 `safety_supervisor_node`는 `/control/cmd_vel/spot_01`로 publish한다.

수동/자동 중재 구조에서는 이 출력을 다음으로 바꾸는 것이 좋다.

```text
/control/cmd_vel_auto/spot_01
```

이렇게 해야 자동제어와 수동제어가 최종 topic에서 충돌하지 않는다.

### Step 3. cmd_vel_mux_node 추가

새 중재 노드가 아래를 구독한다.

```text
/control/cmd_vel_auto/spot_01
/control/cmd_vel_manual/spot_01
/control/manual/enabled 또는 /control/cmd_vel/source
```

그리고 아래 하나만 publish한다.

```text
/control/cmd_vel/spot_01
```

선택 로직 예시:

```text
if e_stop:
  publish zero
elif manual_enabled and manual_cmd_fresh:
  publish manual_cmd
elif auto_enabled and auto_cmd_fresh:
  publish auto_cmd
else:
  publish zero
```

### Step 4. Rapa UI/bridge 구현

Rapa 쪽은 기존 packet format을 유지하면서 `vx/vy/omega`를 채워 보내면 된다.

권장 송신:

```text
UDP target: Jetson IP:9001
packet_type: 0x04
robot_id: 0
cmd_type: 0x0D MANUAL_MOVE
payload: vx, vy, omega, seq
rate: 10~20 Hz
```

UI 버튼:

- Manual enable/deadman
- Stop
- Auto resume: cmd_type 0x0E SET_AUTO
- Speed scale low/medium
- Optional strafe mode

## 9. 왜 cmd_vel_mux가 필요한가

단순히 `cmd_receiver`에서 `/control/cmd_vel/spot_01`로 직접 publish하면 빠르게 테스트는 가능하다.

하지만 자동주행이 켜져 있는 순간 다음 문제가 생긴다.

```text
safety_supervisor_node -> /control/cmd_vel/spot_01
cmd_receiver           -> /control/cmd_vel/spot_01
```

두 publisher가 같은 토픽에 명령을 내면 제어권이 불명확하다.

특히 자동주행이 계속 전진을 publish하고 있는데 수동 UI에서 정지를 보내면, 다음 자동 메시지가 다시 전진을 덮어쓸 수 있다. 반대로 수동 메시지가 자동 명령을 덮어써서 navigation 상태와 실제 로봇 움직임이 어긋날 수 있다.

따라서 최종 actuator/control 계층으로 들어가는 `/control/cmd_vel/spot_01`는 반드시 한 노드가 책임지는 구조가 좋다.

## 10. 라파 쪽에 전달할 핵심 요약

라파 Qt/bridge 개발자는 아래만 지키면 된다.

1. 조이스틱 값을 `vx/vy/omega`로 변환한다.
2. UDP packet format은 현재 Jetson `cmd_receiver`가 decode하는 little-endian 구조를 따른다.
3. MANUAL_MOVE packet을 10~20Hz로 계속 보낸다.
4. deadman release, stop, disconnect 시 zero velocity를 여러 번 보낸다.
5. 처음에는 `vx + omega` 중심으로 구현하고, strafe는 별도 버튼/모드로 `vy`만 보내게 한다.
6. Jetson 쪽에서는 수동 cmd_vel과 자동 cmd_vel을 mux로 중재해야 한다.

## 11. 관련 소스 파일

- `/home/jetson/robot_ws/src/network/cmd_receiver/cmd_receiver/cmd_receiver_node.py`
  - 현재 UDP packet 수신, unpack, `CLASSIC` publish.

- `/home/jetson/robot_ws/src/control/classic_control/classic_control/classic_control_node.py`
  - `/control/cmd_vel/spot_01`를 받아 `vx/vy/wz`를 classic gait target으로 변환.

- `/home/jetson/robot_ws/src/control/classic_control/config/classic_control.yaml`
  - CLASSIC 속도 제한, deadband, timeout.

- `/home/jetson/robot_ws/src/control/actuator_bridge/src/actuator_bridge_node.cpp`
  - `/control/cmd_vel/spot_01`를 STM motion state로 분류.

- `/home/jetson/robot_ws/src/navigation/spot_navigation/src/path_follower_node.cpp`
  - 자동제어 raw cmd_vel publisher.

- `/home/jetson/robot_ws/src/navigation/spot_navigation/src/safety_supervisor_node.cpp`
  - 현재 자동제어 final `/control/cmd_vel/spot_01` publisher. mux 구조에서는 output remap 대상.

- `/home/jetson/robot_ws/src/control/motion_manager/src/joint_target_mux_node.cpp`
  - behavior mode 관리 및 selected joint target mux.

## 12. 최종 권장 방향

최종 권장 구조는 다음이다.

```text
Rapa manual UDP
  -> cmd_receiver
  -> /control/cmd_vel_manual/spot_01

Navigation auto
  -> safety_supervisor_node
  -> /control/cmd_vel_auto/spot_01

cmd_vel_mux_node
  -> /control/cmd_vel/spot_01

classic_control_node + actuator_bridge_node
  -> consume /control/cmd_vel/spot_01
```

이 구조가 수동/자동 제어권을 명확하게 분리하고, 수동 개입 시 자동제어 명령이 로봇을 다시 움직이는 문제를 막는다.
