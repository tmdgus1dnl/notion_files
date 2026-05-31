# [Perception] LiDAR 인지 환경 별 검증 테스트

상태: Perception

## 상황별 검증 시나리오

각 노드가 바라보는 관점 : 

```
ObstacleModel      = 장애물 cluster가 어느 방향에 있는가?
LocalOccupancyGrid = LiDAR ray 기준으로 free / occupied / unknown cell이 어떻게 생겼는가?
FreeSpaceModel     = front / left / right 방향이 주행 가능 후보인가?
FreeSpaceMarker    = FreeSpaceModel 판단을 RViz에서 시각적으로 확인
```

즉, 상황별로 봐야 하는 데이터가 다르다.

---

## 공통으로 항상 켜져 있어야 하는 토픽

전체 파이프라인이 살아 있는지 확인하는 순서 : 

```
ros2 topic hz /scan_3D
ros2 topic hz /perception/lidar/points_filtered
ros2 topic hz /perception/lidar/obstacle_clusters
ros2 topic hz /perception/lidar/local_occupancy_grid
ros2 topic hz /perception/lidar/free_space_model
ros2 topic hz /perception/lidar/free_space_markers
```

정상 흐름 :

```
/scan_3D
  ↓
/perception/lidar/points_filtered
  ↓
/perception/lidar/obstacle_clusters
  ↓
/perception/lidar/obstacle_model

/points_filtered
  ↓
/perception/lidar/local_occupancy_grid
  ↓
/perception/lidar/free_space_model
  ↓
/perception/lidar/free_space_markers
```

하나라도 안 나오면 검증 할 수 없음

---

### 상황 A: 전방에 장애물이 있는 경우

> 예: 사람이나 박스가 Spot 정면 약 0.4~0.8m 앞에 있음.
> 

#### 확인할 노드 1: `obstacle_model_node`

확인 명령:

```
ros2 topic echo /perception/lidar/obstacle_model --once
```

봐야 하는 값:

```
front.valid
front.nearest
front.azimuth
obstacle_detected
```

기대값:

```
front valid=true
front nearest=0.4~0.8m 근처
front azimuth=-20~20도 근처
obstacle_detected=true
```

해석:

```
전방 sector에 대표 장애물 cluster가 잡혔다.
```

현재 네 로그에서 이런 식으로 나오고 있었지.

```
front | valid=true | nearest=0.53~0.56 | azimuth=-19도
```

이건 전방 장애물 감지가 된 상태야.

---

#### 확인할 노드 2: `local_occupancy_grid_node`

확인할 로그:

```
input points
valid points
free cells
occupied cells
```

기대값:

```
occupied cells 증가
free cells도 어느 정도 존재
```

RViz에서 봐야 하는 것:

```
전방 쪽 검은색 occupied cell이 생기는지
LiDAR point cloud가 해당 장애물에 찍히는지
```

---

#### 확인할 노드 3: `free_space_model_node`

확인 명령:

```
ros2 topicecho /perception/lidar/free_space_model--once
```

봐야 하는 값:

```
front_blocked
front_clearance
front_occupied_ratio
front_unknown_ratio
front_free
blocked_state
```

기대값:

```
front_blocked=true
front_clearance가 장애물 거리 근처로 감소
front_occupied_ratio 증가
front_free=false
```

예상 로그:

```
front: free=0 blocked=1 clearance=0.45 unknown=... occupied=...
```

해석:

```
전방은 장애물 때문에 주행 가능 후보가 아니다.
```

---

#### **[상황 A에서 정상 판정]**

```
ObstacleModel:
  front obstacle valid=true

FreeSpaceModel:
  front_blocked=true
  front_clearance 감소

RViz:
  전방 sector에 occupied grid와 point cloud가 보임
```

이 3개가 일치하면 전방 장애물 판단은 정상이다.

### 상황 B: 전방이 비어 있는 경우

> 예: LiDAR 정면 1~1.5m 안에 큰 장애물이 없음.
> 
> 
> 이 상황이 제일 중요함. 왜냐하면 지금 FreeSpaceModel이 항상 `UNKNOWN`으로 빨려 들어가는 문제가 있기 때문
> 

#### 확인할 노드 1: `obstacle_model_node`

봐야 하는 값:

```
front.valid
front.nearest
obstacle_detected
```

기대값:

```
front valid=false
또는 front nearest가 멀어짐
obstacle_detected=false 또는 front obstacle 없음
```

단, 좌/우에 물체가 있으면 `obstacle_detected=true`일 수 있다. 중요한 건 **front가 사라지는지** 확인하기

#### 확인할 노드 2: `local_occupancy_grid_node`

봐야 하는 값:

```
free cells
occupied cells
```

기대값:

```
occupied cells 감소
free cells 증가 또는 유지
```

RViz에서 봐야 하는 것:

```
전방 검은 occupied cell이 줄어드는지
전방 쪽 free 영역이 보이는지
```

#### 확인할 노드 3: `free_space_model_node`

봐야 하는 값:

```
front_free
front_blocked
front_clearance
front_free_ratio
front_occupied_ratio
front_unknown_ratio
blocked_state
```

가장 중요한 기대 변화:

```
front_clearance 증가
front_occupied_ratio 감소
front_blocked=false로 바뀔 가능성
front_free_ratio 증가
```

완벽하게 `front_free=1`이 안 나와도, 최소한 아래처럼 변화가 있어야 함함.

```
장애물 있을 때:
  front clearance=0.35~0.5
  front occupied 높음

전방 비웠을 때:
  front clearance 증가
  front occupied 감소
```

만약 전방을 비워도 계속:

```
front free=0
front blocked=1
front unknown=0.85 이상
state=8
```

이면 **1) FreeSpaceModel 파라미터가 너무 보수적**이거나, 2) **grid/check range가 센서 특성과 안 맞는 거**

---

#### [상황 B에서 정상 판정]

```
ObstacleModel:
  front valid=false 또는 nearest 증가

LocalOccupancyGrid:
  전방 occupied cell 감소

FreeSpaceModel:
  front clearance 증가
  front occupied_ratio 감소
  front_blocked가 완화되는 방향
```

이렇게 반응하면 MVP는 정상이다.

### 상황 C: 좌측에 장애물이 있는 경우

> 예: Spot 기준 왼쪽 20~60도 영역에 물체를 둠.
> 

#### 확인할 노드 1: `obstacle_model_node`

봐야 하는 값:

```
left.valid
left.nearest
left.azimuth
```

기대값:

```
left valid=true
left nearest=물체 거리 근처
left azimuth=20~60도 사이
```

예상 로그:

```
left | valid=true | nearest=0.55 | azimuth=43도
```

이건 좌측 sector에 장애물이 잡힌 것

#### 확인할 노드 2: `free_space_model_node`

봐야 하는 값:

```
left_blocked
left_clearance
left_occupied_ratio
left_unknown_ratio
```

기대값:

```
left_blocked=true
left_clearance 감소
left_occupied_ratio 증가
```

#### RViz에서 확인

```
LEFT sector 안쪽에 occupied grid가 있는지
LEFT 부채꼴 색상이 unknown/blocked로 바뀌는지
```

현재 marker 색상 정책상 unknown이 높으면 노란색으로 표시될 수 있다.

그러면 “빨간색 blocked”가 아니라 “노란색 unknown dominant”로 보일 수 있다.

그래서 색상만 보지 말고 로그에서 `left_clearance`, `left_occupied_ratio`도 같이 봐야 함함.

### 상황 D: 우측에 장애물이 있는 경우

예: Spot 기준 오른쪽 -60~-20도 영역에 물체를 둠.

#### 확인할 노드 1: `obstacle_model_node`

봐야 하는 값:

```
right.valid
right.nearest
right.azimuth
```

기대값:

```
right valid=true
right nearest=물체 거리 근처
right azimuth=-60~-20도 사이
```

현재 네 로그에서는:

```
right | valid=false
```

였지.

이건 현재 장면에서 오른쪽에 대표 cluster가 없다는 뜻이야.

#### 확인할 노드 2: `free_space_model_node`

봐야 하는 값:

```
right_blocked
right_clearance
right_occupied_ratio
right_unknown_ratio
```

우측 물체를 놓으면 기대값:

```
right_clearance 감소
right_occupied_ratio 증가
right_blocked=true
```

#### 주의할 점

ObstacleModel에서 `right.valid=false`인데 FreeSpaceModel에서 `right_blocked=1`일 수 있어.

이건 모순이 아니야.

```
ObstacleModel right=false
= 오른쪽에 대표 cluster가 없다

FreeSpaceModel right_blocked/unknown
= 오른쪽 공간을 주행 가능하다고 확신할 수 없다
```

즉, FreeSpaceModel은 unknown까지 고려해서 더 보수적으로 판단해.

### 상황 E: 모든 방향이 unknown으로 나오는 경우

현재 네 상황이 여기에 가까워.

로그:

```
front unknown=0.85
left unknown=0.82
right unknown=0.92
blocked_state=8
```

#### 확인할 노드 1: `local_occupancy_grid_node`

봐야 하는 값:

```
free cells
occupied cells
grid size
```

현재 예:

```
width=45 height=50
data_size=2250
free cells=49
occupied cells=76
```

전체 cell은 2250개인데, free+occupied가 대략 125개 정도면 나머지 대부분은 unknown이야.

```
unknown cell ≈ 2250 - 49 - 76 = 2125개
```

그래서 FreeSpaceModel에서 unknown ratio가 높게 나오는 게 자연스러워.

#### 확인할 노드 2: `free_space_model_node`

봐야 하는 값:

```
front_unknown_ratio
left_unknown_ratio
right_unknown_ratio
blocked_state
```

기대 해석:

```
unknown_ratio가 0.8~0.9 이상이면
현재 grid/check range 대비 관측된 free 공간이 너무 적다.
```

---

#### 원인 후보

이 상황에서는 아래를 의심해.

```
1. max_check_range가 너무 넓음
2. grid 영역이 너무 넓음
3. LiDAR point가 sparse함
4. ray tracing으로 free 처리되는 cell이 적음
5. unknown_ratio_threshold가 너무 낮음
6. free_ratio_threshold가 너무 높음
```

---

#### 확인해야 할 파라미터

`free_space_model.param.yaml`:

```
max_check_range
unknown_ratio_threshold
free_ratio_threshold
occupied_ratio_threshold
min_clearance_threshold
```

`local_occupancy_grid.param.yaml`:

```
origin_x
origin_y
size_x
size_y
resolution
use_ray_tracing
inflation_radius
```

### 상황 F: ObstacleModel은 감지, FreeSpaceModel은 전부 unknown인 경우

현재 네 상황이 이 케이스야.

```
ObstacleModel:
  front valid=true
  left valid=true
  obstacle_detected=true

FreeSpaceModel:
  front/left/right free=0
  blocked=1
  state=8
```

#### 해석

이건 이렇게 보면 돼.

```
ObstacleModel:
  장애물 cluster는 잘 잡고 있음

FreeSpaceModel:
  전체 공간 관점에서는 unknown이 많아서 주행 가능 판단을 못 함
```

즉, **장애물 감지는 잘 되는데, free space 판단은 아직 보수적**인 상태야.

---

#### 이때 확인할 것

### 1. ObstacleModel 쪽

```
front nearest
left nearest
right nearest
azimuth
```

목적:

```
장애물 방향과 거리가 실제와 맞는지 확인
```

### 2. LocalOccupancyGrid 쪽

```
free cells
occupied cells
RViz Map
```

목적:

```
free cell이 너무 적게 생기는지 확인
```

### 3. FreeSpaceModel 쪽

```
unknown_ratio
free_ratio
occupied_ratio
clearance
```

목적:

```
unknown 때문에 state=8로 가는지,
clearance 때문에 blocked가 되는지 구분
```

### 상황 G: RViz에서는 이상한데 로그는 정상인 경우

예: RViz 부채꼴이 이상하게 보이거나 고정된 것처럼 보임.

#### 확인할 것

### 1. RViz Fixed Frame

```
Fixed Frame = base_link
```

이면 sector는 항상 로봇 기준으로 고정되어 보인다.

```
Fixed Frame = odom/map
```

이면 로봇 회전과 함께 sector가 월드 기준으로 회전해 보인다.

현재 localization/odom이 없으면 `base_link` 기준으로 보는 게 맞아.

---

### 2. Marker 토픽

```
ros2 topic hz /perception/lidar/free_space_markers
```

### 3. Marker frame

```
ros2 topicecho /perception/lidar/free_space_markers--once |grep frame_id
```

기대:

```
frame_id: base_link
```

### 상황 H: free_space_model은 나오는데 marker가 안 보이는 경우

확인 순서:

```
ros2 topic hz /perception/lidar/free_space_model
ros2 topic hz /perception/lidar/free_space_markers
ros2 topic info /perception/lidar/free_space_markers
```

## 경우 1

```
free_space_model은 나옴
free_space_markers는 안 나옴
```

원인:

```
free_space_marker_node가 실행 안 됨
input_topic이 다름
QoS/환경 문제
```

확인:

```
ros2node info /free_space_marker_node
```

---

## 경우 2

```
free_space_markers는 나옴
RViz에 안 보임
```

원인:

```
RViz에 MarkerArray Display가 없거나
Topic이 다르거나
Fixed Frame이 안 맞음
```

RViz 설정:

```
Add → MarkerArray
Topic → /perception/lidar/free_space_markers
Fixed Frame → base_link
```

---

### 상황별 핵심 확인표

| 상황 | 먼저 볼 노드 | 볼 데이터 | 정상 기대 |
| --- | --- | --- | --- |
| 전방 장애물 | obstacle_model_node | front.valid, front.nearest | valid=true, 거리 감소 |
| 전방 장애물 | free_space_model_node | front_blocked, front_clearance | blocked=true, clearance 감소 |
| 전방 비움 | obstacle_model_node | front.valid | false 또는 거리 증가 |
| 전방 비움 | free_space_model_node | front_clearance, occupied_ratio | clearance 증가, occupied 감소 |
| 좌측 장애물 | obstacle_model_node | left.valid, azimuth | valid=true, 20~60도 |
| 우측 장애물 | obstacle_model_node | right.valid, azimuth | valid=true, -60~-20도 |
| 전부 unknown | local_occupancy_grid_node | free cells, occupied cells | free가 너무 적은지 확인 |
| 전부 unknown | free_space_model_node | unknown_ratio, blocked_state | state=8 원인 확인 |
| marker 안 보임 | free_space_marker_node | free_space_markers hz | marker publish 확인 |
| RViz 회전 이상 | RViz/TF | Fixed Frame, frame_id | base_link 기준이면 고정이 정상 |

우리는 **LiDAR Perception의 1차 환경 인지 계층**을 거의 만든 상태야.

현재 완성된 축은 크게 두 개야.

```
1. Obstacle Model
   - LiDAR point cloud 기반 장애물 cluster 분석
   - front / left / right sector별 대표 장애물 존재 여부, 거리, 방향 산출

2. FreeSpace Model
   - Local Occupancy Grid 기반 주행 가능 공간 분석
   - front / left / right sector별 free / blocked / unknown / clearance 산출
   - RViz 부채꼴 sector 시각화 완료
```

이제 앞으로의 계획은 **“개별 모델 개발”에서 “상위 계층이 쓸 수 있는 환경 모델화”로 넘어가는 것**이야.

---

# 앞으로의 개발 계획

## 1단계. 현재 LiDAR Perception MVP 검증 정리

먼저 지금 만든 두 모델이 상황 변화에 반응하는지 간단히 검증해야 해.

확인할 상황은 이 정도면 충분해.

```
1. 전방 장애물 있음
2. 전방 비움
3. 좌측 장애물 있음
4. 우측 장애물 있음
5. 장애물이 가까워졌다가 멀어지는 상황
```

이때 봐야 할 건:

```
Obstacle Model
- front / left / right valid
- nearest distance
- azimuth
- obstacle_detected

FreeSpace Model
- front / left / right blocked
- clearance
- unknown_ratio
- occupied_ratio
- blocked_state
```

목표는 완벽한 튜닝이 아니라, **상황이 바뀌었을 때 값이 방향성 있게 변하는지 확인하는 것**이야.

예를 들어 전방 장애물을 치우면:

```
front obstacle valid 감소
front occupied_ratio 감소
front clearance 증가
```

이런 변화가 보여야 해.

---

## 2단계. FreeSpaceModel 파라미터 최소 튜닝

지금 FreeSpaceModel은 정상 동작하지만, `UNKNOWN_DOMINANT`가 자주 뜨는 상태야.

그래서 다음은 완전 튜닝이 아니라 **최소 반응성 확보용 튜닝**을 하면 돼.

우선 봐야 할 파라미터는:

```
unknown_ratio_threshold
free_ratio_threshold
occupied_ratio_threshold
min_clearance_threshold
max_check_range
```

특히 현재처럼 unknown이 너무 높으면:

```
- max_check_range가 너무 넓은지
- grid가 너무 넓은지
- free_ratio_threshold가 너무 높은지
- unknown_ratio_threshold가 너무 낮은지
```

를 봐야 해.

하지만 이건 나중에 decision/planning이 붙은 뒤 다시 조정될 가능성이 크니까, 지금은 **front/left/right가 전부 항상 unknown으로만 고정되지 않게 하는 수준**이면 충분해.

---

```bash
1. ObstacleModel + FreeSpaceModel 현재 상태 README 정리
2. 상황별 간단 테스트로 FreeSpaceModel 반응성 확인
3. LocalizationHint.msg 설계
4. localization_hint_node 구현
5. EnvironmentModel.msg 설계
6. environment_model_node 구현
7. 이후 odom / IMU 기반 Localization 설계로 이동
```

```bash
ObstacleModel / FreeSpaceModel 완료
→ 최소 반응성 검증
→ LocalizationHint 생성
→ EnvironmentModel 통합
→ Decision/Planning 연동
→ Odometry/IMU 기반 Localization 본개발
→ Digital Twin 공통좌표계 연동
```