# 2026-05-23 DisasterControlQt Dynamic 10-Robot UI Plan

## 목적

`disaster_control_qt` 앱을 최대 10대 로봇까지 관제할 수 있게 만들기 위한 구현 전 분석 문서다.

현재 결론은 다음과 같다.

- 브릿지/SHM 백엔드는 10대까지 설계되어 있다.
- Qt 데이터 수집부도 `kMaxRobots = 10` 기준으로 10대까지 worker/snapshot을 만들 수 있다.
- 실제 문제는 프론트엔드 UI가 4대 고정으로 생성되고, 맵 렌더링도 `id < 4`로 제한한다는 점이다.

따라서 다음 작업은 백엔드 프로토콜 변경이 아니라, `disaster_control_qt`의 UI 동적 생성/삭제와 관제 화면 UX 재구성이다.

## 현재 코드 기준

### 10대까지 가능한 부분

- `disaster_control_qt/src/shmtypes.h`
  - `constexpr int kMaxRobots = 10;`
- `disaster_control_qt/src/shmmonitor.cpp`
  - `ShmMonitor::start(int maxRobots)`가 `std::clamp(maxRobots, 1, kMaxRobots)` 후 로봇 수만큼 `RobotWorkerThread`를 만든다.
  - `m_snapshots.resize(m_maxRobots)`로 snapshot 배열도 동적 크기다.
- `disaster_control_qt/src/robotshmconnection.cpp`
  - `/robot_bridge_%1` 이름으로 robot id별 SHM을 열어 읽는다.
  - `sendCommand()`도 `m_robotId < kMaxRobots` 범위까지 허용한다.

### 4대 고정인 부분

- `disaster_control_qt/src/mainwindow.cpp`
  - `kDefaultRobotCount = 4`
  - `kSelectableRobotCount = 4`
  - 비디오 타일 생성 루프가 4대 기준이다.
  - 대시보드 로봇 상태 row 생성 루프가 4대 기준이다.
  - 로봇 상태 페이지의 `RobotStatusCard` 생성 루프가 4대 기준이다.
  - 제어 로봇 selector가 앞 4대까지만 표시한다.
  - footer의 "총 로봇" 값도 4로 고정된다.

- `disaster_control_qt/src/dashboardwidgets.cpp`
  - `isMapRobotId(int id)`가 `id >= 0 && id < 4`만 true로 처리한다.
  - 2D/3D 맵의 로봇 마커, 라벨, 경로 렌더링이 이 필터를 통과한 로봇만 처리한다.

## 브릿지 상태

브릿지 앱은 코드상 10대까지 가능하다.

- `bride_app_codex_indexed/shm_def.h`
  - `#define MAX_ROBOTS 10`
- `bride_app_codex_indexed/bridge_main.c`
  - 실행 인자 `num_robots`를 받아 `1..MAX_ROBOTS` 범위를 검증한다.
  - `num_robots` 수만큼 `/robot_bridge_0`부터 `/robot_bridge_N-1`까지 SHM을 만든다.
  - `num_robots` 수만큼 reassembly thread를 생성한다.
- `bride_app_codex_indexed/jetson_rx.c`
  - 수신 패킷의 `robot_id`가 `ctx->num_robots` 이상이면 버린다.

주의할 점:

- 브릿지를 `./bridge_daemon 4`로 실행하면 Qt를 10대로 바꿔도 `/robot_bridge_4..9`는 없다.
- 브릿지를 `./bridge_daemon 10`으로 실행하면 Qt는 10개 SHM을 감시할 수 있다.
- "동적 로봇 추가/삭제"를 브릿지 런타임 hot-add/hot-remove까지 의미하면 브릿지 구조 변경이 필요하다.
- 이번 목표는 우선 "최대 10대까지 표현 가능한 Qt 관제 UI"로 보는 것이 안전하다.

## 구현 목표 정의

이번 단계의 목표:

- Qt 앱이 최대 10대 snapshot을 화면에 표현한다.
- 설정 화면에서 감시 로봇 수를 바꾸면 카메라/상태/제어/맵 UI가 그 수에 맞게 재구성된다.
- 로봇 수를 줄였을 때 선택된 로봇, fullscreen camera, 맵 선택 상태가 깨지지 않는다.
- 브릿지가 해당 SHM을 만들지 않은 로봇은 "미연결/대기" 상태로 안정적으로 표시된다.

이번 단계에서 하지 않는 것이 좋은 것:

- 브릿지의 런타임 hot-add/hot-remove 구현.
- 프로토콜 변경.
- SHM layout 변경.
- 로봇별 상세 권한/임무 배정 로직 추가.

## 관제 시스템 UX 고려사항

10대 관제는 단순히 화면에 10개 카드를 나열하는 문제가 아니다. 운영자가 빠르게 이상 로봇을 찾고, 선택하고, 조치해야 한다.

### 화면 밀도

현재 4대 기준 2x2 비디오 그리드는 10대에서 그대로 쓰기 어렵다.

권장:

- 대시보드의 실시간 영상은 스크롤 가능한 grid로 전환한다.
- 10대 기준 5x2 또는 반응형 3열/4열 grid를 사용한다.
- 각 카메라 타일은 고정 최소 크기를 갖고, 화면이 부족하면 `QScrollArea`로 스크롤한다.
- 대시보드 첫 화면에서는 모든 카메라를 동일 크기로 보여주는 것보다, 연결/경고/선택 로봇 우선순위를 줄 수도 있다.

첫 구현 권장안:

- 2열 고정 또는 너비 기반 3열 grid.
- 스크롤 허용.
- 기존 `VideoTile` 재사용.
- fullscreen camera는 선택된 로봇 1대만 크게 표시.

### 로봇 상태 목록

상태 row 10개는 대시보드 하단 패널에 모두 넣으면 높이가 부족할 가능성이 높다.

권장:

- 대시보드의 compact 상태 목록은 `QScrollArea` 안에 row를 넣는다.
- row는 10대까지 한눈에 스캔 가능하도록 높이를 작게 유지한다.
- 정렬은 기본 robot id 순서로 시작한다.
- 나중에 위험/오프라인 우선 정렬을 추가할 수 있다.

### 로봇 상태 상세 페이지

현재 `RobotStatusCard`는 카드 크기가 커서 10개를 한 화면에 넣기 어렵다.

권장:

- 상세 페이지도 `QScrollArea` + grid layout으로 바꾼다.
- desktop 기준 2열 또는 3열.
- 카드가 너무 커지면 10대에서 스크롤이 과도해진다. 첫 구현은 기존 카드 크기를 유지하되 스크롤을 허용한다.
- 추후 compact card/detail drawer 구조로 개선할 수 있다.

### 제어 로봇 선택

현재 selector는 4대 제한이다.

권장:

- selector에는 현재 snapshot 크기만큼 전체 robot id를 넣는다.
- popup은 스크롤을 허용한다.
- "제어 가능" 여부는 연결 상태와 분리해서 표시한다.
- SHM이 없거나 오프라인인 로봇도 선택은 가능하게 할지 결정해야 한다.

첫 구현 권장안:

- 전체 감시 로봇 수만큼 selector 표시.
- 미연결 로봇도 표시하되 명령 전송 실패 메시지는 현재 로직 유지.
- 나중에 offline 항목 disabled 처리 가능.

### 위험한 제어 상태

10대에서는 실수로 잘못된 로봇에 명령을 보내는 위험이 커진다.

최소 보완:

- `m_selectedRobot`가 로봇 수 변경 후 범위 밖이면 0 또는 마지막 로봇으로 보정한다.
- fullscreen camera, victim alert, command send가 모두 유효 robot id를 재검증한다.
- 선택 로봇 이름이 제어 패널에 항상 명확히 보이게 유지한다.

추후 보완:

- 긴급정지 전 선택 로봇 확인 또는 전체 긴급정지와 개별 긴급정지를 분리.
- selector에 robot id뿐 아니라 온라인/오프라인 상태 표시.

## 맵 마커 고려사항

현재 맵은 `isMapRobotId(id)`에서 4대만 통과한다.

### 해야 할 변경

- `isMapRobotId()`를 `id >= 0 && id < kMaxRobots` 또는 `id >= 0` 기준으로 바꾼다.
- 2D/3D 모두 이 helper를 공유하므로 한 곳 변경으로 마커/라벨/경로 필터가 풀릴 가능성이 높다.

### 시작점 문제

`startPointForRobot(robotId)`는 map config의 starts를 사용한다.

현재 동작:

- `markerStartRobotId(0)`은 1
- `markerStartRobotId(1)`은 0
- 나머지는 자기 id
- starts 개수를 초과하면 `starts.first()`를 반환한다.

문제:

- map yaml에 start point가 4개만 있으면 5~10번 로봇의 초기 마커가 같은 위치에 겹칠 수 있다.
- 실제 odom이 들어오고 맵 안 좌표라면 raw 좌표를 쓰므로 덜 문제된다.
- SHM은 열렸지만 위치가 아직 유효하지 않을 때는 겹침이 눈에 띌 수 있다.

권장:

- 우선 `isMapRobotId()` 제한을 10대로 풀어 마커 표현을 가능하게 한다.
- starts가 부족할 때는 `starts.first()` 그대로 쓰지 말고 작은 offset을 추가해 겹침을 줄인다.
- 더 좋은 방법은 `map_data/map.yaml`에 10대 start point를 명시하는 것이다.

### 색상 문제

`robotPathColor(id)`는 id별 색상을 반환한다. 10대까지 색이 충분히 구분되는지 확인해야 한다.

확인할 것:

- 0~9 id가 모두 시각적으로 구분되는지.
- 선택 로봇 강조와 일반 로봇 표시가 겹치지 않는지.
- 3D 마커 label `A01..A10`이 겹치지 않는지.

## 코드 구조 변경 제안

### MainWindow 멤버 추가

`mainwindow.h`에 다음 멤버를 추가하는 방향이 좋다.

```cpp
int m_robotCount = kDefaultRobotCount;
QGridLayout *m_robotCardGrid = nullptr;
QScrollArea *m_videoScroll = nullptr;
QScrollArea *m_statusScroll = nullptr;
QScrollArea *m_robotCardScroll = nullptr;
```

필요 함수:

```cpp
void setRobotCount(int count);
void syncRobotUi(int count);
void syncVideoTiles(int count);
void syncStatusRows(int count);
void syncRobotStatusCards(int count);
void relayoutVideoTiles();
void relayoutRobotStatusCards();
void clampSelectedRobot();
```

### 설정 적용 흐름

현재:

```cpp
m_monitor.start(maxRobots->value());
showPage(0);
```

권장:

```cpp
setRobotCount(maxRobots->value());
showPage(0);
```

`setRobotCount()` 내부:

1. count를 `1..kMaxRobots`로 clamp.
2. `m_robotCount` 갱신.
3. `syncRobotUi(m_robotCount)` 호출.
4. `clampSelectedRobot()` 호출.
5. `m_monitor.start(m_robotCount)` 호출.
6. `refreshRobotSelector()` 호출.
7. `refreshRobotList()` 호출.
8. `m_map->setSnapshots(m_snapshots)` 호출.

주의:

- `m_monitor.start()`는 즉시 빈 snapshot 배열을 emit한다.
- UI sync를 먼저 할지 monitor start를 먼저 할지는 crash 방지를 기준으로 정해야 한다.
- 권장 순서는 UI sync 후 monitor start다. snapshot이 들어와도 받을 위젯이 준비되어 있기 때문이다.

### updateSnapshots 변경

현재는 snapshot id가 widget vector size 안에 있을 때만 업데이트한다.

동적 UI에서는 다음을 보장해야 한다.

- `m_snapshots = snapshots;`
- `syncRobotUi(snapshots.size())`를 호출할지 여부는 신중히 결정한다.

권장:

- UI 크기는 사용자가 설정한 `m_robotCount`를 기준으로 유지한다.
- snapshot 크기가 달라졌을 때만 보정한다.
- 일반적인 update마다 layout을 재구성하면 성능과 깜빡임 문제가 생긴다.

### refreshRobotSelector 변경

현재:

```cpp
for (int i = 0; i < m_snapshots.size() && i < kSelectableRobotCount; ++i)
```

권장:

```cpp
for (int i = 0; i < m_snapshots.size(); ++i)
```

또는:

```cpp
for (int i = 0; i < m_robotCount; ++i)
```

첫 구현은 `m_snapshots.size()`가 안전하다. `ShmMonitor::start(count)` 직후 snapshot size가 count로 맞춰지기 때문이다.

### footer/metric 변경

- "총 로봇: 4" 고정 라벨은 멤버로 바꾸거나 매번 생성하지 말고 업데이트 가능하게 만든다.
- 연결 로봇 metric은 이미 `connected / m_snapshots.size()` 형태라 동작한다.
- 초기 표시값 중 "0 / 4" 같은 하드코딩 텍스트가 있는지 추가 검색 후 수정한다.

## 구현 순서 제안

### 1단계: UI 동적 생성 기반

- `m_robotCount` 추가.
- 설정 적용을 `setRobotCount()`로 변경.
- `syncVideoTiles()`, `syncStatusRows()`, `syncRobotStatusCards()` 추가.
- 기존 4개 생성 루프를 sync 함수 호출로 대체.

완료 기준:

- 기본 4대 화면이 기존처럼 보인다.
- 설정에서 10으로 바꾸면 비디오/상태/카드가 10개 생긴다.
- 다시 4로 줄이면 4개만 남고 crash가 없다.

### 2단계: 스크롤/레이아웃 안정화

- 카메라 영역을 `QScrollArea`로 감싼다.
- 로봇 상태 목록을 `QScrollArea`로 감싼다.
- 로봇 상세 카드 페이지를 `QScrollArea`로 감싼다.
- grid column 계산을 화면 폭 또는 고정 값으로 정한다.

완료 기준:

- 10대에서도 주요 패널이 서로 겹치지 않는다.
- 창 크기를 줄여도 UI가 깨지지 않는다.

### 3단계: selector/control 안전성

- selector 10대 표시.
- `clampSelectedRobot()` 구현.
- fullscreen camera가 삭제된 robot id를 참조하지 않게 처리.
- victim alert의 robot id 유효성 재검증.

완료 기준:

- 10번 로봇 선택 후 command send 시 올바른 `/robot_bridge_9`에 쓰기를 시도한다.
- 10대에서 4대로 줄인 뒤 command/camera 동작이 crash 없이 보정된다.

### 4단계: 맵 10대 표시

- `isMapRobotId()` 제한을 `kMaxRobots` 기준으로 변경.
- starts 부족 시 offset fallback을 추가할지 결정.
- 2D/3D에서 A01..A10 label, marker, path가 보이는지 확인.

완료 기준:

- 5~10번 snapshot이 들어오면 맵에 마커가 표시된다.
- 선택 로봇이 5~10번이어도 2D/3D 전환과 fullscreen map에서 crash가 없다.

### 5단계: 검증

가능한 검증 방법:

- 브릿지를 10대로 실행: `./bridge_daemon 10`
- `/dev/shm/robot_bridge_0..9` 생성 확인.
- Qt 앱 실행 후 설정에서 10대 감시.
- 실제 Jetson 없이도 SHM open 상태가 10개까지 표시되는지 확인.
- 가능하면 테스트용 SHM writer 또는 기존 bridge test 도구로 5~10번 robot id의 odom/image/lidar 일부를 넣어 확인.

## 주요 리스크

### 성능

10대에서 각 로봇마다 worker thread가 1개씩 돈다. 현재 구조상 최대 10개 worker는 허용 범위로 보이나, 이미지와 LiDAR가 모두 고속으로 들어오면 UI 업데이트 비용이 커질 수 있다.

완화:

- `ShmMonitor`는 이미 33ms UI flush timer로 묶고 있다.
- UI widget 재배치는 snapshot update마다 하지 않아야 한다.
- 이미지 scaling 비용이 높으면 tile visibility 또는 selected/visible 우선 처리 필요.

### 화면 과밀

10대 카메라와 10대 상태카드를 한 화면에 모두 노출하면 관제성이 오히려 나빠질 수 있다.

완화:

- 첫 구현은 스크롤로 기능 완성.
- 이후 연결/위험/선택 로봇 우선 정렬 또는 필터 추가.

### 로봇 수 변경 중 이벤트

`m_monitor.start()`는 기존 worker를 stop하고 새 worker를 만든다. 이 사이에 UI가 이전 snapshot 또는 이전 selected id를 볼 수 있다.

완화:

- `setRobotCount()`에서 selected id를 먼저 보정.
- `updateSnapshots()`에서 id 범위 검사 유지.
- fullscreen camera의 id 범위 검사 강화.

### 맵 start point 부족

맵 설정의 start point가 10개보다 적으면 초기 마커가 겹친다.

완화:

- fallback offset 적용.
- 장기적으로 map yaml에 10개 start point 추가.

## 다음 작업 체크리스트

- [x] `MainWindow`에 `m_robotCount`와 동적 layout 멤버 추가.
- [x] 4대 고정 생성 루프를 sync 함수로 대체.
- [x] 카메라/상태/카드 영역에 scroll 적용.
- [x] selector의 4대 제한 제거.
- [x] selected robot 보정 로직 추가.
- [x] footer/label의 4대 고정 텍스트 제거.
- [x] `dashboardwidgets.cpp::isMapRobotId()` 10대 기준으로 변경.
- [x] map start fallback 정책 결정 및 구현.
- [ ] 4대 기본 화면 regression 확인.
- [ ] 10대 설정 화면 확인.
- [x] 브릿지 10대 실행과 SHM 확인.

## 권장 구현 범위

다음 구현 턴에서는 다음 범위까지 한 번에 처리하는 것이 좋다.

1. `MainWindow` UI 동적화.
2. selector 10대화.
3. 맵 id 제한 10대화.
4. 기본 빌드 확인.

브릿지 hot-add/hot-remove는 별도 작업으로 남기는 것이 좋다. 현재 목표인 "10대까지 관제 화면에 표현"에는 필요하지 않고, 브릿지 thread/SHM lifecycle까지 건드리면 리스크가 크게 늘어난다.

## 2026-05-23 1차 구현 결과

완료한 코드 변경:

- `disaster_control_qt/src/mainwindow.h`
  - `m_robotCount` 추가.
  - 동적 UI sync 함수 선언 추가.
  - 카메라/상태/카드용 scroll/layout 멤버 추가.
  - 로봇 총수 footer label 멤버 추가.

- `disaster_control_qt/src/mainwindow.cpp`
  - 카메라 타일, 상태 row, 로봇 상태 카드를 `m_robotCount` 기준으로 생성/삭제하도록 변경.
  - 카메라/상태/카드 영역을 `QScrollArea`로 감쌈.
  - 기본 감시 로봇 수를 10대로 변경해 앱 시작 직후 10대 UI가 보이도록 조정.
  - `DISASTER_QT_DEMO_ROBOTS=1..10` 환경변수 기반 UI 테스트 모드 추가.
  - 테스트 모드에서는 실제 SHM 없이 가짜 snapshot, 카메라 이미지, 상태값, 맵 좌표를 생성한다.
  - 테스트 모드의 명령 버튼은 실제 SHM에 쓰지 않고 이벤트 로그만 남긴다.
  - 설정 화면 적용 버튼이 `setRobotCount()`를 호출하도록 변경.
  - selector가 snapshot 전체를 표시하도록 변경.
  - 선택 로봇 범위 보정 추가.
  - 10대에서 4대로 줄였을 때 grid row stretch가 남아 빈 공간을 만들지 않도록 row stretch reset 추가.

- `disaster_control_qt/src/dashboardwidgets.cpp`
  - `isMapRobotId()`를 `id < 4`에서 `id < kMaxRobots`로 변경.
  - map start point가 부족할 때 시작점이 완전히 겹치지 않도록 fallback offset 추가.

검증 결과:

- `cmake --build disaster_control_qt/build_codex` 성공.
- `QT_QPA_PLATFORM=offscreen` smoke 실행에서 즉시 크래시 없음. 단, offscreen은 `QOpenGLWidget`을 지원하지 않아 3D 시각 검증 용도로는 부적합하다.
- Wayland 환경에서 `timeout 8s`로 실제 앱 실행 시 즉시 크래시 없이 유지 후 timeout 종료됨.
- `DISASTER_QT_DEMO_ROBOTS=3`, `DISASTER_QT_DEMO_ROBOTS=10` offscreen smoke 실행 확인.
- Wayland 환경에서 `DISASTER_QT_DEMO_ROBOTS=10` 실행 시 즉시 크래시 없이 timeout 종료 확인.
- `bride_app_codex_indexed/bridge_daemon 10` 실행 시 `/robot_bridge_0`부터 `/robot_bridge_9`까지 SHM 초기화 로그와 robot 0~9 watchdog 로그 확인.

남은 검증:

- 실제 화면에서 설정을 10대로 변경했을 때 카메라 grid, 상태 row, 상태 카드 scroll이 보기 좋게 동작하는지 확인.
- 10대에서 다시 4대로 줄였을 때 빈 row/빈 카드 공간이 남지 않는지 확인.
- 실제 odom 또는 테스트 writer로 robot id 4~9 snapshot을 넣었을 때 맵 2D/3D 마커와 라벨이 표시되는지 확인.
- map yaml에 start point가 10개보다 적은 상태에서 fallback offset이 운영자가 이해할 수 있는 위치인지 확인.
