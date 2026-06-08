# Manual Control Protocol Alignment

Date: 2026-05-29

This document records the current Qt manual-control command mapping so the Jetson/robot-side receiver can publish the correct ROS 2 topics.

## Current Status

`proto.h` was not changed for this mapping. The command type constants already exist in:

`/home/pi/robot_project/bride_app_codex_indexed/proto.h`

```c
#define CMD_TYPE_SET_MODE       0x05
#define CMD_TYPE_MANUAL_MOVE    0x0D
#define CMD_TYPE_SET_AUTO       0x0E
#define CMD_TYPE_BODY_ACTION    0x0F
```

The command payload shape is:

```c
typedef struct __attribute__((packed)) {
    uint8_t  cmd_type;
    float    vx;
    float    vy;
    float    omega;
    uint32_t seq;
} CmdPayload;
```

The incoming command packet shape is:

```c
typedef struct __attribute__((packed)) {
    uint8_t  robot_id;
    uint8_t  cmd_type;
    float    vx;
    float    vy;
    float    omega;
    uint32_t seq;
} CmdPacket;
```

## Current Qt Mapping

The Qt app currently maps manual controls as follows.

| UI input | `cmd_type` | `vx` | `vy` | `omega` | Meaning |
|---|---:|---:|---:|---:|---|
| Joystick | `CMD_TYPE_MANUAL_MOVE` `0x0D` | forward/reverse velocity | `0.0` | yaw velocity | Manual velocity control |
| Stand button / 서기 | `CMD_TYPE_SET_MODE` `0x05` | `1.0` | `0.0` | `0.0` | Stand posture |
| Sit button / 앉기 | `CMD_TYPE_SET_MODE` `0x05` | `2.0` | `0.0` | `0.0` | Sit posture |
| Greet button / 인사 | `CMD_TYPE_BODY_ACTION` `0x0F` | `1.0` | `0.0` | `0.0` | Greeting action |
| Return to auto | `CMD_TYPE_SET_AUTO` `0x0E` | `0.0` | `0.0` | `0.0` | Leave manual mode |

Important: the receiver must check `cmd_type` first. Do not interpret `vx` alone. For movement commands, `vx` is velocity. For posture/action commands, the current Qt code reuses `vx` as an action code because the payload has no separate `action_id` field.

## Receiver-Side Rule

The Jetson/robot-side command handler should branch like this:

```cpp
switch (cmd_type) {
case CMD_TYPE_MANUAL_MOVE:
    // vx = forward/reverse velocity
    // vy = currently unused, expected 0.0
    // omega = yaw velocity
    // Publish velocity/manual-control ROS 2 topic.
    break;

case CMD_TYPE_SET_MODE:
    if (static_cast<int>(vx) == 1) {
        // Stand / 서기
        // Publish stand posture ROS 2 topic.
    } else if (static_cast<int>(vx) == 2) {
        // Sit / 앉기
        // Publish sit posture ROS 2 topic.
    }
    break;

case CMD_TYPE_BODY_ACTION:
    if (static_cast<int>(vx) == 1) {
        // Greet / 인사
        // Publish greeting ROS 2 topic.
    }
    break;

case CMD_TYPE_SET_AUTO:
    // Leave manual mode / return to automatic control.
    break;
}
```

## Qt Source Locations

These are the Qt-side source locations that define and send the current mapping.

| File | Why it matters |
|---|---|
| `/home/pi/robot_project/disaster_control_qt/src/mainwindow.cpp` | Defines manual action values and maps UI buttons/joystick to commands. |
| `/home/pi/robot_project/disaster_control_qt/src/shmmonitor.cpp` | `ShmMonitor::sendCommand()` validates robot id and forwards commands to shared memory. |
| `/home/pi/robot_project/disaster_control_qt/src/robotshmconnection.cpp` | `RobotShmConnection::sendCommand()` fills `cmd_type`, `vx`, `vy`, `omega`, `seq` and pushes to the command queue. |
| `/home/pi/robot_project/disaster_control_qt/src/shmtypes.h` | Qt shared-memory type declarations used by the UI monitor. |
| `/home/pi/robot_project/bride_app_codex_indexed/proto.h` | Command constants and packed payload structs that the bridge/robot side must match. |

Key lines in `mainwindow.cpp`:

```cpp
static constexpr int kManualActionStand = 1;
static constexpr int kManualActionSit = 2;
static constexpr int kManualActionGreet = 1;
```

`sendManualVelocity()` sends joystick commands:

```cpp
sendControlCommand(CMD_TYPE_MANUAL_MOVE, vx, vy, omega, QString(), 1, true);
```

`sendManualAction()` sends posture/action commands:

```cpp
// Greet
sendControlCommand(CMD_TYPE_BODY_ACTION,
                   static_cast<float>(actionCode),
                   0.0f,
                   0.0f,
                   ...);

// Stand / sit
sendControlCommand(CMD_TYPE_SET_MODE,
                   static_cast<float>(actionCode),
                   0.0f,
                   0.0f,
                   ...);
```

## Files To Send To The Robot/Jetson Developer

Minimum package:

1. `/home/pi/robot_project/codex_md/2026-05-29_manual_control_protocol_alignment.md`
2. `/home/pi/robot_project/bride_app_codex_indexed/proto.h`
3. `/home/pi/robot_project/disaster_control_qt/src/mainwindow.cpp`

If they need to inspect the full Qt-to-shared-memory send path, also send:

4. `/home/pi/robot_project/disaster_control_qt/src/shmmonitor.cpp`
5. `/home/pi/robot_project/disaster_control_qt/src/shmmonitor.h`
6. `/home/pi/robot_project/disaster_control_qt/src/robotshmconnection.cpp`
7. `/home/pi/robot_project/disaster_control_qt/src/robotshmconnection.h`
8. `/home/pi/robot_project/disaster_control_qt/src/shmtypes.h`

## Recommended Cleanup

The current implementation is compatible if the robot side follows the mapping above.

For a cleaner protocol, define action constants in `proto.h` and use one command type for all body actions:

```c
#define BODY_ACTION_STAND       1
#define BODY_ACTION_SIT         2
#define BODY_ACTION_GREET       3
```

Recommended future mapping:

| UI input | `cmd_type` | `vx` |
|---|---:|---:|
| Stand / 서기 | `CMD_TYPE_BODY_ACTION` `0x0F` | `BODY_ACTION_STAND` `1.0` |
| Sit / 앉기 | `CMD_TYPE_BODY_ACTION` `0x0F` | `BODY_ACTION_SIT` `2.0` |
| Greet / 인사 | `CMD_TYPE_BODY_ACTION` `0x0F` | `BODY_ACTION_GREET` `3.0` |

If this cleanup is adopted, update both Qt and Jetson/robot-side code at the same time.
