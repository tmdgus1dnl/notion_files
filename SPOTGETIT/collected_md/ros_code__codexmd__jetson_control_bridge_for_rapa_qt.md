# Jetson Control Bridge Notes for Rapa Qt Frontend

작성일: 2026-05-29
대상: 라파(Rapa) Qt 프론트/네트워크 브릿지 앱 개발자
Jetson workspace: `/home/jetson/robot_ws`

## 1. 현재 Jetson 쪽 결론

Jetson 쪽에는 이미 UDP command packet을 받는 `cmd_receiver`가 있다.

현재 구현은 UDP packet payload에서 아래 값을 unpack한다.

```text
cmd_type: uint8
vx: float32
vy: float32
omega: float32
seq: uint32
```

하지만 현재 코드는 `cmd_type == MOVE(0x03)`일 때 속도값은 사용하지 않고, ROS2 topic `/control/behavior/mode`에 `CLASSIC`만 publish한다.

즉 현재 상태는 다음과 같다.

```text
Rapa/Bridge UDP packet
  -> Jetson cmd_receiver
  -> cmd_type, vx, vy, omega, seq unpack
  -> if MOVE: publish "CLASSIC" to /control/behavior/mode
  -> vx/vy/omega are discarded in current code
```

실제 조종까지 하려면 Jetson 쪽에서 unpack한 `vx/vy/omega`를 `geometry_msgs/msg/Twist`로 `/control/cmd_vel/spot_01`에 publish하는 경로가 추가되어야 한다.

필요한 최종 흐름은 다음과 같다.

```text
Rapa Qt frontend
  -> Rapa network bridge app
  -> UDP command packet to Jetson:9001
  -> Jetson cmd_receiver
  -> /control/behavior/mode = "CLASSIC"
  -> /control/cmd_vel/spot_01 = Twist(linear.x=vx, linear.y=vy, angular.z=omega)
  -> classic_control_node
  -> /control/classic_control/joint_target
  -> joint_target_mux_node
  -> /control/selected/joint_target
  -> actuator_bridge_node
  -> STM
```

## 2. UDP destination

Jetson receiver listens here:

```text
Jetson UDP bind: 0.0.0.0:9001
Expected destination from Rapa bridge: <Jetson IP>:9001
```

Relevant constants in Jetson code:

```python
BRIDGE_IP       = "192.168.0.13"
ROBOT_IDS       = {0}
BRIDGE_PORT     = 9000
JETSON_CMD_PORT = 9001
```

`BRIDGE_IP` is the remote bridge address used by Jetson for announce/ack. For Rapa-side sending, the important target is Jetson IP port `9001`.

## 3. Packet format currently decoded by Jetson

Jetson Python code uses little-endian `struct` formats.

Header:

```python
PKT_HEADER_STRUCT = struct.Struct("<BBHHHIIQ")
```

Payload:

```python
CMD_PAYLOAD_STRUCT = struct.Struct("<BfffI")
```

### 3.1 Header layout

Total header size is 24 bytes.

| Offset | Type | Field | Current meaning |
|---:|---|---|---|
| 0 | uint8 | packet_type | command packet must be `0x04` |
| 1 | uint8 | robot_id | accepted robot id is `0` |
| 2 | uint16 | frag_idx | normally `0` |
| 4 | uint16 | frag_total | normally `1` |
| 6 | uint16 | payload_len | command payload size, currently `17` |
| 8 | uint32 | frame_id | sender-side increasing id recommended |
| 12 | uint32 | payload_offset | normally `0` |
| 16 | uint64 | timestamp_us | monotonic or system timestamp in microseconds |

### 3.2 Command payload layout

Total command payload size is 17 bytes.

| Offset | Type | Field | Meaning |
|---:|---|---|---|
| 0 | uint8 | cmd_type | command kind |
| 1 | float32 | vx | forward/backward velocity, m/s |
| 5 | float32 | vy | lateral velocity, m/s |
| 9 | float32 | omega | yaw angular velocity, rad/s |
| 13 | uint32 | seq | command sequence number |

All numeric fields are little-endian.

### 3.3 Command types known in Jetson code

```text
CMD_TYPE_STOP          = 0x02
CMD_TYPE_MOVE          = 0x03
CMD_TYPE_HEARTBEAT     = 0x04
CMD_TYPE_START_MISSION = 0x09
```

Current behavior:

| cmd_type | Current Jetson behavior |
|---:|---|
| `0x03 MOVE` | publish `CLASSIC` to `/control/behavior/mode`; currently does not publish speed |
| `0x04 HEARTBEAT` | ignored/pass |
| others | logs as unregistered command type |

## 4. Recommended Rapa-side sending behavior

Rapa network bridge should send `MOVE` command packets periodically while the operator is actively controlling.

Recommended rate:

```text
10-20 Hz for normal UI joystick control
```

Recommended payload mapping:

```text
vx    = forward/backward command in m/s
vy    = lateral command in m/s
omega = yaw command in rad/s
seq   = monotonically increasing uint32
```

Recommended initial safe limits for CLASSIC mode:

```text
vx:    -0.05 to 0.20 m/s
vy:    -0.05 to 0.05 m/s
omega: -0.35 to 0.35 rad/s
```

These match current `classic_control` config. For first UI testing, use lower limits:

```text
vx:    -0.03 to 0.08 m/s
vy:    -0.02 to 0.02 m/s
omega: -0.10 to 0.10 rad/s
```

Recommended deadman behavior:

- Send `MOVE` packets continuously while a control button/deadman is held.
- If UI joystick returns to center, keep sending `vx=0, vy=0, omega=0` for a short time.
- If app loses focus, network disconnects, or deadman is released, send zero command several times.
- Add Jetson-side timeout when implementing speed publish so command loss results in zero Twist.

## 5. Control semantics on Jetson

The final ROS velocity topic is:

```text
/control/cmd_vel/spot_01
geometry_msgs/msg/Twist
```

Field mapping:

```text
Twist.linear.x  = vx
Twist.linear.y  = vy
Twist.angular.z = omega
```

`classic_control_node` uses all three fields for gait generation.

`actuator_bridge_node` also subscribes to the same topic and classifies the command into STM motion state:

| Twist condition | STM motion state |
|---|---|
| all zero | STOP |
| `vx > 0`, `omega == 0` | WALK_FORWARD |
| `vx < 0`, `omega == 0` | WALK_BACKWARD |
| `omega > 0`, `vx == 0` | TURN_LEFT |
| `omega < 0`, `vx == 0` | TURN_RIGHT |
| `vx != 0`, `omega > 0` | WALK_FORWARD_TURN_LEFT |
| `vx != 0`, `omega < 0` | WALK_FORWARD_TURN_RIGHT |
| `vy > 0` fallback | STRAFE_LEFT |
| `vy < 0` fallback | STRAFE_RIGHT |

Caution: current classifier prioritizes `vx` over `vy` if `vx != 0` and `omega == 0`. For pure strafe, send `vx=0`, `omega=0`, nonzero `vy`.

## 6. Important current limitation

Do not assume that sending nonzero `vx/vy/omega` currently moves the robot.

As of the inspected code, `cmd_receiver` receives and unpacks the values, but does not publish `/control/cmd_vel/spot_01`.

Therefore integration has two phases:

1. Rapa bridge sends the existing packet format correctly.
2. Jetson `cmd_receiver` is updated later to publish `Twist` from `vx/vy/omega`.

No Jetson code was changed while writing this document.

## 7. Suggested Rapa Qt UI controls

Minimum usable UI:

- Connect/disconnect state for Jetson UDP target.
- Deadman/enable button.
- Virtual joystick or directional buttons.
- Speed scale selector: low/medium.
- Stop button that sends zero velocity repeatedly.
- Optional mode buttons: CLASSIC, STAND, SIT, E-STOP if corresponding Jetson handling is added.

Recommended joystick mapping:

```text
vertical axis   -> vx
horizontal axis -> omega
optional strafe modifier + horizontal axis -> vy
```

For first test, avoid mixing lateral and yaw at the same time. Start with only `vx` and `omega`.

## 8. Minimal packet construction pseudocode

C-like pseudocode for Rapa bridge:

```c
// little-endian layout required
packet_type = 0x04;  // PKT_TYPE_CMD
robot_id = 0;
frag_idx = 0;
frag_total = 1;
payload_len = 17;
frame_id++;
payload_offset = 0;
timestamp_us = now_us();

cmd_type = 0x03;  // CMD_TYPE_MOVE
vx = limited_vx_mps;
vy = limited_vy_mps;
omega = limited_omega_radps;
seq++;

send header("<BBHHHIIQ") + payload("<BfffI") to Jetson:9001;
```

Python equivalent for reference:

```python
import struct, time

PKT_HEADER_STRUCT = struct.Struct("<BBHHHIIQ")
CMD_PAYLOAD_STRUCT = struct.Struct("<BfffI")

PKT_TYPE_CMD = 0x04
CMD_TYPE_MOVE = 0x03
ROBOT_ID = 0

seq = 1
frame_id = 1
vx = 0.05
vy = 0.0
omega = 0.0
ts_us = time.monotonic_ns() // 1000

payload = CMD_PAYLOAD_STRUCT.pack(CMD_TYPE_MOVE, vx, vy, omega, seq)
header = PKT_HEADER_STRUCT.pack(
    PKT_TYPE_CMD,
    ROBOT_ID,
    0,
    1,
    len(payload),
    frame_id,
    0,
    ts_us,
)
packet = header + payload
```

## 9. Source files to share with Rapa-side developer

Most important Jetson files:

1. `/home/jetson/robot_ws/src/network/cmd_receiver/cmd_receiver/cmd_receiver_node.py`
   - Current UDP receiver.
   - Defines packet constants, header/payload struct formats, command types, robot id, and port.
   - Important lines: constants near top, `_udp_loop`, `_unpack_cmd`.

2. `/home/jetson/robot_ws/src/control/classic_control/classic_control/classic_control_node.py`
   - Consumes `/control/cmd_vel/spot_01` and generates classic gait joint targets.
   - Shows velocity clamp/deadband/timeout behavior.

3. `/home/jetson/robot_ws/src/control/classic_control/config/classic_control.yaml`
   - Current CLASSIC speed limits and deadbands.

4. `/home/jetson/robot_ws/src/control/actuator_bridge/src/actuator_bridge_node.cpp`
   - Subscribes to `/control/cmd_vel/spot_01`.
   - Converts Twist into STM `motion_state`.
   - Sends selected joint target and motion state to STM.

5. `/home/jetson/robot_ws/src/control/motion_manager/src/joint_target_mux_node.cpp`
   - Consumes `/control/behavior/mode`.
   - Allowed mode strings: `RL`, `CLASSIC`, `STAND`, `SIT`, `DETECT`.
   - Selects which joint target source reaches the actuator bridge.

6. `/home/jetson/robot_ws/src/interfaces/robot_interfaces/msg/StmMotion.msg`
   - Defines motion state enum values.

7. `/home/jetson/robot_ws/src/interfaces/robot_interfaces/msg/JointTarget.msg`
   - Defines control mode enum values.

8. `/home/jetson/robot_ws/src/control/classic_control/classic_control/classic_one_cycle_test_node.py`
   - Good reference for how Jetson-side test code publishes `CLASSIC` mode and `Twist` together.

## 10. Practical integration checklist

Rapa side:

- Build little-endian packet exactly as `<BBHHHIIQ` + `<BfffI`.
- Send to Jetson IP, UDP port `9001`.
- Use `packet_type=0x04`, `robot_id=0`, `cmd_type=0x03` for MOVE.
- Send at 10-20 Hz while control is active.
- Clamp speed before sending.
- Send zero velocity on stop/deadman release/disconnect.

Jetson side, future change needed:

- Add a `Twist` publisher to `cmd_receiver` for `/control/cmd_vel/spot_01`.
- On `MOVE`, publish `CLASSIC` and publish `Twist(vx, vy, omega)`.
- On STOP or command timeout, publish zero Twist and optionally switch to STAND/SIT depending on desired behavior.
- Keep current packet format unless a new explicit mode/stop/e-stop protocol is designed.
