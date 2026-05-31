# [AN] Global Path 기반 Autonomous Navigation 설계

상태: Autonomous Navigation

## 1. 전체 구조

```bash
[Path Progress Tracker]
- global path 상 현재 위치 계산
- nearest_index, target_index, goal_reached 산출
        ↓
[Navigation FSM]
- 현재 자율보행 상태 결정
- TRACK_GLOBAL / PLAN_LOCAL / FOLLOW_LOCAL / REJOIN / STOP 상태 관리
        ↓
[Local Path Planner]
- FSM 상태와 path progress, obstacle/free-space를 보고 local path 생성
        ↓
[Path Follower / Velocity Controller]
- local path를 따라가기 위한 cmd_vel_raw 생성
        ↓
[Safety Supervisor]
- cmd_vel_raw 최종 검사
- 위험하면 stop
- 안전하면 cmd_vel publish
        ↓
[Motion Command Adapter]
- cmd_vel을 STM SPI packet으로 변환
```

```bash
                   ┌────────────────────────────┐
                   │ navigation_supervisor_node │
                   │ - Navigation FSM           │
                   │ - Safety Filter            │
                   └────────────┬───────────────┘
                                │ state/mode
                                ↓
/path_progress ─────→ ┌─────────────────────────┐
/localization  ─────→ │                         │
/obstacle_model ────→ │ local_path_planner_node │
/free_space_model ──→ │                         │
											│                         │
											└─────────┬───────────────┘
                                ↓
                         /planning/local_path
                                ↓
                      ┌─────────────────────────┐
                      │   path_follower_node    │
 											└─────────┬───────────────┘
                                ↓
                         /control/cmd_vel_raw
                                ↓
                   ┌────────────┴───────────────┐
                   │ navigation_supervisor_node │
                   │ final safety override      │
                   └────────────┬───────────────┘
                                ↓
                         /control/cmd_vel
```

### 1-1. Path Progress Tracking - `path_progress_tracker_node`

> 할당된 global path에서 현재 로봇이 어디쯤 있는지 계산하는 계층
> 
> 
> global path 진행률 관리자, global path 복귀 기준점 계산기, lookahead target 생성기
> 

```bash
Input :
- /planning/global_path/robot_1
- /localization/pose

Output :
- /navigation/path_progress/robot_1        # 상태 보고용 + 다른 노드 입력용
- /navigation/global_sub_goal/robot_1      # local planner가 바로 쓰기 쉬운 목표점

기능 :
- global path 수신
- 현재 pose 기준 nearest waypoint index 계산
- 진행 방향 기준으로 index가 뒤로 튀지 않도록 관리
- lookahead distance 기준 target waypoint 선택
- target heading 계산
- distance_to_goal 계산
- goal_reached 판단
```

`*sub_goal`: 전체 goal이 아니라, global path 위에서 지금 당장 바라볼 중간 목표점

**메시지 데이터 구조 :**

```bash
# PathProgress.msg

std_msgs/Header header

string robot_id

**uint32 nearest_index**            # 현재 pose와 가장 가까운 global path waypoint index
**uint32 target_index**             # lookahead로 선택한 목표 waypoint index
uint32 total_waypoints          # global path 전체 waypoint 개수

float32 progress_ratio          # 전체 path 중 진행 비율

float32 target_x_m              # 목표 waypoint의 x
float32 target_y_m              # 목표 waypoint의 y
float32 target_heading_rad      # 현재 위치에서 target을 바라보는 방향

float32 heading_error_rad       # 현재 yaw와 목표 heading 차이
float32 distance_to_target_m    # 현재 위치에서 target까지 거리
float32 distance_to_goal_m      # 현재 위치에서 goal까지 거리

**bool goal_reached**               # 목표 도착 여부
```

#### 1️⃣ /navigation/path_progress/robot_1

> 현재 global path 진행 상황 전체를 알려주는 토픽
> 

메시지 타입 후보:

```
robot_interfaces/msg/PathProgress
```

포함 데이터:

```
nearest_index
target_index
progress_ratio
distance_to_goal
goal_reached
heading_error
```

즉, “로봇이 path의 어디쯤인지”를 알려준다.

#### 2️⃣ /navigation/global_sub_goal/robot_1

`sub_goal`: 전체 goal이 아니라, **global path 위에서 지금 당장 바라볼 중간 목표점**

예를 들어 global path의 최종 목적지가 20m 앞이라고 해도 로봇이 그걸 한 번에 바라보면 안된다.

현재 위치에서 0.5m~1.0m 앞의 waypoint를 잡아야 안정적으로 따라갈 수 있다.

```
- global goal: 최종 목적지
- global sub_goal: 현재 위치 기준 lookahead 거리 앞의 중간 목표점
```

메시지 타입:

```
geometry_msgs/msg/PoseStamped
```

예시:

```
header:
  frame_id: mission_map
pose:
  position:
    x: 3.2
    y: 1.8
  orientation:
    yaw: target_heading
```

---

### 1-2. Local Path Planning - `local_path_planner_node`

> Global Path 추종 중 장애물을 만났을 때, 현재 환경에서 따라갈 짧은 local path를 생성
> 

```bash
Input :
- /navigation/path_progress/robot_1
- /navigation/global_sub_goal/robot_1
- /localization/pose
- /perception/lidar/obstacle_model
- /perception/lidar/free_space_model

Output :
- /planning/local_path/robot_1
- Type: nav_msgs/msg/Path

기능 :
- global sub-goal 방향 확인
- obstacle_model 기반 front_blocked 판단
- free_space_model 기반 회피 가능 방향 확인
- 회피가 필요 없으면 global sub-goal 방향으로 local path 생성
- 회피가 필요하면 best_heading 방향으로 local target 생성
- 회피 후 global path 재합류를 위한 rejoin target 설정
- local path를 mission_map 기준 nav_msgs/Path로 publish
```

#### 1️⃣ 동작 케이스

#### Case 1. 전방 장애물 없는 경우

```bash
front_blocked == false
→ global_sub_goal 방향으로 local path 생성
```

#### Case 2. 전방 장애물 O + Free Space 있음

```bash
front_blocked == true
free_space_model.path_available == true
→ best_heading_angle_rad 방향으로 local path 생성
```

#### Case 3. 전방 장애물 O + Free Space 없음

```bash
front_blocked == true
free_space_model.path_available == false
→ local path 생성 실패
→ planner_status = BLOCKED
```

#### 2️⃣ Local path 생성

```bash
1. 현재 pose를 시작점으로 둔다.
2. 선택된 heading 방향으로 local_target_distance만큼 떨어진 점을 만든다.
3. 현재 pose와 local target 사이를 2~5개 waypoint로 보간한다.
4. nav_msgs/Path로 publish한다.
```

#### 3️⃣ 좌표 변환 개념

회피 heading : `base_link` 기준

```bash
free_space_model.best_heading_angle_rad
= base_link 기준 회피 방향
```

이를 `mission_map` 기준 local target으로 바꾸려면:

```bash
global_heading = current_yaw + best_heading_angle_rad

target_x = current_x + local_target_distance * cos(global_heading)
target_y = current_y + local_target_distance * sin(global_heading)
```

---

### 1-3. Path Following / Velocity Control - `path_follower_node`

> Local Path를 따라가기 위해 **목표 선속도 v**와 **목표 각속도  w**를 계산하는 과정
> 

```bash
Input :
- /planning/local_path/robot_1
- /localization/pose
- /navigation/state/robot_1

Output :
- /control/cmd_vel_raw/robot_1 (Type: geometry_msgs/msg/TwistStamped)
									or
- /control/motion_command_raw/robot_1 (Type: robot_interfaces/msg/MotionCommand)

기능 :
- local path에서 현재 pose 기준 nearest waypoint 계산
- local lookahead target 선택
- target heading 계산
- heading error 계산
- 선속도 v_cmd 계산
- 각속도 w_cmd 계산
- heading error가 크면 제자리 회전
- heading error가 작으면 전진
```

---

### 1-4. Safety Supervisor / Navigation FSM - `navigation_supervisor_node`

> 모든 속도 명령이 Publish 되기 전에 안전 조건 검사를 진행하고, 현재 로봇의 자율보행 상태를 관리하는 과정으로 단순 safety filter가 아니라 **Robot Autonomous Walking FSM**도 함께 가진다.
> 

```bash
Input :
- /control/cmd_vel_raw/robot_1
- /navigation/path_progress/robot_1
- /planning/local_path/robot_1
- /perception/lidar/obstacle_model
- /perception/lidar/free_space_model
- /localization/pose
- /mission/command/robot_1

Output :
- /control/cmd_vel/robot_1
- /navigation/state/robot_1
- /decision/local_decision/robot_1

기능 :
- cmd_vel_raw를 최종 cmd_vel로 통과 또는 차단
- emergency stop 판단
- stale data 검사
- path 없음 / pose 없음 처리
- goal reached 시 stop
- obstacle이 너무 가까우면 stop
- 속도 제한
- 가속도 제한
- Navigation FSM 상태 관리
- 현재 판단 이유 publish
```

#### 1️⃣ Navigation FSM

상태 후보 :

```bash
NAV_IDLE
NAV_WAITING_FOR_PATH
NAV_TRACKING_GLOBAL_PATH
NAV_PLANNING_LOCAL_PATH
NAV_FOLLOWING_LOCAL_PATH
NAV_AVOIDING_OBSTACLE
NAV_REJOINING_GLOBAL_PATH
NAV_STOPPED_BY_OBSTACLE
NAV_GOAL_REACHED
NAV_EMERGENCY_STOP
NAV_LOCALIZATION_LOST
NAV_PERCEPTION_STALE
```

상태 전이 예시 :

```bash
NAV_WAITING_FOR_PATH
  global path 수신
  → NAV_TRACKING_GLOBAL_PATH

NAV_TRACKING_GLOBAL_PATH
  front blocked
  → NAV_PLANNING_LOCAL_PATH

NAV_PLANNING_LOCAL_PATH
  local path 생성 성공
  → NAV_FOLLOWING_LOCAL_PATH

NAV_FOLLOWING_LOCAL_PATH
  global path와 다시 가까워짐
  → NAV_TRACKING_GLOBAL_PATH

어느 상태든
  emergency obstacle
  → NAV_EMERGENCY_STOP

어느 상태든
  goal reached
  → NAV_GOAL_REACHED
```

#### 2️⃣ 최종 cmd_vel 보정 규칙

```bash
if emergency_stop:
    v = 0
    w = 0

else if stale_pose or stale_perception:
    v = 0
    w = 0

else if goal_reached:
    v = 0
    w = 0

else:
    cmd_vel_raw를 속도 제한 후 통과
```

---

### 1-5. Monitoring / Visualization

> Rviz, 관제 UI, 로그에서 현재 Navigation 상태를 확인하는 과정
> 

```bash
Input :
- /planning/global_path/robot_1
- /navigation/path_progress/robot_1
- /planning/local_path/robot_1
- /localization/pose
- /perception/lidar/obstacle_model
- /perception/lidar/free_space_model
- /control/cmd_vel/robot_1
- /navigation/state/robot_1

기능 :
- global path 시각화
- local path 시각화
- 현재 pose 시각화
- obstacle state 표시
- free-space best heading 표시
- current cmd_vel 표시
- Navigation FSM 상태 표시
- goal reached 여부 표시
```

---

## 2. ROS2 설계 구조

패키지 :

```bash
mission_control
├── global_path_generator_node
├── mission_assignment_node
└── mock_global_path_publisher_node

spot_navigation
├── path_progress_tracker_node
├── navigation_fsm_node
├── local_path_planner_node
├── path_follower_node
├── safety_supervisor_node
└── navigation_visualizer_node
```

각 노드의 핵심 output :

| 노드 | 출력 | 의미 |
| --- | --- | --- |
| `path_progress_tracker_node` | `/navigation/path_progress/robot_1` | global path 어디쯤 왔는지 |
| `navigation_fsm_node` | `/navigation/state/robot_1` | 현재 자율보행 상태 |
| `local_path_planner_node` | `/planning/local_path/robot_1` | 지금 따라갈 짧은 local path |
| `path_follower_node` | `/control/cmd_vel_raw/robot_1` | local path 추종용 원시 v, w |
| `safety_supervisor_node` | `/control/cmd_vel/robot_1` | 안전 검사를 통과한 최종 v, w |

Topic 최종 예시 :

```bash
# Mission / Global Path
/planning/global_path/robot_1
/planning/global_path/robot_2

# Localization
/localization/pose

# Perception
/perception/lidar/obstacle_model
/perception/lidar/free_space_model

# Navigation
/navigation/path_progress/robot_1
/navigation/global_sub_goal/robot_1
/planning/local_path/robot_1
/control/cmd_vel_raw/robot_1
/control/cmd_vel/robot_1
/navigation/state/robot_1

# Control
/control/motion_command/robot_1
```

## 3. 개발 순서

패키지 구조 :

```bash
spot_navigation
├── include/spot_navigation
├── src
│   ├── path_progress_tracker_node.cpp
│   ├── navigation_fsm_node.cpp
│   ├── local_path_planner_node.cpp
│   ├── path_follower_node.cpp
│   └── safety_supervisor_node.cpp
├── config
│   ├── path_progress_tracker.param.yaml
│   ├── navigation_fsm.param.yaml
│   ├── local_path_planner.param.yaml
│   ├── path_follower.param.yaml
│   └── safety_supervisor.param.yaml
└── launch
    └── spot_navigation.launch.py
```

frame 기준 :

```bash
global_path: mission_map
localization_pose: mission_map
local_path: mission_map
obstacle/free_space: base_link
cmd_vel: base_link 기준 속도 명령
```

업무 분담 :

| 구분 | 담당자 A | 담당자 B |
| --- | --- | --- |
| 패키지 뼈대 | 공동 | 공동 |
| msg 설계 | 공동 | 공동 |
| mock global path | 주 담당 | 보조 |
| PathProgress.msg | 주 담당 | 검토 |
| NavigationState.msg | 검토 | 주 담당 |
| LocalPathStatus.msg | 주 담당 | 검토 |
| SafetyStatus.msg | 검토 | 주 담당 |
| path_progress_tracker_node | 주 담당 | 테스트 보조 |
| navigation_fsm_node | 테스트 보조 | 주 담당 |
| local_path_planner_node | 주 담당 | FSM 연동 검토 |
| path_follower_node | 인터페이스 검토 | 주 담당 |
| safety_supervisor_node | 입력 조건 검토 | 주 담당 |
| launch 통합 | 공동 | 공동 |
| RViz 검증 | 공동 | 공동 |

---

### 3-1. 메시지

#### 1️⃣ PathProgress.msg

```bash
std_msgs/Header header

string robot_id

uint32 nearest_index
uint32 target_index
uint32 total_waypoints

float32 progress_ratio

float32 target_x_m
float32 target_y_m
float32 target_heading_rad

float32 heading_error_rad
float32 distance_to_target_m
float32 distance_to_goal_m

bool goal_reached
```

#### 2️⃣ NavigationState.msg

```bash
std_msgs/Header header

string robot_id

uint8 nav_state

bool path_received
bool pose_valid
bool perception_valid

bool front_blocked
bool left_blocked
bool right_blocked

bool local_path_required
bool rejoin_required
bool stop_required
bool goal_reached

float32 front_clearance_m
float32 left_clearance_m
float32 right_clearance_m

string reason

uint8 NAV_WAITING_FOR_PATH=0
uint8 NAV_TRACKING_GLOBAL_PATH=1
uint8 NAV_PLANNING_LOCAL_PATH=2
uint8 NAV_FOLLOWING_LOCAL_PATH=3
uint8 NAV_AVOIDING_OBSTACLE=4
uint8 NAV_REJOINING_GLOBAL_PATH=5
uint8 NAV_STOPPED_BY_OBSTACLE=6
uint8 NAV_GOAL_REACHED=7
uint8 NAV_EMERGENCY_STOP=8
uint8 NAV_LOCALIZATION_LOST=9
uint8 NAV_PERCEPTION_STALE=10
```

#### 3️⃣ LocalPathStatus.msg

```bash
std_msgs/Header header

string robot_id

bool local_path_available
uint8 planner_state

float32 target_x_m
float32 target_y_m
float32 selected_heading_rad
float32 local_path_length_m

string reason

uint8 PLANNER_IDLE=0
uint8 PLANNER_GLOBAL_SUB_GOAL=1
uint8 PLANNER_AVOIDANCE=2
uint8 PLANNER_REJOIN=3
uint8 PLANNER_BLOCKED=4
```

#### 4️⃣ SafetyStatus.msg

```bash
std_msgs/Header header

string robot_id

bool safe_to_move
bool emergency_stop_required
bool pose_stale
bool perception_stale
bool obstacle_too_close
bool goal_reached

float32 input_linear_x_mps
float32 input_angular_z_radps

float32 output_linear_x_mps
float32 output_angular_z_radps

string reason
```

---

### 3-2. Path Progress Tracker - `path_progress_tracker_node` (A)

```bash
I(Anput :
- /planning/global_path/robot_1
- /localization/pose

Output :
**- /navigation/path_progress/robot_1
- /navigation/global_sub_goal/robot_1**
```

- `/navigation/global_sub_goal/robot_1` 는 후속 노드 안정화 후에 PathProgress에서 분리 발행
- **PathProgress.msg**
    
    ```bash
    std_msgs/Header header
    
    string robot_id
    
    uint32 nearest_index
    uint32 target_index
    uint32 total_waypoints
    
    float32 progress_ratio
    
    float32 target_x_m
    float32 target_y_m
    float32 target_heading_rad
    
    float32 heading_error_rad
    float32 distance_to_target_m
    float32 distance_to_goal_m
    
    bool goal_reached
    ```
    

**구현 기능 :**

```bash
- global path 저장 + 현재 pose 수신
- 현재 pose 기준 nearest waypoint index 계산
- 진행 방향 기준으로 index가 뒤로 튀지 않도록 관리
- lookahead distance 기준 target waypoint 선택
- target heading 계산
- distance_to_goal 계산
- goal_reached 판단
```

**파라미터 :**

- 핵심 의미
    
    **`lookahead_distance_m` :**
    
    너무 작으면 로봇이 path 위에서 좌우로 흔들릴 수 있음.
    
    ```
    - 작음: 로봇이 바로 앞 waypoint만 보고 잦은 회전
    - 큼: 부드럽지만 코너를 크게 돌아감
    ```
    
    **`goal_tolerance_m` :**
    
    목적지에서 얼마나 가까워지면 `goal_reached=true`로 볼지 결정한다.
    
    ```
    - 작음: 정확하지만 로봇이 goal 근처에서 계속 미세 조정
    - 큼: 도착 판단은 쉬우나 정확도 낮음
    ```
    

| 파라미터 | 의미 | 관련 노드 | 초기값 예시 |
| --- | --- | --- | --- |
| `lookahead_distance_m` | 현재 위치에서 global path를 몇 m 앞까지 보고 target waypoint를 잡을지 | `path_progress_tracker_node` | `0.5` |
| `goal_tolerance_m` | goal에 도착했다고 볼 거리 기준 | `path_progress_tracker_node` | `0.25~0.30` |
| `nearest_search_window` | nearest index를 찾을 때 이전 index 주변 몇 개 waypoint만 볼지 | `path_progress_tracker_node` | `20` |
| `allow_backward_index_jump` | nearest index가 뒤로 튀는 것을 허용할지 | `path_progress_tracker_node` | `false` |
| `path_timeout_sec` | global path가 오래되었는지 판단하는 시간 | `path_progress_tracker_node` 또는 `navigation_fsm_node` | `1.0~2.0` |

**검증 :**

```bash
1. nearest_index가 로봇 위치에 따라 증가한다
2. target_index가 nearest_index보다 앞에 있다
3. distance_to_goal이 점점 줄어든다
4. goal 근처에서 goal_reached=true가 된다
```

---

### 3-3. Navigation FSM 개발 - `navigation_fsm_node` (B)

```bash
Input :
- /navigation/path_progress/robot_1
- /perception/lidar/obstacle_model
- /perception/lidar/free_space_model
- /localization/pose

Output :
- /navigation/state/robot_1
```

- **NavigationState.msg**
    
    ```bash
    std_msgs/Header header
    
    string robot_id
    
    uint8 nav_state
    
    bool path_received
    bool pose_valid
    bool perception_valid
    
    bool front_blocked
    bool left_blocked
    bool right_blocked
    
    bool local_path_required
    bool rejoin_required
    bool stop_required
    bool goal_reached
    
    float32 front_clearance_m
    float32 left_clearance_m
    float32 right_clearance_m
    
    string reason
    
    uint8 NAV_WAITING_FOR_PATH=0
    uint8 NAV_TRACKING_GLOBAL_PATH=1
    uint8 NAV_PLANNING_LOCAL_PATH=2
    uint8 NAV_FOLLOWING_LOCAL_PATH=3
    uint8 NAV_AVOIDING_OBSTACLE=4
    uint8 NAV_REJOINING_GLOBAL_PATH=5
    uint8 NAV_STOPPED_BY_OBSTACLE=6
    uint8 NAV_GOAL_REACHED=7
    uint8 NAV_EMERGENCY_STOP=8
    uint8 NAV_LOCALIZATION_LOST=9
    uint8 NAV_PERCEPTION_STALE=10
    ```
    

**FSM State 전송 구조 :**

```bash
                           ┌───────────────────────┐
                           │  navigation_fsm_node  │
                           └──────────┬────────────┘
                                      │
                              /navigation/state
                                      │
          ┌───────────────────────────┼───────────────────────────┐
          ↓                           ↓                           ↓
local_path_planner_node        path_follower_node          safety_supervisor_node
          │                           │                           │
          ↓                           ↓                           ↓
/planning/local_planner_status  /control/cmd_vel_raw       /safety/status
          │                           │                           │
          └─────────────── feedback to navigation_fsm_node ───────┘
```

- FSM → state 명령
- 각 노드 → status 보고
- FSM → status를 보고 다음 state 결정
- **노드별 FSM status publish**
    
    **1️⃣ path_progress_tracker_node**
    
    Publish Topic :
    
    ```
    /navigation/path_progress/robot_1
    ```
    
    FSM은 여기서 아래 값을 본다.
    
    ```
    path_valid
    pose_valid
    distance_to_goal_m
    distance_to_nearest_m
    goal_reached
    ```
    
    이 값으로 FSM은 다음 상태를 판단할 수 있다.
    
    ```
    goal_reached == true
    → NAV_GOAL_REACHED
    
    distance_to_nearest_m < rejoin_tolerance
    → NAV_TRACKING_GLOBAL_PATH로 복귀 가능
    ```
    
    ---
    
    **2️⃣ local_path_planner_node**
    
    Publish Topic :
    
    ```
    /planning/local_planner_status/robot_1
    ```
    
    FSM은 여기서 아래 값을 본다.
    
    ```
    path_available
    planner_status
    blocked
    reason
    ```
    
    예:
    
    ```
    planner_status = AVOIDANCE
    path_available = true
    → NAV_FOLLOWING_LOCAL_PATH 또는 NAV_AVOIDING_OBSTACLE
    
    planner_status = BLOCKED
    path_available = false
    → NAV_STOPPED_BY_OBSTACLE
    ```
    
    ---
    
    **3️⃣ path_follower_node**
    
    현재는 `cmd_vel_raw`만 publish한다고 했는데, FSM이 local path 완료 여부를 알기 위해서는 follower status가 있으면 좋다.
    
    토픽 후보:
    
    ```
    /control/path_follower_status/robot_1
    ```
    
    또는:
    
    ```
    /navigation/path_follower_status/robot_1
    ```
    
    추천 메시지:
    
    ```
    std_msgs/Header header
    
    string robot_id
    
    bool local_path_received
    bool local_path_valid
    bool local_path_reached
    
    float32 target_x_m
    float32 target_y_m
    float32 distance_to_local_target_m
    float32 heading_error_rad
    
    string reason
    ```
    
    이걸 통해 FSM은 판단할 수 있다.
    
    ```
    local_path_reached == true
    → REJOINING 또는 TRACKING_GLOBAL_PATH로 전환 검토
    ```
    
    물론 초기에는 이 status 없이도 `PathProgress.distance_to_nearest_m`만으로 rejoin 판단이 가능.
    
    하지만 구조를 제대로 가져갈 거면 `PathFollowerStatus`도 있으면 좋음.
    
    ---
    
    **4️⃣ safety_supervisor_node**
    
    Publish Topic :
    
    ```
    /safety/status/robot_1
    ```
    
    FSM은 여기서 아래 값을 본다.
    
    ```
    emergency_stop_required
    pose_stale
    perception_stale
    safe_to_move
    obstacle_too_close
    ```
    
    예:
    
    ```
    emergency_stop_required == true
    → NAV_EMERGENCY_STOP
    
    perception_stale == true
    → NAV_PERCEPTION_STALE
    
    pose_stale == true
    → NAV_LOCALIZATION_LOST
    ```
    

**구현 기능 :**

```bash
1. path_progress 수신 여부 확인
2. pose/perception stale 검사
3. front_blocked 판단
4. goal_reached 판단
5. 현재 nav_state 결정
6. local_path_required 여부 publish
7. stop_required 여부 publish
```

**파라미터 :**

- 핵심 의미
    
    **`front_block_distance_m` :**
    
    전방 장애물 감지 시 회피 모드로 전환하는 기준
    
    ```
    front.nearest_distance_xy < front_block_distance_m
    → NAV_PLANNING_LOCAL_PATH 또는 NAV_AVOIDING_OBSTACLE
    ```
    
    **`front_clear_distance_m` :**
    
    회피 중 다시 global path 추종으로 돌아갈지 판단하는 기준
    
    보통 `front_clear_distance_m`은 `front_block_distance_m`보다 크게 잡는다.
    
    ```
    front_block_distance_m = 0.70
    front_clear_distance_m = 0.85
    ```
    
    이렇게 해야 hysteresis가 생겨서 상태가 계속 흔들리지 않는다.
    
    ```
    0.69m → blocked
    0.71m → clear
    0.68m → blocked
    ```
    
    이런 식으로 왔다 갔다 하는 걸 막을 수 있다다.
    

| 파라미터 | 의미 | 관련 노드 | 초기값 예시 |
| --- | --- | --- | --- |
| `front_block_distance_m` | 전방이 막혔다고 판단하는 거리 | `navigation_fsm_node` | `0.70` |
| `side_block_distance_m` | 좌/우가 막혔다고 판단하는 거리 | `navigation_fsm_node` | `0.50` |
| `front_clear_distance_m` | 전방이 다시 clear됐다고 판단하는 거리 | `navigation_fsm_node` | `0.85` |
| `clear_hold_time_sec` | front clear 상태가 몇 초 유지되어야 회피 종료로 볼지 | `navigation_fsm_node` | `0.5` |
| `rejoin_tolerance_m` | global path에 복귀했다고 판단하는 거리 | `navigation_fsm_node` | `0.30` |
| `pose_timeout_sec` | pose가 오래되었다고 판단하는 시간 | `navigation_fsm_node` | `0.5` |
| `perception_timeout_sec` | perception이 오래되었다고 판단하는 시간 | `navigation_fsm_node` | `0.5` |

**검증 :** → 실제 local path 없이도 검증 가능

```bash
1. 장애물 앞에 있는 경우 :
	front_blocked=true
	nav_state=PLANNING_LOCAL_PATH 또는 AVOIDING_OBSTACLE

2. 장애물이 없는 경우 :
	nav_state=TRACKING_GLOBAL_PATH
```

---

### 3-4. Local Path Planner - `local_path_planner_node` (A)

```bash
Input :
- /navigation/path_progress/robot_1
- /navigation/state/robot_1
- /localization/pose
- /perception/lidar/obstacle_model
- /perception/lidar/free_space_model

Output :
- /planning/local_path/robot_1
- /planning/local_path_status/robot_1
```

**구현 기능 :**

```bash
1. nav_state 확인
2. TRACKING_GLOBAL_PATH이면 path_progress의 target_x, target_y 방향으로 local path 생성
3. AVOIDING_OBSTACLE이면 free_space_model.best_heading 방향으로 local target 생성
4. REJOINING_GLOBAL_PATH이면 nearest global path point보다 앞쪽 target으로 local path 생성
5. free_space가 없으면 local_path_available=false
6. nav_msgs/Path publish
```

- 장애물이 없어도 local path는 항상 만듬
    
    ```
    1. 장애물 O : global sub-goal 방향 local path 생성
    2. 장애물 X : free-space best heading 방향 local path 생성
    ```
    
    이렇게 해야 `path_follower_node`는 항상 `/planning/local_path/robot_1`만 보면 된다.
    

**좌표 변환 :** 

```bash
global_heading = current_yaw + best_heading_angle_rad;

target_x = current_x + local_target_distance * cos(global_heading);
target_y = current_y + local_target_distance * sin(global_heading);
```

- free-space heading은 `base_link` 기준이므로, local planner 내부에서 `mission_map` 기준 target으로 변환한다.
- Global path와 pose는 `mission_map` 기준이고, obstacle/free-space는 `base_link` 기준으로 나눠야 한다는 점이 기존 설계의 핵심

**파라미터 :**

- 핵심 의미
    
    **`local_target_distance_m` :**
    
    local planner가 지금 당장 만들어낼 짧은 목표점 거리
    
    ```
    작음:
    장애물 주변에서 민첩하지만 자주 path가 바뀜
    
    큼:
    부드럽지만 좁은 공간에서 위험할 수 있음
    ```
    
    **`max_avoidance_heading_rad` :**
    
    free-space model이 너무 큰 각도를 제안해도 제한을 건다.
    
    ```
    best_heading = 1.4 rad
    max_avoidance_heading = 0.8 rad
    → 실제 사용 heading = 0.8 rad
    ```
    
    **`min_free_space_clearance_m` :**
    
    free-space model이 path_available=true라고 해도 clearance가 너무 작으면 local planner가 거부할 수 있다.
    
    ```
    best_clearance < min_free_space_clearance_m
    → local_path_available=false
    ```
    

| 파라미터 | 의미 | 관련 노드 | 초기값 예시 |
| --- | --- | --- | --- |
| `local_target_distance_m` | 현재 위치에서 local target을 몇 m 앞에 만들지 | `local_path_planner_node` | `0.6~0.8` |
| `local_path_waypoint_count` | local path를 몇 개 waypoint로 보간할지 | `local_path_planner_node` | `5` |
| `avoidance_heading_gain` | free-space heading을 얼마나 강하게 반영할지 | `local_path_planner_node` | `1.0` |
| `max_avoidance_heading_rad` | 회피 heading의 최대 각도 제한 | `local_path_planner_node` | `0.8~1.0` |
| `min_free_space_clearance_m` | free-space를 회피 경로로 인정할 최소 clearance | `local_path_planner_node` | `0.45~0.60` |
| `use_unknown_as_caution` | unknown 영역을 통과 후보로 볼지 | `local_path_planner_node` | `true` |
| `rejoin_target_offset_index` | rejoin 시 nearest index보다 몇 개 앞을 볼지 | `local_path_planner_node` | `3~5` |

**검증 :** 

```bash
1. 장애물 없음 → global path 방향으로 local path 생성
2. 전방 장애물 있음 → free-space 방향으로 local path 생성
3. free-space 없음 → local_path_status가 BLOCKED
```

- RViz에서 `/planning/local_path/robot_1`Path 확인

---

### 3-5. Path Follower / Velocity Controller - `path_follower_node` (B)

```bash
Input :
- /planning/local_path/robot_1
- /localization/pose
- /navigation/state/robot_1

Output :
- /control/cmd_vel_raw/robot_1 (Type: geometry_msgs/msg/TwistStamped)
```

**구현 기능 :**

```bash
1. local path 수신
2. 현재 pose 기준 local path의 nearest point 계산
3. local lookahead target 선택
4. target heading 계산
5. heading error 계산
6. angular.z = k_yaw * heading_error 계산
7. heading error가 크면 linear.x 감소
8. heading error가 너무 크면 제자리 회전
9. cmd_vel_raw publish
```

**기본 제어 공식 :**

```bash
w_cmd = k_yaw * heading_error
w_cmd = clamp(w_cmd, -max_w, max_w)

if abs(heading_error) > turn_in_place_threshold:
    v_cmd = 0.0
else:
    v_cmd = max_v * speed_scale
```

**파라미터 :**

- 핵심 의미
    
    **`k_yaw` :**
    
    heading error를 각속도로 바꾸는 비율
    
    ```
    w = k_yaw * heading_error
    ```
    
    크게 잡으면 빠르게 방향을 맞추지만 흔들릴 수 있고, 작게 잡으면 부드럽지만 반응이 느리다.
    
    **`turn_in_place_threshold_rad` :**
    
    로봇이 목표 방향과 너무 많이 틀어져 있으면, 전진하면서 돌지 말고 제자리 회전부터 하게 하는 기준
    
    ```
    abs(heading_error) > turn_in_place_threshold_rad
    → linear.x = 0
    → angular.z만 출력
    ```
    

| 파라미터 | 의미 | 관련 노드 | 초기값 예시 |
| --- | --- | --- | --- |
| `path_lookahead_distance_m` | local path 위에서 몇 m 앞 target을 볼지 | `path_follower_node` | `0.3~0.5` |
| `max_linear_x_mps` | 최대 전진 속도 | `path_follower_node` | `0.10~0.20` |
| `min_linear_x_mps` | 최소 전진 속도 | `path_follower_node` | `0.03~0.05` |
| `max_angular_z_radps` | 최대 yaw 각속도 | `path_follower_node` | `0.5~0.8` |
| `k_yaw` | heading error를 각속도로 바꾸는 gain | `path_follower_node` | `1.0~1.5` |
| `heading_tolerance_rad` | 이 이내면 거의 정렬됐다고 보는 각도 | `path_follower_node` | `0.15` |
| `turn_in_place_threshold_rad` | 이 이상이면 전진하지 않고 제자리 회전 | `path_follower_node` | `0.6~0.8` |
| `slow_down_angle_rad` | 이 각도에 가까워질수록 감속 | `path_follower_node` | `0.5~0.7` |

**검증 :**

```bash
target이 왼쪽이면 angular.z > 0
target이 오른쪽이면 angular.z < 0
heading error가 작으면 linear.x > 0
heading error가 크면 linear.x가 줄거나 0
```

---

### 3-6. Safety Supervisor - `safety_supervisor_node` (B)

```bash
Input :
- /control/cmd_vel_raw/robot_1
- /navigation/state/robot_1
- /perception/lidar/obstacle_model
- /perception/lidar/free_space_model
- /localization/pose

Output :
- /control/cmd_vel/robot_1     # STM으로 내려가기 직전의 최종 안전 속도 명령
- /safety/status/robot_1       # 왜 그 cmd_vel이 나왔는지 설명하는 안전 판단 결과
```

**구현 기능 :**

```bash
1. cmd_vel_raw 수신
2. nav_state 확인
3. obstacle emergency distance 검사
4. pose stale 검사
5. perception stale 검사
6. goal_reached 검사
7. max speed 제한
8. acceleration limit 적용
9. 최종 cmd_vel publish
```

**파라미터 :**

- 핵심 의미
    
    **`soft_stop_distance_m` :**
    
    장애물이 가까워지면 바로 멈추지 않고 속도를 줄이는 구간
    
    ```
    front_clearance < soft_stop_distance_m
    → v를 점진적으로 줄임
    ```
    
    **`emergency_stop_distance_m` :**
    
    이 거리 안에 장애물이 들어오면 무조건 정지한다.
    
    ```
    front_clearance < emergency_stop_distance_m
    → v = 0
    → w = 0
    ```
    
    **`max_linear_decel_mps2` :**
    
    soft stop에서 얼마나 빠르게 감속할지 정한다.
    
    ```
    너무 작음:
    장애물 앞에서 늦게 멈춤
    
    너무 큼:
    로봇 움직임이 급격해짐
    ```
    

| 파라미터 | 의미 | 관련 노드 | 초기값 예시 |
| --- | --- | --- | --- |
| `soft_stop_distance_m` | 감속을 시작할 전방 거리 | `safety_supervisor_node` | `0.60~0.70` |
| `emergency_stop_distance_m` | 즉시 정지할 전방 거리 | `safety_supervisor_node` | `0.25~0.35` |
| `max_safe_linear_x_mps` | safety 통과 후 허용할 최대 전진 속도 | `safety_supervisor_node` | `0.15~0.20` |
| `max_safe_angular_z_radps` | safety 통과 후 허용할 최대 각속도 | `safety_supervisor_node` | `0.6~0.8` |
| `max_linear_accel_mps2` | 선속도 증가 제한 | `safety_supervisor_node` | `0.2~0.4` |
| `max_linear_decel_mps2` | 선속도 감소 제한 | `safety_supervisor_node` | `0.3~0.6` |
| `max_angular_accel_radps2` | 각속도 변화 제한 | `safety_supervisor_node` | `1.0~2.0` |
| `cmd_timeout_sec` | cmd_vel_raw가 오래되었는지 판단 | `safety_supervisor_node` | `0.3~0.5` |
| `pose_timeout_sec` | pose stale 판단 | `safety_supervisor_node` | `0.5` |
| `perception_timeout_sec` | perception stale 판단 | `safety_supervisor_node` | `0.5` |

**검증 :**

```bash
정상 상황 → cmd_vel_raw가 cmd_vel로 통과
장애물 너무 가까움 → cmd_vel = 0
goal_reached → cmd_vel = 0
pose/perception stale → cmd_vel = 0
```

- cmd_vel = 0에 대한 최종 계산 값을 만들 때, 선형적인 제어가 가능한 상황인지 항상 생각하면서 계산하기

---

### 3-7. 파라미터 튜닝

튜닝 대상 파라미터 : 

**개발 노드별 파라미터 항목 참고**

```bash
lookahead_distance_m
goal_tolerance_m
local_target_distance_m
front_block_distance_m
emergency_stop_distance_m
max_linear_x_mps
max_angular_z_radps
k_yaw
turn_in_place_threshold_rad
pose_timeout_sec
perception_timeout_sec
```

튜닝 예시 시나리오 :

```bash
1. 장애물 없는 global path 추종
2. 정적 장애물 앞 STOP
3. 정적 장애물 회피 local path 생성
4. 회피 후 global path 복귀
5. 동적 장애물 등장 시 STOP 또는 우회
6. goal reached 후 정지
```

---

## 4. 각 노드별 출력 토픽 정리

- 요약
    
    ```bash
    [Global Path Publisher / Mission]
    출력:
    - /planning/global_path/robot_1
    
    [Path Progress Tracker]
    출력:
    - /navigation/path_progress/robot_1
    
    [Navigation FSM]
    출력:
    - /navigation/state/robot_1
    
    [Local Path Planner]
    출력:
    - /planning/local_path/robot_1
    - /planning/local_planner_status/robot_1
    
    [Path Follower / Velocity Controller]
    출력:
    - /control/cmd_vel_raw/robot_1
    
    [Safety Supervisor]
    출력:
    - /control/cmd_vel/robot_1
    - /safety/status/robot_1
    ```
    
    - Path 계열 : “어디로 갈 것인가?”
    - State / Status 계열 : “현재 상태가 무엇인가?” , “왜 이런 결과가 나왔는가?”
    - cmd_vel 계열 : “실제로 몇 m/s, 몇 rad/s로 움직일 것인가?”
    

### 4-1. `/planning/global_path/robot_1`

> 담당 노드 : mission/global_path_assignment_node
> 
> 
> 메시지 타입 : nav_msgs/msg/Path
> 

nav_msgs/Path :

```bash
std_msgs/Header header
geometry_msgs/PoseStamped[] poses
```

- 데이터 필드 의미
    
    ```bash
    header.frame_id
    = "mission_map"
    = global path가 어떤 좌표계 기준인지
    
    header.stamp
    = path 생성 또는 갱신 시각
    
    poses[i].pose.position.x
    = i번째 waypoint의 mission_map 기준 x
    
    poses[i].pose.position.y
    = i번째 waypoint의 mission_map 기준 y
    
    poses[i].pose.orientation
    = waypoint 방향
    = 초기에는 안 써도 됨
    ```
    

---

### 4-2. `/navigation/path_progress/robot_1`

> 담당 노드 : path_progress_tracker_node
> 
> 
> 메시지 타입 : robot_interfaces/msg/PathProgress
> 

```bash
# PathProgress.msg

std_msgs/Header header

string robot_id

bool path_received
bool pose_received
bool path_valid
bool pose_valid

uint32 nearest_index
uint32 target_index
uint32 total_waypoints

float32 progress_ratio

float32 nearest_x_m
float32 nearest_y_m

float32 target_x_m
float32 target_y_m
float32 target_heading_rad

float32 heading_error_rad
float32 distance_to_nearest_m
float32 distance_to_target_m
float32 distance_to_goal_m

bool goal_reached
```

- 데이터 필드 의미
    
    ### 기본 정보
    
    ```
    header.frame_id
    = "mission_map"
    = 이 메시지의 위치 정보가 mission_map 기준이라는 뜻
    
    robot_id
    = "robot_1"
    = 어떤 로봇의 path progress인지 구분
    ```
    
    ---
    
    ### 입력 유효성
    
    ```
    path_received
    = global path를 한 번이라도 받았는가?
    
    pose_received
    = localization pose를 한 번이라도 받았는가?
    
    path_valid
    = path가 비어 있지 않고 frame_id가 올바른가?
    
    pose_valid
    = pose가 유효하고 오래되지 않았는가?
    ```
    
    이 값들이 필요한 이유는 `navigation_fsm_node`가 다음 상태를 정할 때 사용하기 위함
    
    ```
    path_valid == false
    → NAV_WAITING_FOR_PATH
    
    pose_valid == false
    → NAV_LOCALIZATION_LOST
    ```
    
    ---
    
    ### global path index 정보
    
    ```
    nearest_index
    = 현재 로봇 위치와 가장 가까운 global path waypoint index
    
    target_index
    = lookahead 거리만큼 앞쪽에 있는 목표 waypoint index
    
    total_waypoints
    = global path 전체 waypoint 개수
    
    progress_ratio
    = nearest_index / total_waypoints
    = 전체 경로 중 몇 % 정도 진행했는지
    ```
    
    예를 들어:
    
    ```
    total_waypoints = 100
    nearest_index = 40
    progress_ratio = 0.40
    ```
    
    이면 global path의 약 40% 지점까지 왔다고 볼 수 있다다.
    
    ---
    
    ### 위치 정보
    
    ```
    nearest_x_m, nearest_y_m
    = 현재 로봇과 가장 가까운 global path point 좌표
    
    target_x_m, target_y_m
    = 지금 따라가야 할 lookahead target point 좌표
    ```
    
    `target_x_m`, `target_y_m`이 사실상 이전에 말한 **global sub-goal**
    
    그래서 별도 `/navigation/global_sub_goal/robot_1` 토픽을 만들지 않고, 일단 `PathProgress.msg` 안에 포함시킨 후에 나중에 파이프라인 안정화 이후 분리
    
    ---
    
    ### 방향 정보
    
    ```
    target_heading_rad
    = 현재 위치에서 target point를 바라보는 방향
    
    heading_error_rad
    = target_heading_rad - current_yaw
    = 현재 로봇 heading과 목표 heading의 차이
    ```
    
    예:
    
    ```
    heading_error_rad > 0
    → 목표가 로봇 기준 왼쪽
    
    heading_error_rad < 0
    → 목표가 로봇 기준 오른쪽
    ```
    
    ---
    
    ### 거리 정보
    
    ```
    distance_to_nearest_m
    = 현재 위치와 nearest global path point 사이 거리
    = global path에서 얼마나 벗어났는지 확인 가능
    
    distance_to_target_m
    = 현재 위치와 lookahead target 사이 거리
    
    distance_to_goal_m
    = 현재 위치와 global path 마지막 point 사이 거리
    ```
    
    특히 `distance_to_nearest_m`은 rejoin 판단에 유용
    
    ```
    distance_to_nearest_m < rejoin_tolerance_m
    → global path에 다시 복귀했다고 판단 가능
    ```
    
    ---
    
    ### goal 판단
    
    ```
    goal_reached
    = distance_to_goal_m < goal_tolerance_m
    ```
    
    이 값이 true가 되면 Navigation FSM은 `NAV_GOAL_REACHED`로 전이할 수 있다.
    

---

### 4-3. **`/navigation/state/robot_1`**

> 담당 노드 : navigation_fsm_node
> 
> 
> 메시지 타입 : robot_interfaces/msg/NavigationState
> 

```bash
# NavigationState.msg

std_msgs/Header header

string robot_id

uint8 nav_state

bool path_received
bool path_valid
bool pose_valid
bool perception_valid
bool local_path_required
bool rejoin_required
bool stop_required
bool goal_reached

bool front_blocked
bool left_blocked
bool right_blocked

float32 front_clearance_m
float32 left_clearance_m
float32 right_clearance_m

float32 distance_to_goal_m
float32 distance_to_global_path_m

string reason

uint8 NAV_WAITING_FOR_PATH=0
uint8 NAV_TRACKING_GLOBAL_PATH=1
uint8 NAV_PLANNING_LOCAL_PATH=2
uint8 NAV_FOLLOWING_LOCAL_PATH=3
uint8 NAV_AVOIDING_OBSTACLE=4
uint8 NAV_REJOINING_GLOBAL_PATH=5
uint8 NAV_STOPPED_BY_OBSTACLE=6
uint8 NAV_GOAL_REACHED=7
uint8 NAV_EMERGENCY_STOP=8
uint8 NAV_LOCALIZATION_LOST=9
uint8 NAV_PERCEPTION_STALE=10
```

- 데이터 필드 의미
    
    ### FSM 상태
    
    ```
    nav_state
    = 현재 자율보행 상태
    ```
    
    예:
    
    ```
    NAV_TRACKING_GLOBAL_PATH
    = global path를 정상 추종 중
    
    NAV_AVOIDING_OBSTACLE
    = 장애물 회피 중
    
    NAV_REJOINING_GLOBAL_PATH
    = 회피 후 global path로 복귀 중
    
    NAV_STOPPED_BY_OBSTACLE
    = 장애물 때문에 멈춘 상태
    
    NAV_GOAL_REACHED
    = 목표 도착
    ```
    
    ---
    
    ### 입력 상태
    
    ```
    path_received
    = global path를 받았는가?
    
    path_valid
    = path가 비어 있지 않고 mission_map 기준인가?
    
    pose_valid
    = localization pose가 유효한가?
    
    perception_valid
    = obstacle/free-space 데이터가 유효한가?
    ```
    
    ---
    
    ### 명령성 플래그
    
    ```
    local_path_required
    = local path planner가 path를 새로 만들어야 하는가?
    
    rejoin_required
    = global path로 복귀해야 하는 상태인가?
    
    stop_required
    = 현재 navigation 관점에서 정지가 필요한가?
    
    goal_reached
    = 목표에 도착했는가?
    ```
    
    이 플래그들이 local planner와 safety supervisor가 쓰기에 유용 
    
    ---
    
    ### obstacle 상태
    
    ```
    front_blocked
    = front obstacle이 block distance 이내인가?
    
    left_blocked
    = left obstacle이 block distance 이내인가?
    
    right_blocked
    = right obstacle이 block distance 이내인가?
    
    front_clearance_m
    = 전방 대표 장애물까지 거리
    
    left_clearance_m
    = 좌측 대표 장애물까지 거리
    
    right_clearance_m
    = 우측 대표 장애물까지 거리
    ```
    
    현재 obstacle model은 front/left/right에 대해 representative cluster를 선택하고 nearest distance를 출력하는 구조라서 이 값을 NavigationState 안에 요약해서 넣을 수 있다.
    
    ---
    
    ### 거리 상태
    
    ```
    distance_to_goal_m
    = goal까지 남은 거리
    
    distance_to_global_path_m
    = 현재 위치와 nearest global path point 사이 거리
    ```
    
    `distance_to_global_path_m`은 rejoin 판단에 중요하다.
    
    ---
    
    ### reason (선택)
    
    ```
    reason
    = 현재 상태가 된 이유 (어떤 노드에서 왜 상태를 바꿨는지)
    ```
    
    예:
    
    ```
    "front obstacle detected"
    "goal reached"
    "waiting for global path"
    "perception data stale"
    ```
    
    이 필드는 디버깅 때 매우 유용하다.
    

---

### 4-4. `/navigation/local_path/robot_1`

> 담당 노드 : local_path_planner_node
> 
> 
> 메시지 타입 : nav_msgs/msg/Path
> 

```bash
std_msgs/Header header
geometry_msgs/PoseStamped[] poses
```

- 데이터 필드 의미
    
    ```
    header.frame_id
    = "mission_map"
    
    poses[0]
    = 현재 pose 근처 또는 local path 시작점
    
    poses[-1]
    = local target point
    
    poses[i]
    = 시작점과 target 사이를 보간한 waypoint
    ```
    
    ---
    
    **A. 장애물 없음**
    
    ```
    local path = global sub-goal 방향의 짧은 path
    ```
    
    **B. 장애물 있음 + free-space 있음**
    
    ```
    local path = free_space_model.best_heading 방향으로 만든 회피 path
    ```
    
    **C. 장애물 있음 + free-space 없음**
    
    ```
    local path는 비우거나 publish하지 않음
    local_planner_status.path_available = false
    ```
    

---

### 4-5. `/navigation/local_planner_status/robot_1`

> 담당 노드 : local_path_planner_node
> 
> 
> 메시지 타입 : robot_interfaces/msg/LocalPlannerStatus
> 

```bash
std_msgs/Header header              # frame_id : mission_map

string robot_id

# =========================
# 입력 상태
# =========================

bool path_progress_received     # PathProgress 메시지 수신 여부
bool path_progress_valid        # PathProgress 입력 유효성
bool pose_received              # 현재 pose 메시지 수신 여부
bool pose_valid                 # 현재 pose 입력 유효성

# =========================
# Local planner 상태
# =========================

uint8 planner_state            # local planner의 현재 상태
bool local_path_available      # 유효한 local path 생성 여부
bool stop_required             # 후속 follower/controller의 정지 여부
bool blocked                   # 장애물 or freespace gap 기반 local path 생성 실패 여부

# =========================
# Local target 정보
# =========================

# local path의 최종 target 좌표
# 장애물 회피가 없으면 PathProgress target과 동일할 수 있다.
# 장애물 회피 중이면 새로 계산한 회피 target이 될 수 있다.
float32 local_target_x_m
float32 local_target_y_m
float32 local_target_yaw_rad

# local planner가 최종적으로 선택한 heading
# mission_map 기준 절대 heading
float32 selected_heading_rad

# 현재 pose에서 local target까지의 거리
float32 distance_to_local_target_m

# 현재 yaw와 selected_heading_rad 사이의 오차
float32 local_heading_error_rad

# 현재 yaw와 local_target_yaw_rad 사이의 오차
float32 local_yaw_error_rad

# 현재 pose가 local target에 도달했는지 여부
# global goal 도착 여부가 아니라 local target 도착 여부다.
bool local_target_reached

# =========================
# 생성된 Local path 정보
# =========================

# 생성된 local path의 대략적인 길이 [m]
float32 local_path_length_m

# 생성된 local path의 pose 개수
uint32 local_path_pose_count

# =========================
# 판단 근거
# =========================

# PathProgress의 target_index를 최소 추적용으로 보관
# local planner가 어떤 global sub-goal을 기준으로 local path를 만들었는지 확인하기 위함
uint32 source_target_index

# global path의 lookahead target을 그대로 사용했는지 여부
bool used_global_sub_goal

# free_space_model.best_heading_angle_rad 기반 회피 heading을 사용했는지 여부
bool used_free_space_heading

# 장애물 회피 후 global path 복귀용 local path를 생성했는지 여부
bool used_rejoin_target

# =========================
# Free-space 회피 정보
# =========================

float32 free_space_heading_rad  # base_link 기준 free-space best heading
float32 free_space_clearance_m  # 선택된 free-space 방향의 최소 clearance [m] 
float32 free_space_score        # free_space_model이 계산한 선택 gap의 score

# 디버그용 설명 문자열
string reason

# =========================
# Planner state enum
# =========================

uint8 IDLE=0                    # 초기 상태
uint8 GLOBAL_SUB_GOAL=1         # lookahead target 기반 local path 생성 상태
uint8 AVOIDANCE=2               # free space heading 기반 local path 생성 상태
uint8 REJOIN=3                  # 장애물 회피 후 global path 복귀하기 위한 local path 생성 상태
uint8 BLOCKED=4                 # 장애물 존재 + free space gap X 기반 local path 생성 실패한 상태
uint8 INVALID_INPUT=5           # planning 수행할 수 없는 상태 (ex: pose 없음, path_progress 없음, frame_id 불일치 등)
uint8 GLOBAL_GOAL_REACHED=6
```

- 데이터 필드 의미
    
    ```
    path_available
    = 유효한 local path를 만들었는가?
    
    planner_status
    = 어떤 방식으로 path를 만들었는가?
    
    target_x_m, target_y_m
    = local path의 최종 target point
    
    selected_heading_rad
    = planner가 선택한 heading
    = mission_map 기준 또는 base_link 기준 중 하나로 확정 필요
    ```
    
    여기서는 헷갈림 방지를 위해 추천은:
    
    ```
    selected_heading_rad = mission_map 기준 최종 heading
    free_space_heading_rad = base_link 기준 회피 heading
    ```
    
    ---
    
    ```
    local_path_length_m
    = 생성된 local path 길이
    
    used_global_sub_goal
    = global path target을 사용했는가?
    
    used_free_space_heading
    = free-space best heading을 사용했는가?
    
    blocked
    = free-space가 없어서 path 생성 실패했는가?
    ```
    
    ---
    
    ```
    free_space_heading_rad
    = base_link 기준 free_space_model.best_heading_angle_rad
    
    free_space_clearance_m
    = free_space_model.best_clearance
    
    free_space_score
    = free_space_model.best_score
    ```
    
    free-space model은 candidate gap과 selected gap을 만들고, `best_heading_angle_rad`, `best_clearance`, `best_score`, free/unknown/occupied ratio 등을 출력하므로 이 상태 메시지에 일부를 복사해 두면 디버깅이 쉬워진다.
    
    ---
    
    ### planner_status enum 의미
    
    ```
    IDLE
    = 아직 planning하지 않음
    
    GLOBAL_SUB_GOAL
    = global path sub-goal 방향으로 local path 생성
    
    AVOIDANCE
    = 장애물 회피를 위해 free-space heading 기반 local path 생성
    
    REJOIN
    = global path 복귀용 local path 생성
    
    BLOCKED
    = 전방 장애물은 있는데 사용할 수 있는 free-space가 없음
    
    INVALID_INPUT
    = pose, path_progress, free_space 등 입력이 부족하거나 유효하지 않음
    ```
    

---

### 4-6. `/control/cmd_vel_raw/robot_1`

> 담당 노드 : path_follower_node
> 
> 
> 메시지 타입 : geometry_msgs/msg/TwistStamped
> 

```bash
std_msgs/Header header
geometry_msgs/Twist twist
```

- 데이터 필드 의미
    
    ```
    header.frame_id
    = "base_link"
    = 속도 명령은 로봇 본체 기준
    
    twist.linear.x
    = 목표 전진 속도 [m/s]
    
    twist.linear.y
    = 목표 좌우 속도 [m/s]
    = strafe 지원 전까지는 0.0
    
    twist.angular.z
    = 목표 yaw 각속도 [rad/s]
    ```
    
    ---
    
    ## 예시
    
    ```
    header:
      frame_id: base_link
    twist:
      linear:
        x: 0.12
        y: 0.0
      angular:
        z: 0.35
    ```
    
    의미:
    
    ```
    전진 0.12 m/s
    왼쪽으로 0.35 rad/s 회전
    ```
    

---

### 4-7. `/control/cmd_vel/robot_1`

> 담당 노드 : safety_supervisor_node
> 
> 
> 메시지 타입 : geometry_msgs/msg/TwistStamped
> 

```bash
std_msgs/Header header
geometry_msgs/Twist twist
```

Safety Supervisor가 최종 승인한 **STM으로 보내기 직전의 최종 속도 명령**으로 `cmd_vel_raw` 와 구조가 같지만 의미가 다름.

```bash
/control/cmd_vel_raw/robot_1
= path follower가 계산한 원시 속도

/control/cmd_vel/robot_1
= safety supervisor가 보정한 최종 속도
```

- 상황별 예시
    
    **A. 정상 주행**
    
    ```
    cmd_vel_raw:
    linear.x = 0.12
    angular.z = 0.20
    
    cmd_vel:
    linear.x = 0.12
    angular.z = 0.20
    ```
    
    **B. soft stop**
    
    ```
    cmd_vel_raw:
    linear.x = 0.12
    angular.z = 0.20
    
    cmd_vel:
    linear.x = 0.05
    angular.z = 0.10
    ```
    
    **C. emergency stop**
    
    ```
    cmd_vel_raw:
    linear.x = 0.12
    angular.z = 0.20
    
    cmd_vel:
    linear.x = 0.00
    angular.z = 0.00
    ```
    

---

### 4-8. `/safety/status/robot_1`

> 담당 노드 : safety_supervisor_node
> 
> 
> 메시지 타입 : robot_interfaces/msg/SafetyStatus
> 

```bash
# SafetyStatus.msg

std_msgs/Header header

string robot_id

bool safe_to_move
bool soft_stop_required
bool emergency_stop_required

bool pose_stale
bool perception_stale
bool cmd_stale
bool obstacle_too_close
bool goal_reached
bool local_path_invalid

float32 nearest_front_obstacle_m
float32 soft_stop_distance_m
float32 emergency_stop_distance_m

float32 input_linear_x_mps
float32 input_angular_z_radps

float32 output_linear_x_mps
float32 output_angular_z_radps

string reason
```

- 데이터 필드 의미
    
    ```
    safe_to_move
    = 최종적으로 움직여도 되는 상태인가?
    
    soft_stop_required
    = 감속이 필요한가?
    
    emergency_stop_required
    = 즉시 정지가 필요한가?
    
    pose_stale
    = localization pose가 오래되었는가?
    
    perception_stale
    = obstacle/free-space 데이터가 오래되었는가?
    
    cmd_stale
    = cmd_vel_raw가 오래되었는가?
    
    obstacle_too_close
    = 전방 장애물이 emergency distance 이내인가?
    
    goal_reached
    = 목표에 도착했는가?
    
    local_path_invalid
    = local path가 없거나 유효하지 않은가?
    
    nearest_front_obstacle_m
    = 현재 전방 대표 장애물까지 거리
    
    soft_stop_distance_m
    = 감속 시작 기준 거리
    
    emergency_stop_distance_m
    = 즉시 정지 기준 거리
    
    input_linear_x_mps
    = cmd_vel_raw.linear.x
    
    input_angular_z_radps
    = cmd_vel_raw.angular.z
    
    output_linear_x_mps
    = 최종 cmd_vel.linear.x
    
    output_angular_z_radps
    = 최종 cmd_vel.angular.z
    
    reason (선택)
    = 안전 판단 이유
    ```
    
    reason 예시:
    
    ```
    "safe command passed"
    "soft stop: front obstacle close"
    "emergency stop: obstacle inside threshold"
    "stop: pose stale"
    "stop: goal reached"
    ```