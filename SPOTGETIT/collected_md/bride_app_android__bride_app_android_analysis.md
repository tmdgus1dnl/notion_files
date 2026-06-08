# bride_app_android Code Analysis Report

작성일: 2026-05-07

## 1. 분석 대상

요청 경로명은 `bridge_app_android`였지만, 실제 저장소 경로는 `bride_app_android`이다.

이 디렉터리는 Raspberry Pi/Linux에서 동작하던 C 기반 `BridgeDaemon`을 Android 네이티브 라이브러리와 Android Service 형태로 포팅하기 위한 코드베이스다. 핵심 로직은 여전히 C로 구현되어 있으며, Android 쪽은 `ASharedMemory`, JNI, Binder AIDL을 통해 앱 프로세스와 미들웨어 프로세스를 연결하는 구조다.

## 2. 전체 구조

### 주요 파일

| 파일 | 역할 |
| --- | --- |
| `bridge_main.c` | 데몬 생명주기, 공유 메모리 초기화, 스레드 생성/종료, watchdog |
| `jetson_rx.c` | Jetson에서 오는 UDP 패킷 수신, robot_id 기반 분배 |
| `reassembly_shm.c` | 이미지/LiDAR fragment 재조립 후 공유 메모리에 기록 |
| `jetson_tx.c` | 공유 메모리 명령 큐에서 명령을 꺼내 Jetson으로 송신 |
| `protocol_timer.c` | 1Hz heartbeat 송신 및 ACK timeout polling |
| `pc_link.c` | PC UDP 명령 수신 및 상태 패킷 송신 |
| `bridge_api.c` | 상태/이벤트/명령/ACK/metrics 공통 API |
| `bridge_ipc.c` | Linux `shm_open`과 Android `ASharedMemory` 추상화 |
| `bridge_android_jni.c` | Android Service에서 호출하는 JNI 진입점 |
| `shm_def.h` | 공유 메모리 ABI 정의 |
| `proto.h` | UDP wire protocol 정의 |
| `android/src/main/java/com/robot/bridge/BridgeMiddlewareService.java` | Android 서비스 wrapper |
| `android/src/main/aidl/com/robot/bridge/IBridgeMiddleware.aidl` | 공유 메모리 fd 전달용 Binder 인터페이스 |
| `android/src/main/AndroidManifest.xml` | 서비스/권한 선언 |

## 3. 런타임 아키텍처

`bridge_daemon_run(num_robots)`가 전체 시스템의 진입점이다. Linux에서는 `main()`에서 직접 호출되고, Android에서는 JNI `nativeStart()`가 별도 pthread를 만들어 호출한다.

스레드 구성은 다음과 같다.

| 스레드 | 개수 | 역할 |
| --- | ---: | --- |
| main/watchdog | 1 | 1초마다 packet count 변화 확인, 연결 상태 갱신 |
| jetson_rx | 1 | UDP 9000 수신, Odom/ACK 처리, Image/LiDAR fragment queue push |
| reassembly_shm | robot 수만큼 | fragment 재조립, 이미지/LiDAR triple buffer commit |
| jetson_tx | 1 | `SharedData.cmd_queue` 소비, UDP 9001 명령 송신 |
| protocol_timer | 1 | heartbeat 1Hz 송신, pending command timeout 처리 |
| pc_link | 1 | UDP 9002 PC 명령 수신, 상태 패킷 송신 |

Linux에서는 thread affinity와 `SCHED_FIFO` 우선순위를 시도한다. Android 빌드에서는 `__ANDROID__` 조건으로 affinity/priority 설정을 비활성화한다.

## 4. 데이터 흐름

### Jetson to App

1. Jetson이 UDP 9000으로 `PktHeader + payload`를 송신한다.
2. `jetson_rx_thread()`가 수신 후 `proto_validate_header()`로 기본 검증을 수행한다.
3. `robot_id`가 유효하면 Jetson 송신 주소를 학습한다. 이후 명령 송신 대상은 이 주소의 UDP 9001이 된다.
4. `PKT_TYPE_ODOM`은 `bridge_api_update_odom()`으로 즉시 공유 메모리에 기록된다.
5. `PKT_TYPE_CMD_ACK`는 `bridge_api_handle_ack()`로 pending command를 해제한다.
6. `PKT_TYPE_IMAGE`, `PKT_TYPE_LIDAR`는 robot별 `FragQueue`에 들어간다.
7. `reassembly_shm_thread()`가 fragment를 모아 frame을 완성한다.
8. 이미지와 LiDAR는 각 triple buffer 슬롯에 기록되고, `img_ready_idx`/`lidar_ready_idx`가 atomic으로 갱신된다.
9. frame commit 후 `BridgeSignal`로 reader를 깨운다.

### App/PC to Jetson

1. Android 또는 Qt/클라이언트가 `SharedData.cmd_queue`에 `ShmCmdEntry`를 넣는 구조다.
2. `jetson_tx_thread()`가 모든 robot queue를 round-robin으로 검사한다.
3. queue 안에서는 `shm_cmd_queue_pop_highest()`가 priority가 가장 높은 entry를 먼저 꺼낸다.
4. `bridge_api_send_command()`가 legacy `PKT_TYPE_CMD` packet으로 UDP 9001에 송신한다.
5. STOP/ESTOP/mission 계열 또는 `CMD_FLAG_REQUIRES_ACK` 명령은 pending table에 등록된다.
6. ACK timeout은 250ms이며 critical priority는 최대 3회, 일반 ACK 명령은 1회 재시도된다.

### PC Link

PC는 UDP 9002로 `CmdPacket`을 보낼 수 있다. `pc_link_thread()`는 최초 PC 주소를 학습한 뒤, 1초마다 모든 robot의 `PcStatusPacketV2`를 해당 PC 주소로 송신한다.

## 5. 공유 메모리 ABI

공유 메모리 핵심 구조는 `SharedData`다.

주요 구성:

- 이미지: `ImgSlot img_slots[3]`, `img_ready_idx`, `img_sem`
- LiDAR: `LidarSlot lidar_slots[3]`, `lidar_ready_idx`, `lidar_sem`
- Odom: `pthread_rwlock_t odom_lock`과 위치/속도 필드
- Meta: 연결 상태, drop count, packet count
- State: 통합 robot 상태 snapshot
- EventLog: ring buffer 방식 event log
- Metrics: rx/tx/ack/retry/drop counters
- Command Queue: process-shared mutex 기반 `ShmCmdQueue`

Android와 Linux의 차이는 `BridgeSignal`과 공유 메모리 생성 방식에서 흡수한다.

| 구분 | Linux | Android |
| --- | --- | --- |
| 공유 메모리 생성 | `shm_open`, `ftruncate`, `mmap` | `ASharedMemory_create`, `ASharedMemory_setProt`, `mmap` |
| 공유 메모리 fd 전달 | POSIX shm name 기반 가능 | Binder로 `ParcelFileDescriptor` 전달 |
| frame notification | `sem_t` | `pthread_mutex_t + pthread_cond_t + generation` |
| thread affinity/RT priority | 사용 시도 | 비활성화 |

`SHM_MAGIC`, `SHM_VERSION`, `shm_header_size`, `shared_data_size`가 있으므로 Android 클라이언트는 mmap 후 ABI 검증을 먼저 수행해야 한다.

## 6. Android 포팅 상태

현재 Android 포팅은 "네이티브 미들웨어를 Android 프로세스 안에서 구동하고 공유 메모리 fd를 Binder로 전달하는 골격"까지 구현되어 있다.

구현된 항목:

- CMake의 `ANDROID` 분기에서 `bridge_middleware` shared library 생성
- Android에서 `ASharedMemory` 기반 region 생성
- Android에서 `sem_t` 대신 process-shared pthread mutex/condvar 신호 사용
- JNI `nativeStart`, `nativeStop`, `nativeDupSharedMemoryFd`
- `BridgeMiddlewareService` Java wrapper
- `IBridgeMiddleware.aidl` Binder 인터페이스
- Manifest의 service 선언 및 `INTERNET`, `FOREGROUND_SERVICE` 권한 선언

부족하거나 확인이 필요한 항목:

- `build.gradle`, `settings.gradle`, Android Gradle Plugin 설정이 없다.
- `externalNativeBuild.cmake.path`와 ABI/minSdk 설정이 아직 없다.
- foreground service 권한은 선언되어 있지만 `startForeground()` 호출과 notification channel 구현은 없다.
- Android 8.0+ 백그라운드 서비스 제한을 고려한 시작 방식이 필요하다.
- Android 12+에서는 foreground service type 선언 필요 여부를 앱 타깃 SDK 기준으로 검토해야 한다.
- Android client가 `ParcelFileDescriptor`를 mmap하고 `SharedData` ABI를 읽는 구현은 없다.
- Android Java/Kotlin에서 `pthread_cond_t`를 직접 wait할 수 없으므로, reader 측도 네이티브 helper 또는 polling 전략이 필요하다.
- command queue producer API가 Android 앱 쪽에 아직 노출되어 있지 않다.

## 7. 빌드 분석

### Linux

`Makefile`과 CMake 모두 Linux executable 빌드를 지원한다.

- `make`: `bridge_daemon` 생성
- `make asan`: AddressSanitizer 빌드
- `make tsan`: ThreadSanitizer 빌드
- `cmake -S . -B build_linux && cmake --build build_linux`

분석 시점에 `make`는 `bridge_daemon`이 최신 상태임을 확인했다.

### Android

CMake Android 분기는 다음 library를 만든다.

- target: `bridge_middleware`
- type: shared library
- Android libs: `android`, `log`
- common sources: Linux daemon과 동일
- 추가 source: `bridge_android_jni.c`

`ASharedMemory` 사용 때문에 최소 API level은 26 이상으로 잡는 것이 맞다.

예상 Gradle 설정 방향:

- Android library 또는 app module 생성
- `externalNativeBuild.cmake.path = file("../../CMakeLists.txt")`처럼 현재 CMake 연결
- `minSdk >= 26`
- 필요한 ABI만 `abiFilters`로 제한
- Java/AIDL source set에 `android/src/main` 포함

## 8. 주요 위험 요소

### 8.1 Android foreground service 미완성

Manifest에는 `FOREGROUND_SERVICE` 권한이 있지만 `BridgeMiddlewareService`는 일반 started service처럼 동작한다. 장시간 네트워크/로봇 제어 미들웨어로 사용하려면 Android 버전에 따라 백그라운드 실행이 중단될 수 있다.

권장 조치:

- `startForegroundService()`로 시작
- `onStartCommand()` 초기에 `startForeground()` 호출
- notification channel 생성
- target SDK에 맞는 foreground service type 검토

### 8.2 Binder fd 전달 후 reader lifecycle

`nativeDupSharedMemoryFd()`는 fd를 `dup()`해서 Binder로 넘긴다. 이 방식은 적절하지만, service가 종료되어 원본 fd/mmap을 닫으면 client mmap이 어떤 lifecycle을 가져야 하는지 명확한 정책이 필요하다.

권장 조치:

- service restart 시 client가 fd를 다시 요청하도록 계약 정의
- `SHM_VERSION`/`shared_data_size` mismatch 처리
- Binder death 또는 service reconnect 흐름 구현

### 8.3 Android reader에서 condition wait 문제

`BridgeSignal`이 Android에서 pthread condvar로 정의되어 있다. Java/Kotlin만으로는 이 condvar를 직접 기다릴 수 없다.

선택지는 두 가지다.

- reader 전용 JNI helper를 만들어 `bridge_signal_timedwait()` 제공
- Java/Kotlin은 `ready_idx`와 `state.seq`를 polling하고 알림은 포기

저지연 이미지/LiDAR가 필요하면 JNI helper가 더 적합하다.

### 8.4 command queue producer 부재

`jetson_tx`는 `SharedData.cmd_queue`를 소비하지만, Android 쪽에서 이 queue에 명령을 넣는 API가 없다. 현재 AIDL은 shared memory fd 조회만 제공한다.

권장 조치:

- AIDL에 `sendCommand(...)` 추가 후 service native 함수에서 queue push
- 또는 client가 shared memory를 mmap해 직접 queue에 쓰도록 하되 ABI와 lock 사용 규칙을 별도 SDK로 제공

서비스 중앙집중형 `sendCommand()`가 더 안전하다. client가 직접 `pthread_mutex_t`가 포함된 C 구조체를 Java에서 조작하는 방식은 유지보수 위험이 크다.

### 8.5 protocol_timer의 불필요한 주소 snapshot

`protocol_timer.c`는 `snap_addr`를 복사하지만 실제 송신은 `bridge_api_send_command()`가 다시 addr table을 lookup한다. 현재 기능상 문제는 아니지만 코드 의도가 흐려진다.

개선 방향:

- snapshot을 제거하고 단순히 `addr_table->set` 여부만 판단
- 또는 `bridge_api_send_command_to_addr()` 같은 내부 API로 snapshot 주소를 실제 사용

### 8.6 FPS 계산의 metric 필드 재사용

`bridge_api_note_frame_ready()`에서 image FPS 계산에 `metrics.last_rx_us`, LiDAR FPS 계산에 `metrics.last_ack_us`를 재사용한다. 이 필드들은 이름상 packet receive 및 ACK timestamp 의미라서 혼동과 통계 오염 가능성이 있다.

권장 조치:

- `last_image_frame_us`, `last_lidar_frame_us` 필드를 별도로 추가
- ABI version을 올려 Android/Qt reader와 동기화

### 8.7 fragment queue overflow drop visibility

`FragQueue`가 가득 차면 가장 오래된 fragment를 덮어쓴다. 이 drop은 즉시 metrics에 반영되지 않고, 이후 reassembly timeout이나 frame 불완성으로 간접 반영될 수 있다.

권장 조치:

- `frag_queue_push()`가 drop 여부를 return하도록 변경
- `jetson_rx`에서 `bridge_api_note_drop()` 호출

## 9. 포팅 우선순위 제안

1. Android Gradle module 구성
2. `bridge_middleware` Android 빌드 검증
3. foreground service 완성
4. AIDL command API 추가
5. Android client mmap reader helper 구현
6. SharedData ABI 검증/버전 관리 문서화
7. Android 실기기 네트워크 권한, multicast/broadcast 필요 여부, 배터리 최적화 예외 정책 검토
8. fragment drop metrics와 FPS metric 개선

## 10. 결론

`bride_app_android`는 기존 Linux BridgeDaemon의 핵심 네트워크/공유메모리 로직을 최대한 유지하면서 Android 포팅을 시작한 상태다. 네이티브 계층의 OS 차이 추상화는 방향이 좋고, `ASharedMemory + Binder fd 전달` 구조도 Android 프로세스 간 공유 메모리 방식으로 적절하다.

다만 현재 상태는 Android 앱에 바로 통합 가능한 완성형 SDK라기보다, 네이티브 미들웨어를 Android 서비스로 올리기 위한 1차 골격에 가깝다. 실제 포팅 완료를 위해서는 Gradle 빌드 체계, foreground service lifecycle, command 송신 API, client-side mmap/notification helper가 추가로 필요하다.
