# 10. `jetson_tx.c` 수업 자료

## 이 파일의 역할

`jetson_tx.c`는 Qt GUI가 보내는 command를 Unix domain socket으로 받아 Jetson UDP:9001로 전달한다.

## 개념 설명: Local GUI Command Gateway

Qt GUI와 BridgeDaemon은 같은 장비에서 동작한다. 이때 TCP/UDP localhost보다 Unix domain socket이 자연스럽다.

장점:
- filesystem path로 endpoint를 표현할 수 있다.
- local IPC라 overhead가 낮다.
- socket file 권한으로 접근 제어를 확장할 수 있다.

이 파일은 다중 GUI client를 지원하기 위해 `select()`와 client fd 배열을 사용한다.

## include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<arpa/inet.h>` | network type |
| `<errno.h>` | socket error 처리 |
| `<stdio.h>` | log |
| `<stdlib.h>` | 기본 C utility |
| `<string.h>` | `memset`, `strncpy` |
| `<sys/socket.h>` | socket/select/recv |
| `<sys/un.h>` | Unix domain socket address |
| `<unistd.h>` | `close`, `unlink` |
| `"proto.h"` | command packet/type |
| `"shm_def.h"` | SHM type |
| `"bridge_ctx.h"` | `JetsonTxCtx` |
| `"cmd_dispatch.h"` | command dispatch wrapper |
| `"bridge_api.h"` | timeout polling |

## 주요 상수

`GUI_MAX_CLIENTS = 8`

동시에 붙을 수 있는 GUI/debug/test client 수다.

## 함수 설명

### `create_udp_sock(void)`

기능:
- Jetson command 송신용 UDP socket 생성.

반환:
- 성공 시 fd.
- 실패 시 -1.

### `create_unix_server(void)`

기능:
- Unix domain stream socket 생성.
- 기존 `/tmp/bridge_cmd.sock` 파일 삭제.
- socket path bind.
- `listen(fd, GUI_MAX_CLIENTS)` 호출.

개념:
- 이전 daemon이 비정상 종료하면 socket path가 남을 수 있으므로 `unlink()`가 필요하다.

### `handle_gui_packet(JetsonTxCtx *ctx, int cli_fd, int udp_fd)`

기능:
- GUI client에서 `CmdPacket` 하나를 읽는다.
- ESTOP이면 critical priority로 설정.
- high priority 이상이면 ACK 필요 flag 부여.
- `send_cmd_to_jetson()`으로 공통 command API 호출.

파라미터:
- `ctx`: TX context. API pointer 포함.
- `cli_fd`: GUI client fd.
- `udp_fd`: Jetson 송신 UDP fd.

반환:
- 0: client 유지.
- -1: client 종료 또는 오류.

### `jetson_tx_thread(void *arg)`

기능:
- UDP socket과 Unix socket server 생성.
- client fd 배열 초기화.
- `select()`로 server fd와 client fd를 동시에 감시.
- 새 client accept.
- client command 처리.
- command timeout polling.
- 종료 시 client/server/UDP fd close, socket path unlink.

## 수업 포인트

- `listen(fd, 1)`이면 client 하나만 붙는다. 운영 GUI, debug tool, test client를 동시에 쓰려면 다중 client 구조가 필요하다.
- command 송신 정책은 `bridge_api_send_command()`에 있으므로 GUI 경로와 PC 경로가 같은 정책을 공유한다.
- `MSG_WAITALL`은 구조체 크기만큼 받을 때까지 기다리므로 fixed-size command packet에 적합하다.

## 실습 질문

1. GUI command를 UDP localhost로 받지 않고 Unix socket으로 받은 이유는 무엇일까?
2. `select()` 방식 대신 thread-per-client 방식을 쓰면 장단점은 무엇인가?
3. ESTOP command에 priority를 높게 주는 이유는 무엇인가?
