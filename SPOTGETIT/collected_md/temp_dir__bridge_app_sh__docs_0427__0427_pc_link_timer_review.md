# `protocol_timer.c` 및 `pc_link.c` 상세 분석 (보조 네트워크 및 타이머 파이프라인)

로봇 연결 유지(Heartbeat)와 원격 외부망(PC 관제)과의 연동을 처리하는 스레드입니다.

---

## 1. `protocol_timer.c` (Heartbeat 알람 스레드)
Jetson이 RPi5의 통신 두절을 감지하여 비상 제동을 걸 수 있도록 1초(1Hz) 주기로 생존 신호를 날려주는 역할을 합니다.

### 📌 포함된 핵심 헤더 및 API
- `#include <time.h>`: 정밀 타이머 및 시간 계산.
- **`clock_gettime(CLOCK_MONOTONIC, &now)`**: 시스템 시간이 외부 동기화(NTP)나 관리자 명령으로 뒤틀리더라도 영향을 받지 않고 오직 부팅 후 흐른 '단조 시간(Monotonic)'만을 정밀하게 재는 함수입니다. 주기가 생명인 제어계에서 절대적으로 필요합니다.
- **`nanosleep`**: 지정된 나노초만큼 스레드를 슬립 상태로 만들어 CPU 점유율을 0%로 만듭니다.

### 📌 스마트 슬립 루프 (Smart Sleep)
```c
struct timespec sleep_t = { .tv_sec = 0, .tv_nsec = 100000000L }; // 100ms
nanosleep(&sleep_t, NULL);
```
- 단순히 1초를 `sleep(1)` 해버리면 사용자가 Ctrl+C를 눌러도 최장 1초 동안 프로그램이 뻗은 채로 기다리게 됩니다.
- 1초라는 타겟 시간을 정해두고, **100ms 단위로 토막 내어 잠을 잡니다.** 깨어날 때마다 `if (ctx->stop) break;` 를 확인하여 프로그램 종료 속도(반응성)를 극대화했습니다.

### 📌 스냅샷 기반 송신
```c
struct sockaddr_in snap_addr[MAX_ROBOTS];
pthread_mutex_lock(&ctx->addr_table->mu);
// ... 스냅샷 복사
pthread_mutex_unlock(&ctx->addr_table->mu);
```
- 네트워크 전송 API(`sendto`)는 커널 상태나 라우터 상태에 따라 지연이 발생할 수 있습니다.
- 뮤텍스(Mutex) 락을 쥔 상태에서 `sendto`를 호출하면 송신 지연이 발생할 때 다른 스레드(Rx, Tx)가 모두 멈추게 됩니다. 이를 막기 위해 주소값만 구조체 배열로 재빨리 복사(Snapshot)하고 락을 푼 뒤 안전하게 루프를 돌며 패킷을 쏩니다.

---

## 2. `pc_link.c` (원격 PC 관제 중계기)
같은 망 또는 VPN 등으로 연결된 원격지 PC 앱과 통신하여 로봇의 현재 상태를 알려주고 PC의 명령을 받는 역할입니다.

### 📌 핵심 API 및 로직
- UDP Port 9002를 개방하여 `recvfrom` 대기.
- **동적 IP 학습 (PC 자동 등록)**:
  - Jetson RX와 마찬가지로, PC 측 IP 주소를 하드코딩하지 않습니다. PC 앱에서 최초 커맨드 패킷을 날리면 `recvfrom`에서 잡힌 IP 구조체(`src`)를 `pc_addr`에 저장하고 `pc_addr_set = 1`로 상태를 전환합니다. 이후부터 이 주소로 상태 패킷을 쏩니다.
- **주기적 데이터 취합 및 전송 (`send_status_all`)**:
  - 매 루프마다 `now_us()`를 통해 이전 상태 패킷 송신 시간(`last_status_us`) 대비 1000ms(1초)가 지났는지 체크합니다.
  - 시간이 지났으면, 공유 메모리(SHM)의 곳곳에서 데이터를 빼옵니다:
    - `atomic_load`로 현재 Jetson 연결 상태 조회
    - `pthread_rwlock_rdlock`으로 최신 Odom(x, y, theta, seq) 복사
    - 메타 데이터(이미지/라이다 패킷 손실 카운트) 복사
  - 취합된 데이터를 `PcStatusPacket` (C 구조체)에 포장하여 `sendto`로 쏩니다.
- **포워딩 (Forwarding)**: PC에서 수신한 커맨드 패킷은 본인이 처리하지 않고, 공통 함수인 `send_cmd_to_jetson`을 호출하여 Jetson 로봇으로 곧장 토스합니다.
