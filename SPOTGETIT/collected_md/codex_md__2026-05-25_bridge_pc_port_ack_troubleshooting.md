# 2026-05-25 Bridge PC Port / ACK Troubleshooting

## Context

RPi bridge, Qt control app, PC, and Jetson-like endpoints were being debugged around command delivery and connection status.

Observed symptoms:

- PC could confirm route generation command `0x08`.
- PC could not confirm move command `0x03`.
- PC address seemed to be learned only when PC sent odom.
- When PC sent only ACK, bridge logs later showed `disconnected`.
- Port roles were confusing because the same numeric ports were being discussed without specifying the device that owns the port.

## Current Port Map

Always read a port together with the target device.

| Direction | Source | Destination | Destination port | Meaning |
|---|---|---:|---:|---|
| Jetson or PC data -> RPi | Jetson/PC | RPi bridge | `9000` | Bridge RX path for odom, path, image/lidar, ACK, etc. |
| RPi -> Jetson command | RPi bridge | Jetson | `9001` | Command TX path, `PktHeader + CmdPayload` |
| RPi -> PC command | RPi bridge | PC | `9001` | PC currently expects command packets here |
| PC <-> RPi auxiliary link | PC/RPi | peer | `9002` | Legacy/separate PC link for raw `CmdPacket`/status |

Important distinction:

- `9000` is the port on RPi where bridge receives packets.
- `9001` is the command receive port on the peer endpoint, either Jetson or PC.
- `9002` exists in code as a separate PC link, but it is not the active command path for current move/route operation.

## Root Cause: Route Worked, Move Did Not

Route generation and move were using different paths.

Route generation:

- Qt sent `CMD_TYPE_SET_ROUTE` (`0x08`) through the normal command path.
- Display robot id was mapped through `displayToPhysicalRobotId()`.
- Bridge sent a framed command packet, `PktHeader + CmdPayload`, to peer UDP `9001`.
- PC confirmed this path.

Move before the fix:

- S01-S04 move commands were treated as PC-target commands with `CMD_FLAG_TARGET_PC`.
- Bridge sent raw `CmdPacket` to PC UDP `9002`.
- PC was watching/expecting the `9001` framed command path, so move was not confirmed.

Fix:

- Removed the Qt-side PC-target special case for normal commands.
- Move now follows the same path as route generation:
  - display id -> physical id mapping
  - normal SHM command queue
  - bridge sends `PktHeader + CmdPayload`
  - destination is peer UDP `9001`

Changed file:

- `/home/pi/robot_project/disaster_control_qt/src/mainwindow.cpp`

Verification:

- `cmake --build build_codex` passed.
- A zero-speed test move was pushed to `robot_bridge_4`.
- Bridge log showed:
  - `jetson_tx robot=4 cmd=move seq=900103 priority=3 ack=0`
- `tcpdump` confirmed UDP packets leaving:
  - `192.168.0.13:<ephemeral> > 192.168.0.3.9001: UDP, length 41`

## Why PC Connects With Odom But ACK Alone Goes Disconnected

Bridge connection status is watchdog-driven.

The watchdog checks each robot's `meta.pkt_count` once per second. If the count does not increase for 3 seconds, it marks that robot disconnected.

Relevant behavior:

- `bridge_api_note_rx()` sets `jetson_connected = 1` and increments `pkt_count`.
- `watchdog_loop()` marks disconnected when `pkt_count` stops increasing for 3 seconds.
- ACK is a command response, not a heartbeat or continuous state stream.

Therefore:

- PC sends odom periodically -> `pkt_count` keeps increasing -> connected stays true.
- PC sends one ACK -> `pkt_count` may increase once -> no more packets -> watchdog later marks disconnected.

This does not necessarily mean ACK failed. It means the bridge did not receive continuous packets for that `robot_id`.

## PC Peer Learning Detail

Current PC peer learning from `jetson_rx.c` recognizes these packet types:

- `PKT_TYPE_ODOM`
- `PKT_TYPE_GLOBAL_PATH`
- `PKT_TYPE_PATH_PROGRESS`
- `PKT_TYPE_EVENT`
- `PKT_TYPE_IMAGE`
- `PKT_TYPE_LIDAR`

`PKT_TYPE_CMD_ACK` is not included in `packet_type_learns_pc_peer()`.

So ACK alone does not establish/refresh the PC peer address. Odom or another supported data packet does.

## Bridge Process State Notes

During debugging, duplicate bridge instances caused confusion:

- Multiple `bridge_daemon` processes can bind the same UDP ports due to `SO_REUSEADDR`.
- That can split traffic: one process receives PC/Jetson packets while another process consumes Qt SHM commands.

Operational rule:

- Keep exactly one `bridge_daemon` running.
- If behavior is inconsistent, stop all bridge processes first:
  - `sudo pkill -x bridge_daemon`
- Then start one bridge instance in 5-robot mode:
  - `cd /home/pi/robot_project/bride_app_codex_indexed`
  - `nohup sudo ./bridge_daemon 5 > /tmp/bridge_daemon.log 2>&1 &`

At the end of this troubleshooting, bridge was explicitly stopped at the user's request.

## Practical Interpretation

For current operation:

- PC/Jetson should send data to `RPi:9000`.
- RPi sends commands to the peer's `9001`.
- PC should keep sending odom/path/health-like packets periodically if it wants bridge/Qt to show connected.
- ACK should be treated as command acknowledgement only, not as connection keepalive.

Potential cleanup:

- Rename or disable the legacy `9002` PC link if it is no longer used, to reduce confusion.
- Alternatively, define a clear `HEALTH` or heartbeat packet from PC to RPi and let watchdog use that for PC-side connection status.
