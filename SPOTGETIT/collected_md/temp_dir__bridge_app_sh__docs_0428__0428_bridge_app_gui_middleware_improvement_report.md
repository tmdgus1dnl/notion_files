# 정찰로봇 GUI 애플리케이션 및 통신 미들웨어 확장 개선 보고서

작성일: 2026-04-28

## 1. 보고서 목적

본 보고서는 현재 `bridge_app_sh`의 BridgeDaemon을 단순한 UDP-SHM 중계 프로그램이 아니라, **정찰로봇 GUI 애플리케이션을 뒷받침하는 통신 미들웨어**로 발전시키기 위한 개선 방향을 정리한다.

현재 BridgeDaemon은 Jetson 로봇, 로컬 Qt GUI, 원격 PC 사이에서 다음 역할을 이미 수행한다.

- Jetson 센서 데이터 수신
- 이미지/LiDAR fragment 재조립
- POSIX 공유메모리를 통한 Qt GUI 데이터 제공
- Qt/PC 명령을 Jetson으로 전달
- Heartbeat 송신
- Watchdog 기반 연결 상태 판단
- 다중 로봇 ID 기반 분리

그러나 정찰로봇 GUI 플랫폼으로 확장하려면 단순 데이터 전달을 넘어서 다음 기능이 필요하다.

- GUI가 바로 사용할 수 있는 통합 상태 모델
- 명령 ACK/재전송/우선순위 제어
- 임무/경로/이벤트 중심 API
- 네트워크 품질 진단
- 로깅/녹화/재생
- 보안과 인증
- 장애 복구와 운영 관측성

## 2. 현행 시스템 구조 진단

### 2.1 현재 BridgeDaemon의 책임

| 책임 | 현재 구현 | 소스 위치 |
|---|---|---|
| Jetson UDP 수신 | UDP 9000에서 모든 로봇 패킷 수신 | `jetson_rx.c` |
| Robot ID 분배 | `hdr->robot_id` 기준 SHM/Queue 선택 | `jetson_rx.c:126-138` |
| Odom 기록 | Odom payload를 SHM에 rwlock으로 기록 | `jetson_rx.c:72-88` |
| 이미지/LiDAR 큐잉 | fragment를 로봇별 `FragQueue`에 push | `jetson_rx.c:149-151`, `frag_queue.h` |
| 재조립 | fragment를 frame 단위로 조립 | `reassembly_shm.c` |
| GUI 데이터 제공 | `/robot_bridge_N` POSIX SHM 제공 | `shm_def.h`, `bridge_main.c` |
| GUI 명령 수신 | Unix Domain Socket `/tmp/bridge_cmd.sock` | `jetson_tx.c` |
| PC 명령/상태 연동 | UDP 9002 명령 수신 및 1Hz 상태 송신 | `pc_link.c` |
| Heartbeat | 1Hz CMD heartbeat Jetson 송신 | `protocol_timer.c` |
| 명령 패킷화 | `CmdPacket`을 `PktHeader + CmdPayload`로 래핑 | `cmd_dispatch.c` |

### 2.2 현재 데이터 흐름

```text
Jetson
  -> UDP:9000
  -> jetson_rx
     -> Odom: SharedData 직접 기록
     -> Image/LiDAR: FragQueue push
  -> reassembly_shm
     -> frame 재조립
     -> SHM Triple Buffer publish
     -> sem_post
  -> Qt GUI
     -> shm_open(/robot_bridge_N)
     -> atomic ready_idx load
     -> image/lidar/odom 렌더링
```

```text
Qt GUI
  -> Unix Domain Socket /tmp/bridge_cmd.sock
  -> jetson_tx
  -> cmd_dispatch
  -> UDP:9001
  -> Jetson
```

```text
Remote PC
  -> UDP:9002 CmdPacket
  -> pc_link
  -> cmd_dispatch
  -> UDP:9001 Jetson

BridgeDaemon
  -> UDP:9002 PcStatusPacket 1Hz
  -> Remote PC
```

### 2.3 현재 구조의 강점

#### 대용량 센서 경로가 빠르다

이미지와 LiDAR는 SHM Triple Buffer와 atomic index swap을 사용한다. GUI가 매번 socket으로 대용량 데이터를 복사받지 않아도 되고, BridgeDaemon과 Qt GUI가 같은 메모리 객체를 공유하므로 지연이 낮다.

관련 위치:

- `shm_def.h:87-95`
- `reassembly_shm.c:88-162`
- `bridge_main.c:63-146`

#### 제어 명령 경로가 짧다

Qt GUI 명령은 Unix Domain Socket으로 BridgeDaemon에 들어오고, `send_cmd_to_jetson()`을 통해 바로 UDP 9001로 나간다. 로컬 GUI에서 조이스틱/정지 명령을 빠르게 전달하기 좋은 구조다.

관련 위치:

- `jetson_tx.c:37-86`
- `cmd_dispatch.c:13-63`
- `proto.h:61-78`

#### 다중 로봇 구조의 기본 틀이 있다

`MAX_ROBOTS`, `robot_id`, 로봇별 SHM `/robot_bridge_N`, 로봇별 `FragQueue`, 로봇별 `reassembly_shm_thread`가 이미 있다.

관련 위치:

- `shm_def.h:35-37`
- `bridge_main.c:263-303`
- `jetson_rx.c:126-138`

### 2.4 현재 구조의 한계

#### GUI가 소비할 수 있는 상태 모델이 좁다

현재 SHM에는 이미지, LiDAR, Odom, 연결 상태, 일부 drop count만 있다. 정찰 GUI에서 필요한 배터리, 임무 상태, 로봇 모드, 네트워크 품질, 센서 상태, 경고 이벤트, 지도/목표점 같은 정보가 없다.

현재 상태 패킷도 `PcStatusPacket`이 Odom과 drop count 중심이다.

관련 위치:

- `proto.h:80-91`
- `pc_link.c:33-63`
- `shm_def.h:74-112`

#### 명령 전달이 best-effort UDP에 가깝다

`CmdPacket`에는 `seq`가 있지만, ACK 패킷이나 재전송 로직은 없다. 비상 정지, 모드 전환, 임무 시작/중지 같은 명령은 성공 여부가 GUI에 반드시 돌아와야 한다.

관련 위치:

- `proto.h:61-78`
- `cmd_dispatch.c:13-63`
- `protocol_timer.c:11-13`의 향후 확장 주석

#### GUI 클라이언트 관리가 단일 연결 중심이다

`jetson_tx.c`는 Unix Domain Socket `listen(fd, 1)`로 Qt 연결을 받는다. GUI가 하나일 때는 충분하지만, 향후 운영 GUI, 디버그 툴, 자동화 클라이언트가 동시에 붙는 구조로는 부족하다.

관련 위치:

- `jetson_tx.c:37-56`
- `jetson_tx.c:100-123`

#### 정찰 임무 계층이 없다

현재 명령 타입은 ESTOP, STOP, MOVE, HEARTBEAT뿐이다. 정찰 로봇 GUI가 요구하는 “목표 지점 이동”, “순찰 경로 업로드”, “센서 촬영 요청”, “관심 지점 마킹”, “복귀 명령”, “자율/수동 모드 전환” 같은 추상 명령이 없다.

관련 위치:

- `proto.h:21-25`
- `proto.h:61-78`

#### 관측성과 운영 진단이 부족하다

현재 로그는 `fprintf(stderr, ...)` 중심이다. 프레임 레이턴시, 큐 지연, fragment 유실률, 명령 왕복 시간, 로봇별 네트워크 품질 같은 운영 지표가 구조화되어 있지 않다.

## 3. 목표 아키텍처

BridgeDaemon을 통신 미들웨어로 확장한다면 목표 구조는 다음과 같다.

```text
                 +----------------------+
                 |   정찰로봇 Qt GUI     |
                 |  - 영상/지도/상태     |
                 |  - 조작/임무 제어     |
                 +----------+-----------+
                            |
       SHM video/lidar      | command/event API
             +--------------+--------------+
             |                             |
+------------v-------------------------------------------+
|                 Bridge Middleware                      |
|                                                        |
|  [Sensor Plane]   UDP RX -> Reasm -> SHM Publish        |
|  [State Plane]    Robot State Store -> GUI/PC Snapshot  |
|  [Command Plane]  Cmd Router -> ACK/Retry/Priority      |
|  [Event Plane]    Alarm/Event Log -> GUI/PC Subscribe   |
|  [Ops Plane]      Metrics/Health/Recording/Replay       |
+------------+-------------------------------------------+
             |
             | UDP data/cmd/ack/heartbeat
             |
      +------v-------+        +------v-------+
      | Jetson #0    |  ...   | Jetson #N    |
      +--------------+        +--------------+
```

핵심은 현재 하나로 섞인 “통신 중계”를 다음 5개 plane으로 나누는 것이다.

| Plane | 역할 | 예시 |
|---|---|---|
| Sensor Plane | 고대역 센서 수신/재조립/공유 | Image, LiDAR, thermal, audio |
| State Plane | GUI가 읽는 최신 로봇 상태 모델 | pose, battery, mode, link quality |
| Command Plane | 명령 라우팅/ACK/재전송/우선순위 | ESTOP, velocity, mission command |
| Event Plane | 경고/탐지/장애 이벤트 전달 | obstacle, target detected, low battery |
| Ops Plane | 기록/재생/통계/진단 | latency, drop rate, pcap-like record |

## 4. GUI 애플리케이션 지원 개선 방향

### 4.1 통합 로봇 상태 모델 확장

현재 GUI가 직접 읽을 수 있는 상태는 제한적이다. 정찰 GUI는 로봇별로 최소 다음 상태를 가져야 한다.

| 상태 범주 | 필요한 필드 | 사용 UI |
|---|---|---|
| 연결 | connected, last_seen, link_quality, rtt_ms | 로봇 카드, 상태등 |
| 위치 | x, y, theta, vx, vy, omega, covariance | 지도, 미니맵 |
| 전원 | battery_percent, voltage, current, temperature | 상태 패널 |
| 모드 | manual/autonomous/returning/estop/fault | 상단 모드 표시 |
| 센서 | camera_ok, lidar_ok, imu_ok, gps_ok | 센서 헬스 패널 |
| 스트림 | image_fps, lidar_fps, drop_rate, latency | 영상/LiDAR 뷰 |
| 임무 | mission_id, waypoint_idx, progress, current_goal | 임무 패널 |
| 장애 | fault_code, fault_text, severity | 알림 센터 |

개선 제안:

- `SharedData`에 `RobotState` 영역 추가
- `RobotState`는 작은 필드 중심이므로 seqlock 또는 rwlock snapshot 방식 사용
- GUI는 SHM에서 로봇 상태를 직접 읽고, 큰 데이터는 기존 image/lidar slot을 유지

예상 구조:

```c
typedef struct {
    uint32_t seq;
    uint64_t updated_us;
    uint8_t connected;
    uint8_t mode;
    uint8_t fault_level;
    float battery_percent;
    float link_rtt_ms;
    float image_fps;
    float lidar_fps;
    uint32_t fault_code;
} RobotState;
```

### 4.2 GUI용 이벤트 채널 추가

정찰 GUI는 최신 상태만 보는 것으로 부족하다. “언제 어떤 이벤트가 발생했는가”를 시간순으로 보여줘야 한다.

필요 이벤트:

- 연결됨/연결 끊김
- ESTOP 발생/해제
- 명령 ACK 실패
- 프레임 드롭 급증
- 배터리 부족
- 센서 오류
- 탐지 이벤트
- 임무 시작/중지/완료

개선 제안:

- SHM에 작은 ring buffer 형태의 `EventLog` 추가
- GUI는 `event_write_seq`를 보고 새 이벤트만 소비
- 원격 PC에도 이벤트 요약을 UDP/TCP/WebSocket 등으로 전달 가능

예상 구조:

```c
#define EVENT_LOG_SIZE 1024

typedef struct {
    uint64_t timestamp_us;
    uint8_t robot_id;
    uint8_t severity;
    uint16_t event_type;
    uint32_t code;
    char message[96];
} RobotEvent;
```

### 4.3 GUI 명령 API 확장

현재 `CmdPacket`은 속도 명령 중심이다. 정찰 GUI를 지원하려면 명령 타입을 계층화해야 한다.

| 명령 범주 | 예시 |
|---|---|
| 즉시 제어 | ESTOP, STOP, MOVE |
| 모드 제어 | MANUAL, AUTO, HOLD, RETURN_HOME |
| 임무 제어 | START_MISSION, PAUSE_MISSION, RESUME_MISSION, CANCEL_MISSION |
| 경로 제어 | SET_WAYPOINT, SET_ROUTE, CLEAR_ROUTE |
| 센서 제어 | CAMERA_SNAPSHOT, START_RECORDING, SET_CAMERA_MODE |
| 시스템 제어 | REBOOT_JETSON, RESTART_SENSOR, CALIBRATE_IMU |

개선 제안:

- `CmdPacket`을 고정 velocity 구조에서 generic command envelope로 변경
- payload는 command type별 union 또는 TLV 사용
- 모든 중요 명령에 `cmd_id`, `seq`, `deadline_ms`, `priority`, `requires_ack` 부여

예상 구조:

```c
typedef struct __attribute__((packed)) {
    uint8_t robot_id;
    uint8_t command_type;
    uint8_t priority;
    uint8_t flags;
    uint32_t command_id;
    uint32_t seq;
    uint64_t timestamp_us;
    uint32_t payload_len;
} CommandEnvelope;
```

### 4.4 GUI 다중 클라이언트 지원

향후에는 한 GUI만 붙지 않을 수 있다.

- 로컬 조종 GUI
- 관제 PC
- 디버그 콘솔
- 자동 테스트 클라이언트
- 녹화/재생 도구

개선 제안:

- Unix Domain Socket 서버를 다중 client accept 구조로 변경
- client별 권한/역할 도입
- ESTOP 같은 고우선순위 명령은 모든 client보다 우선 처리
- 같은 robot에 대해 control ownership 개념 도입

Control ownership 예시:

| 상태 | 의미 |
|---|---|
| `NO_OWNER` | 누구나 요청 가능 |
| `LOCAL_GUI` | 로컬 GUI가 수동 조작권 보유 |
| `REMOTE_PC` | 원격 PC가 조작권 보유 |
| `AUTO_MISSION` | 자율 임무가 조작권 보유 |
| `ESTOP_LOCKED` | 비상정지 상태, 해제 권한 필요 |

### 4.5 지도/임무 GUI 연동

정찰로봇 GUI는 단순 영상 뷰어가 아니라 지도와 임무를 함께 다뤄야 한다.

추가할 데이터:

- 로봇 pose history
- waypoint list
- route progress
- local occupancy grid 또는 costmap summary
- 관심 지점 POI
- 탐지 객체 bounding box/position

BridgeDaemon이 모든 지도 처리를 직접 할 필요는 없다. 다만 GUI와 Jetson 사이에서 임무/지도 관련 메시지를 안정적으로 중계할 수 있어야 한다.

## 5. 통신 미들웨어 개선 방향

### 5.1 프로토콜 버전 및 capability negotiation

현재 `PktHeader`에는 프로토콜 버전이 없다. 향후 패킷 타입과 payload가 늘어나면 구버전 GUI/Jetson과 충돌할 수 있다.

개선 제안:

- `PktHeader`에 `version`, `header_len`, `flags` 추가
- 시작 시 `HELLO` / `CAPABILITY` 패킷 교환
- Jetson별 지원 센서/명령/프로토콜 버전 저장

Capability 예시:

| capability | 의미 |
|---|---|
| `CAP_IMAGE_JPEG` | JPEG 이미지 송신 가능 |
| `CAP_LIDAR_XYZI` | XYZI LiDAR 송신 가능 |
| `CAP_CMD_ACK` | 명령 ACK 지원 |
| `CAP_MISSION` | mission command 지원 |
| `CAP_THERMAL` | 열화상 지원 |
| `CAP_DETECTION` | 객체 탐지 결과 지원 |

### 5.2 명령 ACK/재전송/타임아웃

정찰로봇에서 ESTOP, 임무 시작/중지, 복귀 명령은 best-effort UDP만으로 부족하다.

개선 제안:

- Jetson이 `CMD_ACK` 패킷을 BridgeDaemon으로 회신
- BridgeDaemon은 pending command table 유지
- timeout 안에 ACK가 없으면 재전송 또는 GUI에 실패 이벤트 전달
- ESTOP은 짧은 간격으로 여러 번 전송
- MOVE velocity 명령은 최신값만 유지하고 오래된 명령은 폐기

명령별 정책:

| 명령 | 정책 |
|---|---|
| ESTOP | 최고 우선순위, 즉시 다중 전송, ACK 요구 |
| STOP | 높은 우선순위, ACK 요구 |
| MOVE | 최신값만 유지, ACK 선택 |
| HEARTBEAT | 주기 송신, ACK 불필요 또는 낮은 빈도 |
| MISSION_START | ACK 요구, 상태 전이 확인 |
| SET_ROUTE | payload checksum, ACK 요구 |

### 5.3 센서 스트림 QoS 분리

현재 이미지, LiDAR, Odom이 모두 BridgeDaemon을 통과하지만 우선순위 정책은 제한적이다.

개선 제안:

- 데이터 타입별 QoS 정책 명시
- Odom/State는 최신값 우선
- Image는 최신 프레임 우선, 오래된 frame drop
- LiDAR는 최신 프레임 우선 또는 중요 모드에서 reliable mode 선택
- Event/Alarm은 유실 최소화

QoS 예시:

| 데이터 | 목표 | 정책 |
|---|---|---|
| ESTOP | 유실 방지 | 반복 송신 + ACK |
| MOVE | 최신성 | 오래된 명령 폐기 |
| Odom | 최신성/저지연 | SHM 최신값 덮어쓰기 |
| Image | 최신성 | frame drop 허용 |
| LiDAR | 최신성/부분 신뢰성 | drop count 관리 |
| Event | 신뢰성 | sequence + 재요청 |
| Mission | 신뢰성 | ACK + 상태 확인 |

### 5.4 Fragment protocol 강화

현재 fragment 재조립은 frame_id, frag_idx, frag_total, payload_offset 기반이다. 여기에 다음을 추가하면 장애 진단과 데이터 무결성이 좋아진다.

개선 제안:

- frame 단위 total_size
- frame CRC32 또는 payload hash
- fragment payload_len 검증 강화
- frame 시작/종료 marker 또는 manifest fragment
- fragment bitmap을 GUI/진단 로그로 노출
- type별 최대 frame size 명시

현행 코드상 이미 있는 방어:

- 첫 fragment 누락 시 폐기: `reassembly_shm.c:202-205`
- `frag_idx`, `frag_total` 검증: `reassembly_shm.c:225-231`
- OOB 방어: `reassembly_shm.c:238-246`
- timeout GC: `reassembly_shm.c:49-68`

추가하면 좋은 검증:

- `hdr->payload_len == plen`
- `hdr->frag_total <= MAX_FRAGS_PER_FRAME`
- `offset`이 type별 frame size 한도 안인지 확인
- 동일 frame에서 `timestamp_us`, `frag_total`이 바뀌면 폐기
- 완성 후 `total_size`와 선언 크기 비교

### 5.5 네트워크 품질 추정

GUI에서 정찰로봇 운용자는 “왜 화면이 끊기는지”를 알아야 한다. 단순 connected보다 품질 지표가 필요하다.

추가 지표:

- last_rx_us
- packet_rate
- image_fps
- lidar_fps
- fragment_loss_estimate
- queue_drop_count
- reassembly_timeout_count
- cmd_ack_rtt_ms
- heartbeat_jitter_ms
- bandwidth_rx_kbps
- bandwidth_tx_kbps

이 지표는 GUI의 로봇 카드와 네트워크 진단 패널에 직접 표시할 수 있다.

### 5.6 보안과 권한

현재 UDP/Unix Socket 명령 경로는 기본적인 인증이 없다. 정찰로봇에서는 잘못된 명령 주입이 위험하다.

개선 제안:

- PC Link UDP에는 최소 shared token 또는 HMAC 추가
- 운영망에서는 WireGuard/VPN 전제로 설계
- Unix Socket 파일 권한 관리
- client role 기반 권한 분리
- ESTOP 해제는 더 높은 권한 필요
- 명령 audit log 저장

### 5.7 로깅/녹화/재생

정찰로봇 GUI에서 중요한 기능은 사후 분석이다.

필요 기능:

- image frame 저장
- LiDAR frame 저장
- Odom/state timeline 저장
- command/event log 저장
- 네트워크 통계 저장
- 특정 시점 replay

BridgeDaemon에 직접 모든 인코딩을 넣기보다, record worker를 별도 모듈로 두는 것이 낫다.

```text
Sensor/State/Event
  -> BridgeDaemon live path
  -> Recorder queue
  -> disk writer
  -> replay server
```

녹화 파일 구조 예:

```text
session_20260428_153000/
  manifest.json
  robot_0/
    image.mcap
    lidar.mcap
    odom.csv
    events.jsonl
    commands.jsonl
  robot_1/
    ...
```

## 6. 코드 구조 리팩터링 제안

### 6.1 모듈 분리 방향

현재 파일 구조는 작고 직접적이지만, 미들웨어로 확장하면 책임이 커진다. 다음처럼 나누는 것이 좋다.

```text
bridge_main.c          // lifecycle, signal, thread orchestration
bridge_config.c/h      // 설정 파일, CLI 옵션
proto_v1.h             // wire protocol v1
shm_layout.h           // GUI ABI layout only
shm_publish.c/h        // image/lidar/state/event publish API
robot_registry.c/h     // robot discovery, address, capability
robot_state.c/h        // RobotState, stats, watchdog
cmd_router.c/h         // command ownership, priority, ACK/retry
udp_rx.c/h             // Jetson receive socket
udp_tx.c/h             // Jetson send socket
fragment_reasm.c/h     // fragment validation/reassembly
spsc_queue.c/h         // fast queues
pc_gateway.c/h         // remote PC API
gui_gateway.c/h        // Unix socket GUI API
metrics.c/h            // counters, latency, health
recorder.c/h           // recording
```

### 6.2 `bridge_main.c` 축소

현재 `bridge_main.c`는 SHM 생성, signal handler, watchdog, thread 생성, cleanup을 모두 담당한다. 확장 시 유지보수가 어려워진다.

개선 방향:

- signal handler는 `sig_atomic_t`만 설정
- 실제 stop/queue wakeup/join은 main loop에서 수행
- `BridgeRuntime` 생성/시작/정지 API 분리
- watchdog은 `robot_state.c` 또는 별도 thread로 이동

### 6.3 stop 플래그 atomic화

현재 `volatile int stop`은 스레드 동기화 수단으로 충분하지 않다.

개선 방향:

- `volatile int stop`을 `atomic_bool stop`으로 변경
- 모든 루프에서 `atomic_load_explicit(&stop, memory_order_acquire)`
- 종료 요청은 `atomic_store_explicit(&stop, true, memory_order_release)`

적용 대상:

- `bridge_ctx.h:25`
- `bridge_ctx.h:32`
- `bridge_ctx.h:39`
- `bridge_ctx.h:47`
- `bridge_main.c:45`

### 6.4 signal handler 안전화

현재 `sig_handler()`는 `fprintf()`와 `frag_queue_stop()`을 호출한다. `frag_queue_stop()`은 mutex를 잡기 때문에 signal handler에서 안전하지 않다.

개선 방향:

- handler는 `g_stop_signal = 1`만 수행
- main loop가 이를 보고 일반 문맥에서 `frag_queue_stop()` 호출
- `signalfd` 또는 self-pipe 도입도 가능

관련 위치:

- `bridge_main.c:197-208`

### 6.5 SHM ABI 안정화

현재 `shm_def.h`는 C에서는 `_Atomic`, C++에서는 `std::atomic`을 사용한다. 같은 플랫폼에서는 대체로 동작하지만, 장기적으로 ABI 검증이 필요하다.

개선 방향:

- `SHM_MAGIC`, `SHM_VERSION`, `sizeof(SharedData)` 필드 추가
- GUI가 attach 시 version/size 검증
- atomic 필드 offset static assert
- 가능하면 atomic wrapper 접근 함수 제공
- memory order를 release/acquire로 명시

추가 필드 예:

```c
uint32_t shm_magic;
uint16_t shm_version;
uint16_t shm_header_size;
uint32_t shared_data_size;
```

## 7. 정찰로봇 GUI 기능 확장 제안

### 7.1 메인 화면 구성

GUI가 BridgeDaemon의 확장 상태를 활용하면 다음 화면 구성이 가능하다.

| 영역 | 표시 내용 | 데이터 소스 |
|---|---|---|
| 로봇 목록 | 연결, 배터리, 모드, 네트워크 품질 | SHM RobotState |
| 영상 뷰 | JPEG image stream, FPS, latency | SHM img_slots |
| LiDAR 뷰 | point cloud, scan age, drop rate | SHM lidar_slots |
| 지도 | robot pose, trail, waypoint | Odom + mission state |
| 제어 패널 | velocity, ESTOP, mode, ownership | GUI command API |
| 이벤트 패널 | 경고, 탐지, 장애, ACK 실패 | SHM EventLog |
| 진단 패널 | queue depth, drops, RTT, bandwidth | Metrics |

### 7.2 상황 인식 기능

정찰 GUI는 운영자에게 “현재 무엇이 위험한지”를 빨리 보여줘야 한다.

추가 기능:

- 로봇별 위험도 색상
- 연결 품질 하락 시 자동 경고
- 영상 지연이 일정 이상이면 stale 표시
- 명령 ACK 실패 시 command failed 표시
- 배터리 부족/센서 장애 이벤트 상단 고정
- ESTOP 상태는 모든 화면에서 최우선 표시

### 7.3 임무 중심 워크플로우

정찰로봇은 수동 조작뿐 아니라 임무 단위 운용이 중요하다.

추천 워크플로우:

1. 지도에서 목표 지점 선택
2. 경로 생성 또는 waypoint 입력
3. BridgeDaemon이 route command를 Jetson에 전달
4. Jetson이 mission accepted ACK
5. GUI는 progress, current waypoint, ETA 표시
6. 이상 발생 시 pause/return/estop

이를 위해 BridgeDaemon에는 mission command와 mission state 중계 기능이 필요하다.

## 8. 단계별 구현 로드맵

### 8.1 1단계: 안정성 기반 정리

목표: 현재 구조를 유지하면서 실사용 위험을 줄인다.

작업:

- signal handler 안전화
- stop 플래그 atomic화
- packet header validation 함수 추가
- `hdr->payload_len` 검증 추가
- watchdog 상태를 구조화
- `SharedData`에 magic/version/size 추가
- ThreadSanitizer 단독 빌드 타깃 분리

우선순위:

| 우선순위 | 항목 | 이유 |
|---|---|---|
| P0 | signal handler 안전화 | 종료 데드락 방지 |
| P0 | stop atomic화 | 스레드 종료 가시성 보장 |
| P0 | packet validation | 비정상 UDP 입력 방어 |
| P1 | SHM version | GUI/Daemon ABI 불일치 방지 |
| P1 | metrics counter | 운영 진단 기반 |

### 8.2 2단계: GUI 상태 모델 확장

목표: GUI가 로봇 상태를 풍부하게 표시할 수 있게 한다.

작업:

- `RobotState` SHM 영역 추가
- frame latency/FPS/drop metrics 추가
- `EventLog` ring buffer 추가
- PC status packet 확장 또는 v2 packet 추가
- GUI client API 문서화

결과:

- GUI 로봇 카드 구현 가능
- 영상/LiDAR 지연 표시 가능
- 이벤트/경고 패널 구현 가능

### 8.3 3단계: Command Plane 강화

목표: 제어 명령을 신뢰성 있게 운용한다.

작업:

- `CMD_ACK` 패킷 타입 추가
- pending command table 추가
- timeout/retry 정책 구현
- command priority queue 추가
- control ownership 추가
- ESTOP 다중 전송 정책 구현

결과:

- GUI가 “명령 전송됨/수락됨/실패함”을 구분 가능
- 원격 PC와 로컬 GUI의 제어 충돌 방지
- 정찰 임무 명령 확장 기반 확보

### 8.4 4단계: 정찰 임무/이벤트 확장

목표: 단순 조종 앱에서 임무 기반 정찰 GUI로 확장한다.

작업:

- mission command 타입 추가
- waypoint/route payload 정의
- mission state packet 정의
- detection/event packet 정의
- sensor health packet 정의
- recording session control 추가

결과:

- 지도 기반 임무 제어 가능
- 탐지/경고 이벤트 표시 가능
- 센서 상태 기반 운영 가능

### 8.5 5단계: 운영/보안/재생

목표: 현장 운용과 사후 분석에 필요한 기능을 갖춘다.

작업:

- structured logging
- metrics endpoint 또는 status socket
- 녹화/재생 모듈
- PC Link 인증/HMAC
- 설정 파일 도입
- systemd unit/health check
- crash recovery 정책

결과:

- 현장 장애 분석 가능
- 비인가 명령 위험 감소
- 장시간 운용 안정성 향상

## 9. 프로토콜 확장안

### 9.1 패킷 타입 확장

현재:

```c
#define PKT_TYPE_IMAGE   0x01
#define PKT_TYPE_LIDAR   0x02
#define PKT_TYPE_ODOM    0x03
#define PKT_TYPE_CMD     0x04
```

제안:

```c
#define PKT_TYPE_IMAGE       0x01
#define PKT_TYPE_LIDAR       0x02
#define PKT_TYPE_ODOM        0x03
#define PKT_TYPE_CMD         0x04
#define PKT_TYPE_CMD_ACK     0x05
#define PKT_TYPE_STATE       0x06
#define PKT_TYPE_EVENT       0x07
#define PKT_TYPE_MISSION     0x08
#define PKT_TYPE_HEALTH      0x09
#define PKT_TYPE_CAPABILITY  0x0A
```

### 9.2 명령 타입 확장

현재:

```c
#define CMD_TYPE_ESTOP      0x01
#define CMD_TYPE_STOP       0x02
#define CMD_TYPE_MOVE       0x03
#define CMD_TYPE_HEARTBEAT  0x04
```

제안:

```c
#define CMD_TYPE_ESTOP          0x01
#define CMD_TYPE_STOP           0x02
#define CMD_TYPE_MOVE           0x03
#define CMD_TYPE_HEARTBEAT      0x04
#define CMD_TYPE_SET_MODE       0x05
#define CMD_TYPE_RETURN_HOME    0x06
#define CMD_TYPE_SET_WAYPOINT   0x07
#define CMD_TYPE_SET_ROUTE      0x08
#define CMD_TYPE_START_MISSION  0x09
#define CMD_TYPE_PAUSE_MISSION  0x0A
#define CMD_TYPE_CANCEL_MISSION 0x0B
#define CMD_TYPE_SENSOR_CTRL    0x0C
```

### 9.3 상태 패킷 v2

현재 `PcStatusPacket`은 다음 정도만 담는다.

- robot_id
- connected
- x/y/theta
- odom_seq
- img_drop
- lidar_drop
- timestamp_us

v2는 다음을 포함하는 것이 좋다.

```c
typedef struct __attribute__((packed)) {
    uint8_t  robot_id;
    uint8_t  connected;
    uint8_t  mode;
    uint8_t  fault_level;
    float    x;
    float    y;
    float    theta;
    float    vx;
    float    vy;
    float    omega;
    float    battery_percent;
    float    link_rtt_ms;
    float    image_fps;
    float    lidar_fps;
    uint32_t odom_seq;
    uint32_t img_drop;
    uint32_t lidar_drop;
    uint32_t event_seq;
    uint64_t last_rx_us;
    uint64_t timestamp_us;
} PcStatusPacketV2;
```

## 10. 데이터 저장소 확장안

### 10.1 `SharedData` 재구성 방향

현재 `SharedData`는 image/lidar/odom/meta가 한 구조체에 있다. 확장 시 다음처럼 영역을 명확히 나누는 것이 좋다.

```text
SharedData
  Header
    magic/version/size
  SensorBuffers
    image slots
    lidar slots
  FastState
    connected
    pkt_count
    ready indexes
  RobotState
    battery/mode/link/sensor health
  OdomState
    pose/twist/covariance
  EventLog
    ring buffer
  Metrics
    fps/drop/latency/queue depth
```

### 10.2 GUI ABI 원칙

GUI가 안정적으로 붙으려면 다음 원칙이 필요하다.

- 기존 필드 삭제 금지
- 새 필드는 뒤에 추가
- `shm_version`으로 레이아웃 구분
- `shared_data_size`로 attach 가능 여부 확인
- 모든 multi-field snapshot은 seq/rwlock/seqlock 중 하나로 보호
- 대용량 데이터는 slot + ready index 방식 유지

## 11. 성능 개선 방향

### 11.1 Queue 개선

현재 `frag_queue.h`는 mutex + condvar 기반이다. 단순하고 안정적이지만, 고부하에서 수신 루프 지연이 생길 수 있다.

개선 옵션:

| 옵션 | 장점 | 단점 |
|---|---|---|
| 현행 mutex queue 유지 | 단순, blocking 쉬움 | 고부하 경합 가능 |
| SPSC ring + sem/eventfd | 빠름, producer/consumer 구조에 적합 | 구현 복잡도 증가 |
| MPSC queue | 여러 RX worker 확장 가능 | 현재 구조엔 과할 수 있음 |

현 구조는 robot별 producer-consumer가 사실상 SPSC에 가까우므로, 장기적으로는 SPSC ring을 검토할 만하다.

### 11.2 Reader-aware Triple Buffer

현재 writer는 ready index 다음 슬롯에 쓴다. GUI reader가 아주 오래 특정 슬롯을 붙잡으면 overwrite 가능성이 있다.

개선 방향:

- GUI가 `reader_idx`를 publish
- writer는 `ready_idx`와 `reader_idx`를 피해서 write slot 선택
- 빈 슬롯이 없으면 최신성 정책에 따라 frame drop
- drop reason을 metrics에 기록

### 11.3 Thread 우선순위 정책 정리

현재 `create_thread()`는 priority와 core pinning을 지원한다. 다만 `SCHED_FIFO` 설정 실패 시 상세 대응이 없다.

개선 방향:

- priority 설정 실패 로그 강화
- 권한 부족 시 normal scheduling fallback 명시
- thread별 CPU affinity 설정을 config로 분리
- RX, command, heartbeat, reassembly 우선순위 문서화

## 12. 운영 배포 개선 방향

### 12.1 설정 파일 도입

현재 포트와 경로는 `proto.h`의 define 중심이다.

개선 방향:

```toml
num_robots = 4
bridge_port = 9000
jetson_cmd_port = 9001
pc_link_port = 9002
cmd_socket = "/tmp/bridge_cmd.sock"
recording_enabled = true

[robot.0]
name = "scout-0"
expected_ip = "192.168.1.20"

[qos.image]
max_fps = 30
drop_policy = "latest"
```

### 12.2 systemd 운용

현장 운용에서는 데몬 자동 재시작과 로그 관리가 필요하다.

제안:

- `bridge_daemon.service`
- watchdog health file 또는 notify socket
- crash 시 SHM cleanup 정책
- `/run/bridge_app/bridge_cmd.sock`처럼 runtime dir 사용
- 로그는 journald + 별도 session log 병행

### 12.3 장애 복구

필요 정책:

- Jetson 재부팅 후 자동 재등록
- PC 주소 변경 시 재학습
- GUI 재시작 시 SHM 재attach
- BridgeDaemon 재시작 시 SHM version 재검증
- 오래된 command socket 파일 정리

## 13. 테스트 전략

### 13.1 단위 테스트

우선 테스트해야 할 기능:

- packet header validation
- fragment out-of-order 재조립
- fragment 중복 수신 처리
- fragment 누락 timeout
- queue full drop
- ready index publish 순서
- watchdog connected on/off
- command ACK timeout/retry

### 13.2 통합 테스트

가짜 Jetson simulator를 만들어 다음을 검증한다.

- 로봇 1/3/10대 동시 수신
- 이미지 30 FPS 송신
- LiDAR burst 송신
- fragment loss 1%, 5%, 10%
- PC 명령과 Qt 명령 동시 입력
- GUI 재시작 중 SHM attach 안정성
- BridgeDaemon 종료/재시작

### 13.3 부하 테스트

측정 지표:

- RX packet drop
- queue depth
- reassembly latency
- image publish latency
- GUI render stale time
- command send latency
- ACK RTT
- CPU usage per thread
- memory footprint per robot

## 14. 추천 구현 순서

실제 개발 순서는 다음이 가장 현실적이다.

1. 현재 코드 안정화
   - signal handler 수정
   - stop atomic화
   - packet validation 추가

2. GUI 상태 모델 확장
   - `RobotState`
   - metrics
   - event log

3. Command Plane 강화
   - command envelope
   - ACK
   - retry
   - ownership

4. 미들웨어 API 정리
   - GUI socket protocol v2
   - PC status v2
   - capability negotiation

5. 정찰 기능 확장
   - mission command
   - waypoint/route
   - detection/event
   - recording/replay

6. 운영화
   - config file
   - systemd
   - security
   - structured logs

## 15. 최종 제언

현재 BridgeDaemon은 “센서 데이터 고속 전달”에는 좋은 출발점이다. 특히 이미지/LiDAR를 SHM으로 넘기는 구조와 Qt 명령을 Unix Socket으로 받는 구조는 정찰로봇 GUI에 적합하다.

다만 정찰로봇 시스템의 중심 미들웨어가 되려면 현재의 역할을 다음처럼 확장해야 한다.

```text
현재:
  UDP 수신 + 재조립 + SHM publish + 단순 command forward

목표:
  Sensor Plane + State Plane + Command Plane + Event Plane + Ops Plane
```

가장 먼저 고칠 부분은 안정성이다. signal/stop/packet validation/SHM version을 정리해야 그 위에 GUI 상태 모델과 명령 신뢰성 계층을 얹을 수 있다. 그 다음 `RobotState`, `EventLog`, `Metrics`, `Command ACK`를 추가하면 GUI는 단순 영상 뷰어가 아니라 정찰로봇 관제 애플리케이션으로 발전할 수 있다.

장기적으로는 BridgeDaemon을 “로봇별 센서 스트림을 GUI에 빠르게 공급하는 데이터 플레인”과 “명령/상태/이벤트를 신뢰성 있게 중계하는 컨트롤 플레인”으로 나누는 것이 가장 좋은 확장 방향이다.
