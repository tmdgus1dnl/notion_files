# Android Native 관제 앱 구현 계획

## 목적

현재 Qt 기반 관제 앱과 BridgeDaemon/ROS 연동을 먼저 완성한 뒤, 발표용 렌더링 품질을 높이기 위해 Kotlin + Jetpack Compose 기반 Android native 앱을 새로 구현한다.

이 문서는 Android native 버전의 구현 방향, 구조, 재사용 범위, 단계별 작업 계획을 정리한다.

## 전제

- 현재 Linux/RPi 구조의 Qt 앱은 기능 검증 기준 앱으로 유지한다.
- ROS 송신, UDP 패킷, 이벤트 타입, 명령 타입, 로봇 상태 모델은 현재 시스템에서 먼저 확정한다.
- Android native 앱은 Qt UI를 그대로 포팅하지 않고, 검증된 기능을 Compose UI로 다시 구현한다.
- 발표용 앱이므로 극단적인 대규모 로봇 동시 처리보다 시각 품질, 안정성, 조작감, 디버깅 용이성을 우선한다.
- Android tablet 버전은 RPi를 중간 브릿지로 두는 구조가 아니라, 태블릿 앱이 로봇 UDP를 직접 송수신하는 단일 앱 구조를 기본으로 한다.

## 핵심 결정

### 1. 공유메모리는 Android native 앱에서 제거한다

현재 Linux 구조는 다음 흐름이다.

```text
BridgeDaemon
-> POSIX shared memory /robot_bridge_N
-> Qt app polling/rendering
```

Android 일반 앱에서는 기존 `shm_open`, `/dev/shm`, process-shared `pthread_rwlock`, `sem_t` 구조를 그대로 쓰는 방식이 적합하지 않다. Android에는 `ASharedMemory`, Binder fd passing, mmap 같은 대체 수단이 있지만, 발표용 단일 앱 구조에서는 복잡도 대비 이득이 작다.

Android native 버전에서는 공유메모리 계층을 없애고, 로봇 UDP 송수신/패킷 처리 기능을 앱 내부 native module로 포함한다.

### 2. 별도 BridgeDaemon/API 서버는 두지 않는다

태블릿 앱에서는 별도 `BridgeDaemon` 프로세스나 별도 `Bridge API` 서버를 두지 않는다.

```text
Linux/RPi 기준
Robot/Jetson -> RPi BridgeDaemon -> shared memory -> Qt App

Android tablet 기준
Robot/Jetson -> Android App 내부 Native Robot Link Core -> Compose UI
```

로봇과 태블릿 사이에는 UDP socket이 필요하지만, 앱 내부의 native module과 Compose UI 사이에는 socket API가 필요하지 않다. 같은 APK 안에서 JNI, Repository, StateFlow로 연결하는 편이 단순하고 빠르다.

### 3. 필요한 것은 외부 API가 아니라 내부 모듈 경계다

Android native 버전에서 필요한 계약은 네트워크 API가 아니라 다음 내부 경계다.

```text
Native Robot Link Core
-> JNI Adapter
-> Kotlin Repository
-> ViewModel / StateFlow
-> Compose UI
```

이 경계는 UI가 native thread를 직접 만지지 않도록 막고, 데이터 snapshot/event polling/command 호출을 안정적으로 분리하기 위한 것이다.

## 권장 구조

```text
Android APK
├── Kotlin + Jetpack Compose UI
│   ├── Dashboard
│   ├── Robot Status
│   ├── 2D Map
│   ├── 3D Map
│   ├── Camera Fullscreen
│   ├── Event Log
│   └── Command Panel
│
├── ViewModel / StateFlow
│   ├── RobotStateStore
│   ├── EventStore
│   ├── MapStateStore
│   └── CameraStateStore
│
├── JNI Adapter
│   ├── startRobotLink(config)
│   ├── stopRobotLink()
│   ├── getRobotSnapshot(robotId)
│   ├── pollEvents()
│   ├── getLatestImage(robotId)
│   ├── getLatestLidar(robotId)
│   └── sendCommand(cmd)
│
└── NDK Robot Link Core
    ├── UDP receive
    ├── packet parsing
    ├── image/lidar reassembly
    ├── robot state cache
    ├── event ring buffer
    ├── command sender
    └── watchdog/online 판단
```

## Native Robot Link Core 변경 방향

현재 `bride_app_codex_indexed`의 핵심 로직은 재사용하되, 공유메모리에 쓰는 부분을 앱 내부 상태 저장소로 바꾼다.

### 재사용 가능

- `proto.h`
- UDP packet header/payload 정의
- odom/path/event/cmd/cmd_ack 타입
- fragment reassembly 개념
- online/offline watchdog 개념
- command retry/ack 개념
- event severity/type/code 체계

### 변경 필요

- `SharedData` 직접 의존 제거
- POSIX shared memory 생성/삭제 제거
- `sem_t` 기반 frame notification 제거
- Qt polling을 위한 메모리 레이아웃 제거
- Android JNI로 넘기기 좋은 snapshot 구조 추가
- Kotlin에서 다루기 쉬운 DTO/data class 설계
- RPi/PC link 전용 상태 송신 제거
- 앱 내부 lifecycle에 맞는 start/stop 처리 추가

## Android Native 네트워크 구조

### 로봇 -> 태블릿

로봇/Jetson은 기존 `proto.h` 기반 UDP packet을 태블릿 IP로 직접 보낸다.

```text
Jetson/Robot
-> Tablet IP:9000
-> Android App UDP receiver
```

수신 대상:

- odom
- global path
- event
- image fragments
- lidar fragments
- command ack

### 태블릿 -> 로봇

Compose UI에서 발생한 명령은 Kotlin Repository를 거쳐 native core로 전달되고, native core가 Jetson command port로 UDP 송신한다.

```text
Compose command button
-> ViewModel
-> Repository
-> JNI sendCommand()
-> Native UDP sender
-> Jetson IP:9001
```

### Robot address 관리

초기 버전은 설정 기반으로 간다.

- robot count
- robot name
- robot id
- Jetson IP
- receive port
- command port

이후 필요하면 discovery/broadcast를 추가한다.

## Android Native에서 필요 없는 것

- POSIX shared memory
- Qt용 `RobotShmConnection`
- RPi BridgeDaemon 별도 실행
- Android 앱 내부 통신용 socket server
- Binder/ASharedMemory 기반 shared memory bridge
- PC link status packet 송신

단, Linux/RPi 버전의 BridgeDaemon은 현재 Qt 시스템 검증과 fallback 시연용으로 계속 유지한다.

## 데이터 흐름

### 로봇 상태

```text
Jetson/Robot UDP
-> NDK Robot Link Core
-> Native RobotStateCache
-> JNI snapshot
-> Kotlin Repository
-> StateFlow
-> Compose UI
```

### 이벤트

```text
Robot EVENT packet
-> Native event ring buffer
-> JNI pollEvents()
-> Kotlin EventStore
-> popup / event log / severity chart
```

### 카메라

초기 구현은 JPEG frame buffer 방식으로 시작한다.

```text
IMAGE fragments
-> Native reassembly
-> latest JPEG byte array
-> JNI getLatestImage()
-> Compose Image / Android Bitmap decode
```

성능이 부족하면 이후 `MediaCodec`, `SurfaceTexture`, H.264 스트림으로 전환한다.

### LiDAR / 2D Map

```text
LIDAR fragments
-> Native reassembly
-> point array snapshot
-> Kotlin map model
-> Compose Canvas
```

### 3D Map / Robot Preview

Compose 자체로 3D를 구현하지 않는다. Compose 화면 안에 Android 3D 렌더링 surface를 넣는다.

후보:

- Filament
- SceneView
- OpenGL ES custom renderer

권장:

- 로봇 상태 카드의 3D 프리뷰: Filament 또는 SceneView
- 3D map/point cloud: OpenGL ES custom renderer 또는 Filament
- 2D map: Compose Canvas

## UI 구현 방향

### Compose 담당

- 전체 레이아웃
- 사이드바
- 상단 상태바
- 로봇 카드
- 로그 테이블
- 상태 칩
- progress bar
- 필터/탭/버튼
- 이벤트 팝업
- 카메라 전체화면 전환

### Canvas 담당

- 2D map grid
- occupancy/path/waypoint
- robot pose
- path gradient highlight
- radar/cone effect
- sparkline/chart
- severity donut chart

### 3D Renderer 담당

- robot wireframe preview
- robot status color tint
- point cloud
- 3D path
- camera orbit/first-person view

## 화면 구성

### 1. 통합 관제 대시보드

- 전체 탐색 진행률
- 임무 수행 시간
- 연결 로봇 수
- 로봇 링크 상태
- 배터리/네트워크 요약
- 실시간 카메라 카드
- 2D/3D map panel
- 로봇 상태 요약
- 이벤트 로그
- 운용 제어 버튼

### 2. 로봇 상태

- 로봇별 카드 grid
- 배터리 progress
- mission progress
- current position / goal
- temperature/speed/network
- camera/lidar/motor component status
- 3D wireframe robot preview
- online/offline/warning 색상 표현

### 3. 로그

- severity summary
- search/filter
- info/warn/error filter
- event table
- event detail
- decorative chart 또는 실제 severity summary chart

### 4. 카메라

- multi-camera grid
- selected camera fullscreen
- victim event popup 진입
- 뒤로가기 시 이전 화면 복귀

### 5. 지도

- 2D map
- 3D map
- path display
- robot pose
- explored/unexplored area
- zoom/pan
- selected robot follow mode

## 단계별 작업 계획

### Phase 0. 현재 시스템 안정화

- Qt 앱에서 모든 기능을 먼저 검증한다.
- ROS -> BridgeDaemon -> Qt 흐름을 실제 로봇으로 테스트한다.
- event type, command type, packet payload를 확정한다.
- online/offline 판단 기준을 확정한다.
- 카메라/라이다/path/event/command ack의 정상/비정상 케이스를 기록한다.
- Android direct UDP 전환 시 필요한 로봇 측 목적지 IP/port 설정 방법을 확정한다.

완료 기준:

- Qt 앱으로 발표 가능한 수준의 end-to-end 동작 확보
- Android로 옮길 기능 목록 확정

### Phase 1. Android 프로젝트 골격

- Gradle Android app 생성
- Kotlin + Compose 설정
- NDK/CMake 연결
- JNI 기본 함수 연결
- 앱 권한 설정
  - INTERNET
  - ACCESS_NETWORK_STATE
  - WAKE_LOCK
  - 필요 시 CHANGE_WIFI_MULTICAST_STATE

완료 기준:

- Compose 화면에서 native robot link start/stop 호출 가능

### Phase 2. Native Robot Link Core 이식

- `proto.h` 이식
- UDP receive thread 구현
- odom/path/event/cmd_ack 처리
- Native state cache 구현
- event ring buffer 구현
- watchdog 구현
- command send 구현
- robot config 기반 Jetson 주소 관리 구현

완료 기준:

- Android 앱에서 로봇 online/offline, pose, path, event를 텍스트로 확인 가능

### Phase 3. Compose 상태 관리

- Repository
- ViewModel
- StateFlow
- RobotState data class
- Event data class
- Map data class
- command action model

완료 기준:

- native snapshot이 Compose UI state로 안정적으로 반영됨

### Phase 4. 주요 UI 구현

- Dashboard
- Robot status
- Event log
- Command panel
- Camera grid placeholder
- 2D map placeholder

완료 기준:

- 실제 데이터 기반으로 화면 카드/로그/상태가 갱신됨

### Phase 5. 2D Map 구현

- Compose Canvas 기반 grid
- robot pose
- path
- path highlight animation
- explored/unexplored area
- zoom/pan

완료 기준:

- 현재 Qt 2D map 기능을 Android에서 재현

### Phase 6. Camera 구현

- JPEG frame snapshot 방식 구현
- Bitmap decode 최적화
- selected camera fullscreen
- victim popup 후 뒤로가기 상태 복귀

완료 기준:

- 최소 1개 로봇 카메라를 Android 화면에서 실시간 확인

### Phase 7. 3D 구현

- Filament/SceneView/OpenGL 중 하나 선택
- 로봇 모델 로딩
- wireframe/status color material
- 3D map point cloud/path 표시
- 1인칭/관제 시점 전환

완료 기준:

- 로봇 상태 카드의 3D 프리뷰 표시
- 3D map에서 path/points가 시야 내에 보임

### Phase 8. 발표용 polish

- 애니메이션 정리
- 전환 효과
- 색상/typography/design token 정리
- tablet landscape 최적화
- fake/demo fallback mode 추가
- 네트워크 끊김/재연결 UI 정리

완료 기준:

- Galaxy Tab 실기에서 발표용 시나리오를 안정적으로 시연 가능

## 최소 기기 기준

권장 기준:

- Galaxy Tab S9 이상
- RAM 8GB 이상, 가능하면 12GB
- 120Hz AMOLED 모델 우선

추천:

- Galaxy Tab S10+ 12GB
- Galaxy Tab S9+ 12GB
- Galaxy Tab S9 12GB

FE 모델은 단순 UI는 가능하지만, 카메라 + 3D + map + native robot link 동시 실행의 여유가 작아 발표용 최종 타깃으로는 권장하지 않는다.

## 테스트 계획

### 단위 테스트

- packet header validation
- event code/type parsing
- command payload generation
- robot id mapping
- path count clipping

### 통합 테스트

- Android app start/stop 반복
- UDP packet burst
- malformed packet drop
- unsupported packet이 online 처리되지 않는지 확인
- 등록되지 않은 source IP 차단 여부 확인
- robot disconnect/reconnect
- victim detected/cleared event
- camera frame loss
- lidar frame loss
- command ack/timeout

### 렌더링 테스트

- tablet landscape
- phone landscape
- dark theme contrast
- 2D map zoom/pan
- 3D renderer blank frame 여부
- camera fullscreen/back stack
- long log list scrolling

## 주요 리스크

### 1. 3D renderer 복잡도

Compose만으로 해결하지 않고, Filament/SceneView/OpenGL을 별도 모듈로 분리한다.

### 2. 카메라 디코딩 부하

초기에는 JPEG frame으로 구현하고, 성능이 부족할 경우 H.264 + MediaCodec으로 전환한다.

### 3. JNI 데이터 복사 비용

상태/이벤트는 snapshot 복사로 충분하다. 이미지/LiDAR는 필요 시 direct buffer 또는 native-side cache id 방식으로 최적화한다.

### 4. 기존 Qt 기능 누락

Qt 앱을 기준 명세로 두고 화면별 체크리스트를 만들어 누락을 방지한다.

### 5. 태블릿 IP 변경

Jetson이 태블릿 IP로 직접 패킷을 보내야 하므로, 네트워크가 바뀌면 IP 설정 문제가 생길 수 있다. 발표용 초기 버전은 고정 AP/고정 IP로 시작하고, 이후 discovery 또는 설정 화면을 추가한다.

## 최종 방향

Android native 앱은 기존 Qt 앱의 단순 포팅이 아니다.

목표는 다음과 같다.

- 브릿지/ROS 연동은 현재 Qt/RPi 시스템에서 먼저 검증
- Android에서는 공유메모리 제거
- RPi BridgeDaemon/API 서버 없이 앱 내부 Native Robot Link Core가 로봇 UDP를 직접 처리
- Kotlin + Compose로 UI 재구현
- 2D는 Compose Canvas
- 3D는 Filament/SceneView/OpenGL
- 발표용으로 고급 렌더링과 안정적인 시연을 우선
