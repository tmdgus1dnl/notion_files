# `jetson_rx.c` 및 `jetson_tx.c` 상세 분석 (소켓 기반 송수신 파이프라인)

네트워크 인터페이스(UDP) 및 Unix Domain Socket을 다루는 I/O 핵심 모듈입니다.

---

## 1. `jetson_rx.c` (다중 로봇 UDP 수신)
모든 로봇에서 쏟아지는 센서(Odom, Image, LiDAR) 데이터를 9000번 포트 하나에서 몽땅 받아내는 병목 지점입니다.

### 📌 포함된 핵심 헤더 및 API
- `#include <sys/socket.h>`, `#include <arpa/inet.h>`: 소켓 프로그래밍, IP 변환 헤더.
- **`socket(AF_INET, SOCK_DGRAM, 0)`**: UDP 통신 소켓을 생성합니다. (연결 지향이 아닌 데이터그램 기반).
- **`setsockopt(..., SO_RCVBUF, ...)`**: 커널의 UDP 수신 버퍼 크기를 8MB로 확장합니다. 영상/3D LiDAR 조각들이 순간적으로 폭주(Burst)할 때 브릿지 앱이 처리하기도 전에 리눅스 커널이 패킷을 버리는 현상을 막기 위한 필수 최적화입니다.
- **`setsockopt(..., SO_RCVTIMEO, ...)`**: `recvfrom` 함수는 데이터가 올 때까지 무한 대기(Blocking)하는 것이 기본이지만, 100ms 타임아웃을 설정해 주기적으로 깨어나게 만듭니다. 덕분에 프로그램 종료 시(`ctx->stop == 1`) 즉시 루프를 탈출할 수 있습니다.
- **`recvfrom`**: 데이터를 수신하는 동시에 상대방(Jetson)의 IP와 Port(`src_addr`)를 얻어옵니다. 이를 `learn_addr` 함수에 넘겨 **로봇 IP 자동 동적 할당**을 구현했습니다.
- **`atomic_fetch_add` (`<stdatomic.h>`)**: `pkt_count`를 1씩 증가시킵니다. 여러 스레드가 쓰지 않고 오직 Rx 스레드만 쓰지만, Watchdog 메인 스레드가 락 없이 읽기 위해 원자적 연산을 사용합니다.
- **`pthread_rwlock_wrlock` (`handle_odom`)**: 이미지와 달리 Odom은 크기가 작고 항상 덮어쓰므로 큐잉하지 않고 즉시 SHM에 씁니다. Odom 락을 잡고 순식간에 데이터를 업데이트합니다.

---

## 2. `jetson_tx.c` (Qt 커맨드 수신 및 포워딩)
Qt 앱에서 로봇을 조종(이동, 정지 등)하는 명령을 보내면 이를 받아 Jetson으로 쏴주는 역할입니다.

### 📌 포함된 핵심 헤더 및 API
- `#include <sys/un.h>`: Unix Domain Socket 헤더 (로컬 프로세스 간 통신 전용).
- **`socket(AF_UNIX, SOCK_STREAM, 0)`**: TCP처럼 연결 지향적(`SOCK_STREAM`)이지만 외부망이 아닌 동일 머신(RPi5) 내부 메모리를 통해 초고속으로 통신하는 유닉스 소켓을 만듭니다.
- **`unlink(BRIDGE_CMD_SOCK)`**: `/tmp/bridge_cmd.sock` 경로의 소켓 파일이 비정상 종료로 남아있을 경우 `bind`가 실패하므로, 서버를 열기 전에 미리 파일 시스템에서 삭제합니다.
- **`select` (`<sys/select.h>` 역활 포함)**: 클라이언트 접속(`accept`)을 무한 대기하지 않고, 파일 디스크립터 상태를 100ms 간격으로 타임아웃 감시합니다. `stop` 시그널이 오면 안전하게 종료하기 위함입니다.

### 📌 메시지 수신 로직 (Partial Read 방어)
```c
ssize_t n = recv(cli_fd, &cmd, sizeof(cmd), MSG_WAITALL);
if (n != (ssize_t)sizeof(cmd)) continue;
```
- **`MSG_WAITALL`**: 일반 TCP나 스트림 소켓은 구조체 크기만큼 패킷이 한 번에 도착한다는 보장이 없습니다(네트워크 파편화). `MSG_WAITALL` 플래그를 주어 `sizeof(cmd)` 바이트 전체가 도착할 때까지 대기하게 함으로써, 데이터가 중간에 잘린 상태로 해석되는(Garbage value) 치명적 에러를 완벽히 막았습니다.

### 📌 Lock 최소화 전송
- 커맨드를 쏠 때 `send_cmd_to_jetson`을 호출합니다. 이 때 `JetsonAddrTable`을 참조해야 하는데, 주소 테이블의 Lock을 걸고 IP 주소만 스냅샷(복사)한 뒤 즉시 Lock을 해제하고 Lock 밖에서 `sendto`를 날립니다. I/O 지연이 락을 점유하는 것을 막는 교과서적인 멀티스레드 최적화 기법입니다.
