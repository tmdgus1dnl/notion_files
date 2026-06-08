# Bridge Application (`bridge_app_sh`) 소스 코드 분석 보고서

본 문서는 `bridge_app_sh` 디렉토리 내의 모든 `.c` 및 `.h` 소스 코드 파일을 분석한 결과를 정리한 문서입니다. `bridge_app_sh`는 다중 로봇(Jetson)과 Qt 응용 프로그램, 원격 PC 사이에서 데이터를 중계하고 변환하는 핵심 데몬(BridgeDaemon)입니다.

---

## 1. 시스템 아키텍처 및 스레드 모델

BridgeDaemon은 멀티스레드 기반으로 동작하며, 여러 로봇의 트래픽을 동시에 처리하기 위해 각 스레드별로 역할을 명확하게 분담하고 있습니다. 로봇 N대를 기준으로 다음과 같은 스레드 구성을 가집니다.

- **Main / Watchdog 스레드 (1개)**: 시스템 초기화 및 각 로봇별 연결 상태 감시.
- **Jetson RX 스레드 (1개)**: 코어 2에 고정되어 모든 로봇으로부터 오는 UDP 패킷을 수신. Odom 데이터는 바로 처리하고 분할된 이미지/LiDAR 패킷은 큐에 삽입.
- **Reassembly SHM 스레드 (N개)**: 로봇당 1개씩 할당. Fragment Queue에서 패킷 조각을 꺼내 완전한 프레임으로 재조립 후 Shared Memory(SHM)에 기록.
- **Jetson TX 스레드 (1개)**: Qt 앱에서 유닉스 도메인 소켓을 통해 전달되는 커맨드 패킷을 Jetson으로 송신 (UDP).
- **Protocol Timer 스레드 (1개)**: 1Hz 주기로 Jetson들에게 Heartbeat 패킷을 전송하여 연결 유지.
- **PC Link 스레드 (1개)**: PC와의 UDP 통신 담당. PC에서 오는 명령을 전달하고 주기적으로 로봇의 상태 정보를 PC에 전송.

---

## 2. 파일별 상세 분석

### 2.1. 헤더 파일 (`.h`)

#### `proto.h`
Jetson과 BridgeDaemon, PC 간에 사용되는 **와이어 프로토콜(UDP 통신 규격)**을 정의합니다.
- 패킷 타입(`PKT_TYPE_IMAGE`, `LIDAR`, `ODOM`, `CMD`)과 커맨드 타입(`ESTOP`, `STOP`, `MOVE`, `HEARTBEAT`) 정의.
- 포트 정보 (`9000`: RX, `9001`: TX CMD, `9002`: PC Link) 및 MTU (1400) 상수 정의.
- 모든 패킷의 기본이 되는 24바이트 `PktHeader` 구조체 및 데이터 페이로드 구조체 (`OdomPayload`, `CmdPayload`, `PcStatusPacket` 등)를 정의합니다.

#### `shm_def.h`
BridgeDaemon(Writer)과 Qt App(Reader) 간 데이터를 공유하기 위한 **공유 메모리(SHM) 레이아웃**을 정의합니다.
- 로봇마다 개별 SHM 파일(`/robot_bridge_N`) 생성.
- **Image / LiDAR**: Triple Buffer 구조와 `atomic index swap`을 통해 Lock 오버헤드 없이 동기화를 구현하고, `sem_t`를 이용해 새 프레임 알림 이벤트를 제공.
- **Odom / Meta**: 크기가 작은 데이터들은 `pthread_rwlock`을 사용하여 동기화.

#### `frag_queue.h`
Jetson RX 스레드(생산자)와 Reassembly SHM 스레드(소비자) 간 데이터를 주고받기 위한 **Blocking Queue**를 정의합니다.
- 최대 512개의 패킷을 담을 수 있는 원형 큐이며 `pthread_mutex`와 `pthread_cond`를 사용해 동기화 처리.
- 큐 포화 시 가장 오래된 패킷을 덮어씌워 Drop 하는 정책 적용.

#### `bridge_ctx.h`
스레드 간 공유해야 할 **컨텍스트(Context) 구조체**들을 정의합니다.
- `JetsonAddrTable`: 로봇의 동적 IP 주소를 관리 (등록 및 조회).
- 각 서브 스레드가 구동 시 파라미터로 넘겨받는 `JetsonRxCtx`, `JetsonTxCtx`, `ProtoTimerCtx`, `PcLinkCtx`, `ReasmCtx` 선언.

#### `cmd_dispatch.h` & `utils.h`
- `cmd_dispatch.h`: `jetson_tx` 및 `pc_link` 양쪽에서 공통으로 호출되는 커맨드 패킷 전송 함수(`send_cmd_to_jetson`) 선언.
- `utils.h`: `CLOCK_MONOTONIC` 기반의 고정밀 마이크로초 타임스탬프 반환 유틸 함수(`now_us`) 정의.

---

### 2.2. 소스 파일 (`.c`)

#### `bridge_main.c`
프로그램의 엔트리 포인트이며 **전체 시스템의 생명주기를 관리**합니다.
- SHM(`shm_open`, `mmap`) 및 세마포어, Rwlock을 각 로봇 개수만큼 초기화.
- 스레드 스케줄링(우선순위, CPU Affinity)을 적용하여 RX 스레드는 실시간성을 보장.
- `watchdog_loop()`를 돌면서 SHM의 `pkt_count`를 확인해 3초간 패킷이 없으면 Jetson 연결 상태를 Disconnected(`0`)로 변경.

#### `jetson_rx.c` (★★★★ 중요)
- UDP Port 9000을 열어 **모든 로봇의 트래픽을 단일 소켓에서 수신**합니다.
- `recvfrom`으로 송신자 IP를 확인해 `JetsonAddrTable`에 로봇 IP를 자동 학습/등록합니다.
- Odom 데이터는 즉각적으로 `handle_odom()`을 통해 SHM에 기록하고, 부하가 큰 Image/LiDAR 데이터는 `FragQueue`에 밀어 넣고 즉시 복귀하여 수신 버퍼 오버플로우를 막습니다.

#### `reassembly_shm.c` (★★★)
- `FragQueue`에서 데이터를 꺼내와 MTU 단위로 쪼개져 도착하는 Image 및 LiDAR 패킷 조각(Fragments)들을 **하나의 완전한 프레임으로 재조립**합니다.
- `ReasmSlot` 구조체를 힙에 할당하여 최대 6개의 동시 재조립 프레임을 처리.
- 100ms(`REASM_TIMEOUT_US`) 이상 지난 미완성 조립 프레임은 버리고 자원을 회수하는 GC(Garbage Collection) 로직을 포함.
- 버퍼 오버플로우/언더플로우 공격 또는 버그 방어 로직(`offset >= REASM_BUF_MAX` 예외처리) 포함.
- 조립이 완료되면 Triple Buffer 인덱스를 업데이트하고 세마포어를 통해 Qt 앱을 깨웁니다(`sem_post`).

#### `jetson_tx.c` (★★★★★ 중요)
- Qt 앱과 통신하기 위한 유닉스 도메인 소켓(`/tmp/bridge_cmd.sock`) 서버 역할을 합니다.
- Qt 앱으로부터 로봇 조작 명령(`CmdPacket`)을 받으면 즉시 Jetson의 9001 포트로 송신합니다.
- 비상 정지/제어 명령의 지연을 막기 위해 높은 우선순위로 동작합니다.

#### `protocol_timer.c` (★★★★★ 중요)
- Jetson에게 RPi5가 생존해 있음을 알리기 위해 매 1초마다 모든 등록된 Jetson IP로 `CMD_TYPE_HEARTBEAT` 패킷을 쏘는 Heartbeat 역할을 수행합니다.

#### `pc_link.c` (★★★)
- 외부 원격 PC와 통신(UDP 9002)하는 스레드.
- PC에서 들어오는 원격 제어 명령 수신 시 `send_cmd_to_jetson`을 호출해 Jetson으로 포워딩.
- 매 1초마다 `send_status_all()` 함수를 통해 각 로봇의 Odom 상태 및 통신 연결(드롭) 정보를 `PcStatusPacket`으로 감싸 PC로 전송.

#### `cmd_dispatch.c`
- 로봇 ID 검증 및 `JetsonAddrTable`에서 타겟 IP 조회 후, `CmdPacket`을 와이어 프로토콜인 `PktHeader + CmdPayload` 형태로 래핑하여 `sendto`를 호출하는 공통 모듈.

---

## 3. 데이터 흐름 및 동기화 전략 분석

### 3.1. 데이터 흐름 (Data Pipeline)
1. **[Jetson -> RPi] 영상/LiDAR 데이터**: 
   `UDP Receive(jetson_rx)` -> `FragQueue(Producer)` -> `reassembly_shm(Consumer)` -> `Triple Buffer(SHM)` -> `Qt App 읽기`
2. **[Jetson -> RPi] Odom 데이터**: 
   `UDP Receive(jetson_rx)` -> `Direct SHM Write(RwLock)` -> `Qt App 읽기`
3. **[Qt App -> Jetson] 제어 커맨드**:
   `Qt App` -> `Unix Domain Socket(/tmp/bridge_cmd.sock)` -> `jetson_tx` -> `UDP Send` -> `Jetson`
4. **[PC <-> RPi <-> Jetson] 원격 관제**:
   `PC Link UDP (Status Report 1Hz)` / `PC Command -> pc_link -> Jetson`

### 3.2. 동기화 전략 (Synchronization)
- **Lock-free Read/Write (Triple Buffer)**: 대용량 이미지/LiDAR 처리 시 Writer(`reassembly_shm`)와 Reader(`QtApp`)가 사용하는 버퍼 인덱스가 겹치지 않게 `atomic_store/atomic_load`로 인덱스를 전환해 Lock Overhead를 완벽히 제거했습니다.
- **Blocking Queue**: 네트워크 수신 스레드와 재조립 스레드 분리를 위해 `pthread_mutex`와 `pthread_cond` 조합 사용.
- **RwLock**: Odom/Meta 같은 작고 잦은 업데이트가 있는 데이터들은 `pthread_rwlock`을 사용하여 다수 Reader(Qt UI, PC Link)가 안전하게 동시 접근 가능하도록 설계되었습니다.

---

## 결론
`bridge_app_sh` 코드는 **고성능 데이터 처리와 안정성 보장**에 매우 중점을 두고 설계되었습니다. 특히 실시간 처리를 위해 스레드를 쪼개고 무거운 Reassembly 과정과 가벼운 수신(RX) 과정을 비동기식으로 분리한 점, 그리고 데이터 레이스 없는 Triple Buffer 동기화 방식을 채택하여 수신 병목과 렌더링 병목을 효과적으로 제거한 탄탄한 아키텍처를 보여주고 있습니다.
