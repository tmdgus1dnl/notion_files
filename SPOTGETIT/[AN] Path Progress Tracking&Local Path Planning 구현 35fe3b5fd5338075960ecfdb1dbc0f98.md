# [AN] Path Progress Tracking&Local Path Planning 구현

상태: Autonomous Navigation

요약

> Global path(임무 경로) 기반으로 **“어디로 갈 것인가?”**의 Local path 생성 과정
> 

```bash
Global Path를 받아서,
현재 로봇 pose 기준으로 path 진행 상황을 계산하고,
그 결과와 LiDAR 환경 모델을 이용해 지금 따라갈 Local Path를 생성하는 파트
```

---

- GPT
    
    # 1. A 파트 전체 범위
    
    네가 맡을 노드는 크게 3개로 보면 돼.
    
    ```
    A-0. mock_global_path_publisher_node
    A-1. path_progress_tracker_node
    A-2. local_path_planner_node
    ```
    
    최종 산출 토픽은 아래 3개다.
    
    ```
    /navigation/path_progress/robot_1
    /planning/local_path/robot_1
    /planning/local_planner_status/robot_1
    ```
    
    역할 기준으로 보면 이렇게 나뉜다.
    
    ```
    mock_global_path_publisher_node
    = 테스트용 global path를 publish
    
    path_progress_tracker_node
    = global path 상에서 로봇이 어디쯤 왔는지 계산
    
    local_path_planner_node
    = 지금 당장 따라갈 짧은 local path를 생성
    ```
    
    기존 인수인계에서도 global path는 `nav_msgs/Path`로 받고, 현재 pose에서 가장 가까운 waypoint를 찾은 뒤 lookahead 거리만큼 앞의 waypoint를 target으로 잡는 구조가 핵심으로 정리되어 있어.
    
    ---
    
    # 2. A 파트 데이터 흐름
    
    전체 흐름은 이렇게 보면 된다.
    
    ```
    /planning/global_path/robot_1
    /localization/pose
            ↓
    [path_progress_tracker_node]
            ↓
    /navigation/path_progress/robot_1
            ↓
    [local_path_planner_node]
            ↑
    /navigation/state/robot_1
    /perception/lidar/obstacle_model
    /perception/lidar/free_space_model
    /localization/pose
            ↓
    /planning/local_path/robot_1
    /planning/local_planner_status/robot_1
    ```
    
    A 파트에서 가장 중요한 기준은 좌표계야.
    
    ```
    mission_map 기준:
    - global path
    - localization pose
    - path progress
    - local path
    
    base_link 기준:
    - obstacle_model
    - free_space_model
    - front / left / right
    - free_space best heading
    ```
    
    인수인계에서도 global path와 localization pose는 `mission_map`, obstacle/free-space는 로봇 기준 sector 판단으로 나눠야 한다고 되어 있어.
    
    ---
    
    # 3. A-0. mock_global_path_publisher_node
    
    ## 역할
    
    실제 관제/Global Planner가 완성되기 전까지 테스트용 global path를 만들어 publish하는 노드야.
    
    ## 입력
    
    ```
    없음
    ```
    
    ## 출력
    
    ```
    /planning/global_path/robot_1
    Type: nav_msgs/msg/Path
    frame_id: mission_map
    ```
    
    ## 해야 할 기능
    
    ```
    1. YAML 또는 파라미터로 waypoint 목록을 받는다.
    2. nav_msgs/Path 메시지를 만든다.
    3. header.frame_id를 mission_map으로 설정한다.
    4. 일정 주기 또는 latched 개념으로 global path를 publish한다.
    ```
    
    ## 예시 path
    
    ```
    (0.0, 0.0)
    (0.5, 0.0)
    (1.0, 0.0)
    (1.5, 0.0)
    (2.0, 0.5)
    (2.5, 1.0)
    ```
    
    ## 개발 목적
    
    이 노드는 기능적으로 어려운 노드는 아니고, 이후 모든 노드를 검증하기 위한 **테스트 입력 생성기**야.
    
    ---
    
    # 4. A-1. path_progress_tracker_node
    
    ## 역할
    
    이 노드는 global path와 현재 pose를 비교해서, 로봇이 path의 어디쯤 왔는지 계산한다.
    
    ```
    Global Path
    + Current Pose
    → nearest index
    → target index
    → target point
    → heading error
    → goal distance
    ```
    
    ## 입력
    
    ```
    /planning/global_path/robot_1
    Type: nav_msgs/msg/Path
    
    /localization/pose
    Type: robot_interfaces/msg/LocalizedRobotPose
    ```
    
    ## 출력
    
    ```
    /navigation/path_progress/robot_1
    Type: robot_interfaces/msg/PathProgress
    ```
    
    ---
    
    ## PathProgress.msg 추천 구조
    
    ```
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
    
    ---
    
    ## 내부 처리 흐름
    
    ```
    1. global path 수신
    2. localization pose 수신
    3. path와 pose의 frame_id가 mission_map 기준인지 확인
    4. 현재 pose와 가장 가까운 waypoint 탐색
    5. nearest_index 계산
    6. lookahead_distance_m만큼 앞의 target waypoint 선택
    7. target_heading 계산
    8. heading_error 계산
    9. distance_to_goal 계산
    10. goal_reached 판단
    11. PathProgress publish
    ```
    
    ---
    
    ## 핵심 계산
    
    ### 1) nearest_index
    
    ```
    현재 로봇 위치와 가장 가까운 global path waypoint index
    ```
    
    ```cpp
    dist = hypot(path_x - robot_x, path_y - robot_y)
    ```
    
    이 값은 “로봇이 global path의 어디쯤 왔는지”를 나타낸다.
    
    ---
    
    ### 2) target_index
    
    ```
    nearest_index보다 lookahead_distance_m만큼 앞쪽 waypoint
    ```
    
    예를 들어:
    
    ```
    nearest_index = 5
    lookahead 거리 기준으로 앞을 보면
    target_index = 8
    ```
    
    이 target point가 사실상 **global sub-goal**이야.
    
    ---
    
    ### 3) target_heading_rad
    
    ```cpp
    target_heading = atan2(target_y - current_y, target_x - current_x);
    ```
    
    ---
    
    ### 4) heading_error_rad
    
    ```cpp
    heading_error = normalizeAngle(target_heading - current_yaw);
    ```
    
    ```
    heading_error > 0 → 목표가 왼쪽
    heading_error < 0 → 목표가 오른쪽
    ```
    
    ---
    
    ### 5) distance_to_nearest_m
    
    ```
    현재 위치가 global path에서 얼마나 벗어났는지
    ```
    
    이 값은 나중에 회피 후 복귀 판단에 중요해.
    
    ```
    distance_to_nearest_m < rejoin_tolerance_m
    → global path에 복귀했다고 판단 가능
    ```
    
    ---
    
    ## 이 노드에서 하지 말아야 하는 것
    
    ```
    - 장애물 회피 판단
    - local path 생성
    - 속도 v, w 계산
    - stop 판단
    ```
    
    이 노드는 오직 **global path 진행 정보**만 만든다.
    
    ---
    
    # 5. A-2. local_path_planner_node
    
    ## 역할
    
    A 파트의 핵심 노드야.
    
    이 노드는 path progress, navigation state, localization pose, obstacle/free-space 정보를 이용해서 **지금 당장 따라갈 짧은 local path**를 만든다.
    
    ## 입력
    
    ```
    /navigation/path_progress/robot_1
    Type: robot_interfaces/msg/PathProgress
    
    /navigation/state/robot_1
    Type: robot_interfaces/msg/NavigationState
    
    /localization/pose
    Type: robot_interfaces/msg/LocalizedRobotPose
    
    /perception/lidar/obstacle_model
    Type: robot_interfaces/msg/ObstacleModel
    
    /perception/lidar/free_space_model
    Type: robot_interfaces/msg/FreeSpaceModel
    ```
    
    ## 출력
    
    ```
    /planning/local_path/robot_1
    Type: nav_msgs/msg/Path
    
    /planning/local_planner_status/robot_1
    Type: robot_interfaces/msg/LocalPlannerStatus
    ```
    
    ---
    
    ## LocalPlannerStatus.msg 추천 구조
    
    ```
    std_msgs/Header header
    
    string robot_id
    
    bool path_available
    
    uint8 planner_status
    
    float32 target_x_m
    float32 target_y_m
    
    float32 selected_heading_rad
    float32 local_path_length_m
    
    bool used_global_sub_goal
    bool used_free_space_heading
    bool blocked
    
    float32 free_space_heading_rad
    float32 free_space_clearance_m
    float32 free_space_score
    
    string reason
    
    uint8 IDLE=0
    uint8 GLOBAL_SUB_GOAL=1
    uint8 AVOIDANCE=2
    uint8 REJOIN=3
    uint8 BLOCKED=4
    uint8 INVALID_INPUT=5
    ```
    
    ---
    
    # 6. local_path_planner_node 내부 동작 모드
    
    local planner는 크게 4가지 모드로 동작하면 된다.
    
    ```
    1. GLOBAL_SUB_GOAL 모드
    2. AVOIDANCE 모드
    3. REJOIN 모드
    4. BLOCKED 모드
    ```
    
    NavigationState에 따른 local path 생성 방식 :
    
    ```bash
    NAV_TRACKING_GLOBAL_PATH
    → global sub-goal 방향 local path 생성
    
    NAV_AVOIDING_OBSTACLE
    → free-space heading 기반 회피 local path 생성
    
    NAV_REJOINING_GLOBAL_PATH
    → global path 복귀용 local path 생성
    
    NAV_STOPPED_BY_OBSTACLE
    → local path 생성 중단 또는 BLOCKED status publish
    ```
    
    ---
    
    ## 6-1. GLOBAL_SUB_GOAL 모드
    
    ## 조건
    
    ```
    전방 장애물 없음
    또는 NavigationState == NAV_TRACKING_GLOBAL_PATH
    ```
    
    ## 동작
    
    ```
    PathProgress의 target_x_m, target_y_m을 local target으로 사용
    ```
    
    ## 출력
    
    ```
    /planning/local_path/robot_1
    → global sub-goal 방향의 짧은 local path
    
    /planning/local_planner_status/robot_1
    → planner_status = GLOBAL_SUB_GOAL
    ```
    
    ## 의미
    
    장애물이 없을 때도 local path는 생성한다.
    
    이렇게 해야 B 파트의 `path_follower_node`는 항상 `/planning/local_path/robot_1`만 보면 된다.
    
    ---
    
    ## 6-2. AVOIDANCE 모드
    
    ## 조건
    
    ```
    전방 장애물 있음
    free_space_model.path_available == true
    NavigationState == NAV_AVOIDING_OBSTACLE
    또는 NAV_PLANNING_LOCAL_PATH
    ```
    
    현재 obstacle model은 front/left/right 섹터별 대표 장애물을 선택하고 가장 가까운 장애물 정보를 요약한다.
    
    free-space model은 candidate gap 중 selected gap과 best heading, best clearance, best score를 산출하므로 회피 방향 선택에 활용할 수 있다.
    
    ## 동작
    
    ```
    free_space_model.best_heading_angle_rad를 회피 heading으로 사용
    ```
    
    단, 이 heading은 `base_link` 기준이다.
    
    따라서 `mission_map` 기준 target으로 바꿔야 한다.
    
    ```cpp
    global_heading = current_yaw + free_space_best_heading_rad;
    
    target_x = current_x + local_target_distance_m * cos(global_heading);
    target_y = current_y + local_target_distance_m * sin(global_heading);
    ```
    
    ## 출력
    
    ```
    /planning/local_path/robot_1
    → 회피 방향 local path
    
    /planning/local_planner_status/robot_1
    → planner_status = AVOIDANCE
    → used_free_space_heading = true
    ```
    
    ---
    
    ## 6-3. REJOIN 모드
    
    ## 조건
    
    ```
    NavigationState == NAV_REJOINING_GLOBAL_PATH
    ```
    
    ## 동작
    
    회피 후 global path로 돌아가기 위해, 현재 위치 기준 nearest global path point보다 약간 앞쪽 지점을 target으로 잡는다.
    
    ```
    rejoin_target_index = nearest_index + rejoin_target_offset_index
    ```
    
    그리고 그 지점을 local target으로 사용한다.
    
    ## 출력
    
    ```
    /planning/local_path/robot_1
    → global path 복귀용 local path
    
    /planning/local_planner_status/robot_1
    → planner_status = REJOIN
    ```
    
    ## 주의점
    
    rejoin 판단 자체는 B 파트의 Navigation FSM이 하되,
    
    A 파트는 `PathProgress.nearest_index`, `target_index`, `distance_to_nearest_m`을 이용해서 복귀용 target을 만들어줄 수 있어야 한다.
    
    ---
    
    ## 6-4. BLOCKED 모드
    
    ## 조건
    
    ```
    전방 장애물 있음
    free_space_model.path_available == false
    ```
    
    ## 동작
    
    억지로 local path를 만들지 않는다.
    
    ```
    path_available = false
    planner_status = BLOCKED
    reason = "front blocked and no free-space gap"
    ```
    
    ## 출력
    
    ```
    /planning/local_path/robot_1
    → 빈 path 또는 publish 생략
    
    /planning/local_planner_status/robot_1
    → planner_status = BLOCKED
    ```
    
    free-space model은 angular bin 기반으로 free/unknown/occupied 통계와 clearance를 계산해 candidate gap을 만들기 때문에, `path_available=false`는 현재 LiDAR 기준으로 신뢰할 만한 통과 방향을 찾지 못했다는 의미로 보는 게 맞다.
    
    ---
    
    # 7. local path 생성 방식
    
    처음에는 복잡한 곡선 경로를 만들 필요 없어.
    
    직선 보간 local path로 시작하면 된다.
    
    ## 입력
    
    ```
    start = current pose
    target = 선택된 local target
    ```
    
    ## 생성 방식
    
    ```
    1. 현재 pose를 시작점으로 둔다.
    2. target_x, target_y를 끝점으로 둔다.
    3. 그 사이를 local_path_waypoint_count개로 선형 보간한다.
    4. nav_msgs/Path로 publish한다.
    ```
    
    ## 예시
    
    ```
    current = (0.0, 0.0)
    target  = (0.8, 0.2)
    waypoint_count = 5
    
    local path:
    P0 = (0.0, 0.0)
    P1 = (0.2, 0.05)
    P2 = (0.4, 0.10)
    P3 = (0.6, 0.15)
    P4 = (0.8, 0.20)
    ```
    
    이렇게만 해도 B 파트의 path follower가 충분히 따라갈 수 있다.
    
    ---
    
    # 8. A 파트 개발 순서
    
    ## Step 0. 메시지와 토픽 계약 확정
    
    먼저 아래 메시지 2개를 확정한다.
    
    ```
    PathProgress.msg
    LocalPlannerStatus.msg
    ```
    
    A 파트가 직접 publish하는 custom msg이기 때문에 네가 주도해서 확정하면 된다.
    
    ---
    
    ## Step 1. mock_global_path_publisher_node 개발
    
    출력:
    
    ```
    /planning/global_path/robot_1
    ```
    
    검증:
    
    ```bash
    ros2 topic echo /planning/global_path/robot_1
    ```
    
    확인:
    
    ```
    header.frame_id == mission_map
    poses 개수 정상
    waypoint 좌표 정상
    ```
    
    ---
    
    ## Step 2. path_progress_tracker_node 개발
    
    입력:
    
    ```
    /planning/global_path/robot_1
    /localization/pose
    ```
    
    출력:
    
    ```
    /navigation/path_progress/robot_1
    ```
    
    검증:
    
    ```bash
    ros2 topic echo /navigation/path_progress/robot_1
    ```
    
    확인:
    
    ```
    nearest_index가 로봇 위치에 따라 바뀌는가?
    target_index가 nearest_index보다 앞에 있는가?
    target_x_m, target_y_m이 정상인가?
    distance_to_goal_m이 줄어드는가?
    goal_reached가 정상적으로 true가 되는가?
    ```
    
    ---
    
    ## Step 3. local_path_planner_node — global sub-goal 모드 먼저 개발
    
    처음에는 obstacle/free-space 없이 간다.
    
    입력:
    
    ```
    /navigation/path_progress/robot_1
    /localization/pose
    ```
    
    출력:
    
    ```
    /planning/local_path/robot_1
    /planning/local_planner_status/robot_1
    ```
    
    검증:
    
    ```
    장애물 없이 global path 방향으로 local path가 생성되는가?
    local path frame_id가 mission_map인가?
    local path 마지막 점이 path_progress target 방향인가?
    ```
    
    ---
    
    ## Step 4. local_path_planner_node — avoidance 모드 추가
    
    입력 추가:
    
    ```
    /perception/lidar/obstacle_model
    /perception/lidar/free_space_model
    /navigation/state/robot_1
    ```
    
    기능 추가:
    
    ```
    front_blocked 확인
    free_space_model.path_available 확인
    best_heading_angle_rad 사용
    base_link heading → mission_map target 변환
    ```
    
    검증:
    
    ```
    전방 장애물 있음 + free-space 있음
    → 회피 방향 local path 생성
    
    전방 장애물 있음 + free-space 없음
    → LocalPlannerStatus = BLOCKED
    ```
    
    ---
    
    ## Step 5. rejoin 모드 추가
    
    입력:
    
    ```
    /navigation/state/robot_1
    /navigation/path_progress/robot_1
    ```
    
    기능:
    
    ```
    NAV_REJOINING_GLOBAL_PATH 상태에서
    nearest_index + rejoin_target_offset_index 지점을 target으로 local path 생성
    ```
    
    검증:
    
    ```
    로봇이 global path에서 벗어난 위치에 있어도
    가장 가까운 global path 지점보다 앞쪽으로 local path가 생성되는가?
    ```
    
    ---
    
    ## Step 6. RViz 검증
    
    RViz에서 최소 3개를 같이 봐야 한다.
    
    ```
    1. /planning/global_path/robot_1
    2. /planning/local_path/robot_1
    3. /localization/pose 또는 TF base_link
    ```
    
    장애물 회피 검증 시에는 추가로:
    
    ```
    4. /perception/lidar/obstacle_model
    5. /perception/lidar/free_space_model
    ```
    
    ---
    
    # 9. B 파트와의 인터페이스 계약
    
    A 파트가 B 파트에게 넘겨야 하는 것은 이거야.
    
    ```
    /navigation/path_progress/robot_1
    /planning/local_path/robot_1
    /planning/local_planner_status/robot_1
    ```
    
    B 파트가 A 파트에게 넘겨야 하는 것은 이거야.
    
    ```
    /navigation/state/robot_1
    ```
    
    즉, 관계는 이렇게 된다.
    
    ```
    A → B:
    PathProgress
    LocalPath
    LocalPlannerStatus
    
    B → A:
    NavigationState
    ```
    
    단, 개발 초기에는 B의 `/navigation/state/robot_1`이 없어도 A 파트가 독립적으로 테스트 가능해야 한다.
    
    그래서 local_path_planner_node에는 임시 파라미터를 두면 좋아.
    
    ```yaml
    use_navigation_state: false
    ```
    
    이 경우에는 local planner가 직접 obstacle/free-space를 보고 모드를 선택하게 한다.
    
    ```
    front_blocked false → GLOBAL_SUB_GOAL
    front_blocked true + free-space 있음 → AVOIDANCE
    front_blocked true + free-space 없음 → BLOCKED
    ```
    
    나중에 B 파트가 완성되면:
    
    ```yaml
    use_navigation_state: true
    ```
    
    로 바꿔서 NavigationState를 따르게 하면 된다.
    
    ---
    
    # 10. A 파트 YAML 파라미터 초안
    
    ## path_progress_tracker.param.yaml
    
    ```yaml
    path_progress_tracker_node:
      ros__parameters:
        robot_id: "robot_1"
    
        global_path_topic: "/planning/global_path/robot_1"
        localization_pose_topic: "/localization/pose"
        path_progress_topic: "/navigation/path_progress/robot_1"
    
        map_frame: "mission_map"
    
        lookahead_distance_m: 0.5
        goal_tolerance_m: 0.3
    
        nearest_search_window: 30
        allow_backward_index_jump: false
    
        pose_timeout_sec: 0.5
        path_timeout_sec: 2.0
    
        publish_rate_hz: 10.0
    ```
    
    ---
    
    ## local_path_planner.param.yaml
    
    ```yaml
    local_path_planner_node:
      ros__parameters:
        robot_id: "robot_1"
    
        path_progress_topic: "/navigation/path_progress/robot_1"
        navigation_state_topic: "/navigation/state/robot_1"
        localization_pose_topic: "/localization/pose"
        obstacle_model_topic: "/perception/lidar/obstacle_model"
        free_space_model_topic: "/perception/lidar/free_space_model"
    
        local_path_topic: "/planning/local_path/robot_1"
        local_planner_status_topic: "/planning/local_planner_status/robot_1"
    
        map_frame: "mission_map"
    
        use_navigation_state: false
    
        local_target_distance_m: 0.7
        local_path_waypoint_count: 5
    
        front_block_distance_m: 0.7
        min_free_space_clearance_m: 0.45
    
        max_avoidance_heading_rad: 0.9
        avoidance_heading_gain: 1.0
    
        rejoin_target_offset_index: 5
    
        pose_timeout_sec: 0.5
        perception_timeout_sec: 0.5
    
        publish_rate_hz: 10.0
    ```
    
    ---
    
    # 11. A 파트 완료 기준
    
    A 파트가 완료됐다고 볼 수 있는 기준은 아래야.
    
    ```
    1. /planning/global_path/robot_1이 publish된다.
    
    2. /navigation/path_progress/robot_1에서
       nearest_index, target_index, distance_to_goal_m이 정상적으로 나온다.
    
    3. 장애물이 없을 때
       /planning/local_path/robot_1이 global path 방향으로 생성된다.
    
    4. 전방 장애물 + free-space 있음일 때
       /planning/local_path/robot_1이 free-space best heading 방향으로 생성된다.
    
    5. 전방 장애물 + free-space 없음일 때
       /planning/local_planner_status/robot_1이 BLOCKED를 낸다.
    
    6. 로봇이 global path에서 벗어난 위치에 있어도
       nearest global path point를 다시 잡고 rejoin target을 만들 수 있다.
    
    7. RViz에서 global path와 local path가 동시에 확인된다.
    ```
    
    ---
    
    # 12. 최종 요약
    
    네가 맡은 A 파트는 이렇게 정리하면 된다.
    
    ```
    A 파트의 목표:
    Global Path와 현재 Pose, LiDAR 환경 모델을 이용해서
    로봇이 지금 따라갈 Local Path를 생성하는 것
    ```
    
    구성 노드:
    
    ```
    1. mock_global_path_publisher_node
       - 테스트용 global path publish
    
    2. path_progress_tracker_node
       - global path 상 현재 진행 위치 계산
       - /navigation/path_progress/robot_1 publish
    
    3. local_path_planner_node
       - global sub-goal / avoidance / rejoin 상황에 맞는 local path 생성
       - /planning/local_path/robot_1 publish
       - /planning/local_planner_status/robot_1 publish
    ```
    
    핵심 출력:
    
    ```
    /navigation/path_progress/robot_1
    = global path 어디쯤 왔는지
    
    /planning/local_path/robot_1
    = 지금 당장 따라갈 짧은 path
    
    /planning/local_planner_status/robot_1
    = local path를 어떻게 만들었는지, 실패했다면 왜 실패했는지
    ```
    
    즉, 네 파트의 정체성은 이거야.
    
    ```
    “로봇이 어디로 가야 하는지”를 만들어 B 파트에 넘겨주는 영역
    ```
    
    B 파트는 네가 만든 `/planning/local_path/robot_1`을 받아서 `cmd_vel_raw`, `cmd_vel`을 만드는 구조로 이어지면 된다.
    
- 
1. 두번째는 gait_odom_estimator_node.cpp에서 odom을 계산할 때 실측 값을 재보니 전진 상황에서 제어의 입력인 선속도를 0.05m/s를 주었을 때 10cycles 동안 50cm 이동했고, 0.1m/s를 주었을 때는 10cycles 동안 114cm 이동했고 0.3m/s를 주었을 때는 10cycles 동안 120cm 이동했고, 0.3m/s 이상의 선속도들은 10cycles 기준 모두 120cm 이동이 최대더라고. 그래서 이 값에 따라 일차식을 구해서 선속도에 따른 odom 데이터를 주입해야할 거 같다.

```bash
const double prev_yaw_rad = odom_yaw_rad_;
const double new_yaw_rad = odom_yaw_rad_from_imu;

const double delta_yaw_rad =
    normalizeAngle(new_yaw_rad - prev_yaw_rad);

const double mid_yaw_rad =
    normalizeAngle(prev_yaw_rad + 0.5 * delta_yaw_rad);

const double delta_odom_x =
    delta_body_x_m * std::cos(mid_yaw_rad)
    - delta_body_y_m * std::sin(mid_yaw_rad);

const double delta_odom_y =
    delta_body_x_m * std::sin(mid_yaw_rad)
    + delta_body_y_m * std::cos(mid_yaw_rad);

odom_x_m_ += delta_odom_x;
odom_y_m_ += delta_odom_y;
odom_yaw_rad_ = new_yaw_rad;
```

[최종 결과]

![image.png](image%2053.png)

## 1. Message Type

### 1-1. PathProgress.msg

> Publish : `path_progress_tracker_node`
> 
> 
> Subscribe : `navigation_fsm_node` , `local_path_planner_node` 
> 

```bash
# frame_id  : mission_map
# timestamp : path progress 계산 시각
std_msgs/Header header     

string robot_id

bool path_received              # global path 메시지 수신 여부
bool pose_received              # localization pose 메시지 수신 여부
bool path_valid                 # 수신한 global path 유효 여부
bool pose_valid                 # 수신한 localization pose 유효 여부

uint32 nearest_index            # 가장 가까운 global path waypoint index
uint32 target_index             # nearest_index보다 앞쪽에 있는 lookahead target waypoint index
uint32 total_waypoints          # global path 전체 waypoint 개수

float32 progress_ratio          # global path 진행률 (0.0 ~ 1.0)

float32 nearest_x_m             # 가장 가까운 global path waypoint의 x 좌표
float32 nearest_y_m             # 가장 가까운 global path waypoint의 y 좌표

float32 target_x_m              # lookahead target waypoint의 x 좌표
float32 target_y_m              # lookahead target waypoint의 y 좌표
float32 target_heading_rad      # 현재 로봇 위치에서 target waypoint를 바라보는 방향각

float32 heading_error_rad       # 현재 로봇 yaw와 target_heading_rad의 차이
float32 distance_to_nearest_m   # 현재 로봇 위치와 nearest waypoint 사이의 거리
float32 distance_to_target_m    # 현재 로봇 위치와 target waypoint 사이의 거리
float32 distance_to_goal_m      # 현재 로봇 위치와 global path의 마지막 waypoint 사이의 거리

bool goal_reached               # goal에 도달했는지 여부
```

- 역할 :
    - 로봇이 **1) 할당받은 global path 상에서 현재 어디쯤 위치**하는지,
    - 다음에 **2) 추종해야 할 target waypoint가 어디**인지,
    - **3) goal까지 얼마나 남았는지**를 나타내는 진행 상태 메시지.
- 좌표계 :
    - header.frame_id는 반드시 "mission_map"을 사용한다.
    - nearest_x_m, target_x_m 등 모든 위치 좌표는 mission_map 기준이다

### 1-2. LocalPlannerStatus.msg

> Publish : `local_path_planner_node`
> 
> 
> Subscribe : `navigation_fsm_node` , `debugging / monitoring`
> 

```bash
std_msgs/Header header              # frame_id : mission_map

string robot_id

# 이번 cycle에서 유효한 local path를 생성했는지 여부
# - true  : /planning/local_path/spot_01에 따라갈 path가 존재
# - false : local path를 만들 수 없거나 입력 데이터가 부족한 상태
bool path_available                 

uint8 planner_status            # local path 생성 상태값

float32 target_x_m              # local path의 최종 target x 좌표
float32 target_y_m              # local path의 최종 target y 좌표

float32 selected_heading_rad    # local planner가 최종적으로 선택한 heading
float32 local_path_length_m     # 생성된 local path의 대략적인 길이

# global path의 lookahead target 기반 local path 생성 여부
bool used_global_sub_goal

# free_space_model.best_heading_angle_rad 기반 회피 local path 생성 여부
bool used_free_space_heading

# 전방 장애물 O && !free-space gap 기반 local path 생성에 실패했는지 여부.
bool blocked

float32 free_space_heading_rad  # free_space_model에서 받은 base_link 기준 best heading
float32 free_space_clearance_m  # 선택된 free-space 방향의 최소 clearance [m] 
float32 free_space_score        # free_space_model이 계산한 선택 gap의 score

string reason

uint8 IDLE=0                    # 초기 상태
uint8 GLOBAL_SUB_GOAL=1         # lookahead target 기반 local path 생성 상태
uint8 AVOIDANCE=2               # free space heading 기반 local path 생성 상태
uint8 REJOIN=3                  # 장애물 회피 후 global path 복귀하기 위한 local path 생성 상태
uint8 BLOCKED=4                 # 장애물 존재 + free space gap X 기반 local path 생성 실패한 상태
uint8 INVALID_INPUT=5           # planning 수행할 수 없는 상태 (ex: pose 없음, path_progress 없음, frame_id 불일치 등)
```

```bash
1. planner_status : 어떤 방식으로 만들었는가?
2. path_available : 따라갈 path가 있는가?
3. blocked : 막혀서 path를 못 만들었는가?
```

- 역할 :
    - local_path_planner_node가 이번 cycle에서 local path를 생성했는지
    - 어떤 방식으로 생성했는지
    - 실패했다면 왜 실패했는지를 보고하는 메시지
- 주의 사항 :
    - 이 메시지는 로봇 전체 자율보행 FSM 상태가 아니다
    - NavigationState : navigation_fsm_node가 publish하는 로봇 주행 상태
    - LocalPlannerStatus: local_path_planner_node의 planning 결과 보고서
- 좌표계 :
    - target_x_m, target_y_m은 mission_map 기준이다.
    - selected_heading_rad : mission_map 기준 최종 heading
    - free_space_heading_rad : base_link 기준 free-space heading

---

## 2. Path progress tracker

> 
> 
> 
> 현재 로봇 pose가 global path 위에서 어디쯤 진행 중인지 계산하고, 다음 단계 노드가 따라갈 기준 target point를 알려주는 노드
> 
> 할당받은 Global Path 위에서 **현재 Spot의 pose가 어디쯤 진행 중**인지 계산하는 과정
> 

```bash
Input : 
	- /planning/mock_global_path/spot_01 (Type: robot_interfaces/msg/GlobalPathWaypoints)
	- /localization/pose (Type: robot_interfaces/msg/LocalizatedRobotPose)
Output : 
	- /navigation/path_progress/spot_01(Type: robot_interfaces/msg/PathProgress)
```

```bash
1. 현재 로봇이 global path에서 가장 가까운 waypoint는 몇 번인가?
2. 현재 기준으로 몇 m 앞을 target waypoint로 볼 것인가?
3. target point는 어디인가?
4. target을 바라보려면 현재 yaw에서 얼마나 돌아야 하는가?
5. goal까지 얼마나 남았는가?
6. goal에 도착했는가?
7. path/pose 입력은 정상인가?
```

- 현재 로봇 pose 기준으로 **nearest waypoint**, **goal까지 거리**, **heading error**, **goal 도착 여부** 계산

### 2-1. 기본 구현

#### 1️⃣ nearest_index 계산

> 현재 pose와 global path waypoint 사이의 거리를 모두 계산해서 가장 가까운 index 찾는다.
> 

```bash
distance = hypot(waypoint.x_m - current_x, waypoint.y_m - current_y)
```

결과 :

```bash
nearest_index
nearest_x_m
nearest_y_m
distance_to_nearest_m
```

---

#### 2️⃣ target_index 계산

> `nearest_index` 부터 앞으로 가면서 누적 거리가 `lookahead_distance_m` 이상이 되는 waypoint를 target으로 잡는다.
> 

💡Point : 

현재 위치와 가장 가까운 global path 지점을 찾고,
그 지점부터 global path 진행 방향을 따라 0.5m를 넘는 첫 waypoint 지점을 target으로 잡는다

예시 :

```bash
[global path waypoint]

	P0 = (2.00, 1.00)
	P1 = (2.33, 1.33)
	P2 = (2.67, 1.67)
	P3 = (3.00, 2.00)
	P4 = (3.00, 2.50)
	P5 = (3.00, 3.00)
```

현재 Spot pose가 `P0` 근처에 있다고 하면:

```
nearest_index = 0
lookahead_distance_m = 0.5
```

이제 `P0`부터 앞으로 거리 누적 진행.

```
P0 → P1 거리 ≈ 0.47m
누적 거리 = 0.47m
아직 0.5m보다 작음

P1 → P2 거리 ≈ 0.48m
누적 거리 = 0.95m
0.5m 이상이 됨

따라서 target_index = 2
```

즉, target은 `P2`가 된다.

```
nearest_index = 0
target_index = 2
```

결과 :

```bash
target_index
target_x_m
target_y_m
distance_to_target_m
```

- 고도화 Point
    
    코너 감속, goal 방향 정렬, waypoint 자세 추종에 target_yaw_rad 활용
    

---

#### 3️⃣ target_heading 계산

> 현재 pose에서 target point를 바라보는 방향각
> 

```bash
target_heading_rad = atan2(target_y - current_y, target_x - current_x);
```

---

#### 4️⃣ heading_error 계산

> 현재 yaw와 target heading 차이
> 

```bash
heading_error_rad = normalizeAngle(target_heading_rad - current_yaw);
```

의미 :

```bash
heading_error_rad > 0 → 목표가 로봇 기준 왼쪽
heading_error_rad < 0 → 목표가 로봇 기준 오른쪽
```

---

#### 5️⃣ distance_to_goal, goal_reached 계산

> 현재 pose와 마지막 waypoint 사이 거리
> 

```bash
// 마지막 waypoint과의 거리
distance_to_goal_m = hypot(goal_x - current_x, goal_y - current_y);

// 마지막 waypoint 도달 여부
goal_reached = distance_to_goal_m < goal_tolerance_m;
```

### 2-2. global path와 localization pose 실시간 관리

Path 관리 :

```bash
path_received_
→ 메시지를 받은 적이 있는가?

isPathValid()
→ 받은 메시지가 계산 가능한 상태인가?

latest_path_
→ 실제 계산에 사용할 최신 path 데이터는 무엇인가?
```

Pose 관리 :

```bash
pose_received_
→ pose를 받은 적이 있는가?

isPoseValid()
→ pose가 frame, robot_id, timeout 기준으로 유효한가?

latest_pose_
→ 실제 계산에 사용할 최신 pose 데이터는 무엇인가?
```

### 2-3. 결과

![image.png](image%2054.png)

![image.png](image%2055.png)

[Noise]

![image.png](image%2056.png)

- 초록색 구체: nearest waypoint
- 노란색 구체: target waypoint
- 빨간색 구체: goal waypoint
- 파란색 선: 현재 pose → target point
- 흰색 텍스트: nearest_index, target_index, progress, goal 상태

### 2-4. 출력 데이터 분류

| 후속 노드 | 주로 사용하는 PathProgress 데이터 | 사용 목적 |
| --- | --- | --- |
| `local_path_planner_node` | `path_valid`, `pose_valid`, `target_x_m`, `target_y_m`, `target_yaw_rad`, `target_heading_rad`, `heading_error_rad`, `distance_to_target_m`, `goal_reached` | 현재 위치에서 target까지 갈 local path 생성 |
| `navigation_fsm_node` | `path_received`, `pose_received`, `path_valid`, `pose_valid`, `progress_ratio`, `distance_to_goal_m`, `goal_reached` | 주행 가능/대기/도착/오류 상태 판단 |
| `path_follower_node` 또는 control 계층 | `heading_error_rad`, `yaw_error_rad`, `distance_to_target_m`, 향후 local path | 속도/회전 명령 생성 |
| `debug_visualizer_node` | 거의 전체 | RViz 시각화 |
| 관제 UI | `progress_ratio`, `distance_to_goal_m`, `goal_reached`, valid 상태 | 임무 진행 상태 표시 |

#### 1️⃣ 입력 상태 진단 데이터

> 후속 노드가 **계산을 진행해도 되는지 판단하는 gate 역할**
> 
> 
> 활용하는 노드 : `Local Path Planner`
> 

```bash
bool path_received      # global path 메시지 수신 여부
bool pose_received      # localization pose 메시지 수신 여부
bool path_valid         # 수신한 global path 유효 여부
bool pose_valid         # 수신한 localization pose 유효 여부
uint32 total_waypoints    # global path 전체 waypoint 개수
```

**[Local Path Planner]**

```bash
path_valid == false
→ local path 생성하지 않음

pose_valid == false
→ 현재 위치를 믿을 수 없으므로 local path 생성하지 않음

total_waypoints == 0
→ 따라갈 global path 없음
```

즉, 이 값들은 제어용 값이라기보다는 **상태 안정성 검증용 값**

---

#### 2️⃣ 현재 Path 진행 위치 데이터

> 현재 로봇이 **global path에서 어느 지점에 있는지** 확인하는 역할
> 
> 
> 활용하는 노드 : `Local Path Planner`
> 

```bash
uint32 nearest_index            # 가장 가까운 global path waypoint index
float32 distance_to_nearest_m   # 현재 로봇 위치와 nearest waypoint 사이의 거리
float32 nearest_x_m             # 가장 가까운 global path waypoint의 x 좌표
float32 nearest_y_m             # 가장 가까운 global path waypoint의 y 좌표
```

의미 :

```bash
**nearest_index**
= 현재 pose와 가장 가까운 global path waypoint index

distance_to_nearest_m
= 현재 pose와 nearest waypoint 사이의 거리

nearest_x_m, nearest_y_m
= nearest waypoint의 좌표
```

- 핵심 데이터 : `nearest_index`

**[Local Path Planner]**

```bash
1. path 진행률 판단
2. local path를 생성할 때 시작 기준점 판단
3. 로봇이 path에서 너무 벗어났는지 판단
4. debug/RViz 시각화
```

`Local Path Planner`가 실제로 따라갈 목표점으로 쓰기에는 `nearest`보다 `target`이 더 중요함.

---

#### 3️⃣ 다음 목표점 데이터 - Local Path Planner의 핵심 입력

> global path에서의 현위치(nearest point)에서 다음 목표지점(target point)에 대한 정보
> 
> 
> 활용하는 노드 : `Local Pathe Planner`
> 

```bash
uint32 target_index             # nearest_index보다 앞쪽에 있는 lookahead target waypoint index
float32 distance_to_target_m    # 현재 로봇 위치와 target waypoint 사이의 거리
float32 target_x_m              # lookahead target waypoint의 x 좌표
float32 target_y_m              # lookahead target waypoint의 y 좌표
float32 target_yaw_rad          # target_index에 해당하는 global path waypoint의 yaw_rad 값
```

의미 :

```bash
target_index
= nearest_index 기준 lookahead 거리 앞쪽의 waypoint index

target_x_m, target_y_m
= 이번 cycle에서 로봇이 향해야 할 local target point

target_yaw_rad
= target waypoint에서 기대하는 목표 yaw 방향

distance_to_target_m
= 현재 pose에서 target point까지의 거리
```

- `Path Progress Tracker` 내부에서도 nearest index를 구한 뒤 target index를 계산하고, target waypoint의 좌표와 yaw를 꺼내 거리와 방향 계산에 사용

**[Local Path Planner]**

```bash
"지금 global path를 따라가려면 일단 여기까지 가라"
```

- 현재 pose → target_x_m, target_y_m를 잇는 짧은 local path를 생성하면 된다.

---

#### 4️⃣ 방향 오차 데이터 - Controller/Planner에 중요

> 실질적인 방향 판단에 활용되는 데이터
> 
> 
> 활용하는 노드 : `Path Follwer` 
> 

```bash
float32 target_heading_rad      # 현재 로봇 위치에서 target waypoint를 바라보는 방향각
float32 heading_error_rad       # target waypoint 향하기 위한 회전 오차
float32 yaw_error_rad           # target waypoint의 yaw_rad 자세에 맞추기 위한 회전 오차
```

의미 :

```bash
target_heading_rad
= 현재 위치에서 target point를 바라보는 방향각

**heading_error_rad**
= 현재 yaw와 target_heading_rad의 차이

yaw_error_rad
= 현재 yaw와 target_yaw_rad의 차이
```

- 핵심 데이터 : `heading_error_rad`으로 “target 좌표를 향하기 위해 얼마나 회전해야 하는가?”를 계산
    
    ```bash
    heading_error_rad ≈ 0
    → 현재 방향이 target 쪽을 잘 보고 있음
    
    heading_error_rad > 0
    → 왼쪽으로 회전 필요
    
    heading_error_rad < 0
    → 오른쪽으로 회전 필요
    ```
    
- `yaw_error_rad`는 **target waypoint가 가진 자세 방향**과 **현재 yaw**의 차이라서, 최종 자세 정렬이나 goal 근처 정렬에 더 가깝게 쓸 수 있다.

---

#### 5️⃣ mission 진행 상태 데이터

> 현재 임무(global path)에 대한 진행 상태를 표시하기 위함
활용하는 노드 : `Navigation FSM`, `Mission Manager`, `Controller`
> 

```bash
float32 progress_ratio          # global path 진행률 (0.0 ~ 1.0)
float32 distance_to_goal_m      # 현재 로봇 위치와 global path의 마지막 waypoint 사이의 거리
bool goal_reached               # goal에 도달했는지 여부
```

의미 :

```bash
progress_ratio
= global path 진행률, 0.0 ~ 1.0

distance_to_goal_m
= 현재 pose에서 마지막 waypoint까지 거리

goal_reached
= goal_tolerance_m 이내 도착 여부
```

**[`Navigation FSM`, `Mission Manager`, `Controller`]**

```bash
goal_reached == true
→ local path 생성 중단
→ stop command 또는 mission complete 상태 전환

distance_to_goal_m < 특정 임계값
→ 속도 줄이기
→ goal approach mode 진입

progress_ratio
→ UI/관제 화면에 진행률 표시
→ mission 상태 판단
```

- 현재 코드에서도 `distance_to_goal_m <= goal_tolerance_m_`일 때 `goal_reached`를 true로 만들고, `nearest_index / (waypoints.size() - 1)` 기반으로 `progress_ratio`를 계산해 publish한다.

---

## 3. Local Path Planner

> PathProgress를 기반으로 현재 로봇이 당장 따라갈 짧은 local path or local target을 생성하는 노드
> 

```bash
Input :
	- /navigation/path_progress/spot_01

Output :
	- /navigation/local_path/spot_01
		Type: nav_msgs/msg/Path
		
	- /navigation/local_planner_status/spot_01
		Type: robot_interfaces/msg/LocalPlannerStatus
```

```bash
[입력]
/navigation/path_progress/spot_01        (PathProgress)
/localization/pose or /localization/mock_pose
/perception/lidar/obstacle_model         (추후)
/perception/lidar/free_space_model       (추후)

[출력 1]
/navigation/local_path/spot_01
Type: nav_msgs/msg/Path

[출력 2]
/navigation/local_planner_status/spot_01
Type: LocalPlannerStatus
```

[아키텍처]

```bash
/planning/mock_global_path/spot_01
        ↓
mock_localization_pose_publisher_node
        ↓
/localization/mock_pose

/planning/mock_global_path/spot_01
/localization/mock_pose
        ↓
path_progress_tracker_node
        ↓
/navigation/path_progress/spot_01

/navigation/path_progress/spot_01
/localization/mock_pose
        ↓
local_path_planner_node
        ↓
/navigation/local_path/spot_01
/navigation/local_planner_status/spot_01
```

### 3-1 . `PathProgress` vs `LocalPlannerStatus` vs `local_path`

#### 1️⃣`PathProgress`

역할:

```
global path 기준 현재 진행 상태 계산
```

질문에 답함:

```
나는 global path에서 어디쯤 있는가?
다음 global target은 어디인가?
goal까지 얼마나 남았는가?
```

---

#### 2️⃣`LocalPlannerStatus`

역할:

```
local planner의 판단 결과 상태 제공
```

질문에 답함:

```
나는 지금 어떤 local planning mode인가?
local path는 유효한가?
정지해야 하는가?
회피 중인가?
local target은 무엇인가?
```

---

#### 3️⃣`local_path` (`nav_msgs/Path`)

역할:

```
실제로 따라갈 순간적인 local 경로
```

질문에 답함:

```
그럼 실제로 어떤 점들을 따라가면 되는가?
```

### 3-2. Pure Traget Following Local Path - 1차

> 장애물을 고려하지 않고, 오로지 target x, y 기반의 짧은 직선 local path 생성
> 

```bash
Input :
	- /navigation/path_progress/spot_01

Output :
	- /navigation/local_path/spot_01
		Type: nav_msgs/msg/Path
	- /navigation/local_planner_status/spot_01
		Type: robot_interfaces/msg/LocalPlannerStatus
```

기능 :

- PathProgress 유효성 확인
- goal_reached 확인
- 현재 pose 또는 nearest point에서 target point까지 local path 생성
- local path valid 상태 publish

---

**[1차 MVP 설계]**

MVP에서는 perception 없이 아래만 사용한다.

```
PathProgress + Current Pose
```

**핵심 흐름 :**

```bash
PathProgress 수신
Current Pose 수신
        ↓
timer 주기마다 publishLocalPlanning()
        ↓
입력 유효성 검사
        ↓
상태 결정
        ↓
상태별 local path 생성
        ↓
local_path + LocalPlannerStatus publish
```

- **1차 MVP 상태 전이 :**
    
    ```bash
                        ┌────────────────────────────────┐
                        │             IDLE               │
                        │ 노드 초기 상태                  │
                        │ planner_state = IDLE            │
                        │ 생성 path: 없음                 │
                        │ 실행 함수: 생성자 초기화         │
                        └───────────────┬────────────────┘
                                        │
                                        │ timer 시작
                                        │ publishLocalPlanning()
                                        ▼
                     ┌──────────────────────────────────────┐
                     │          입력 유효성 검사             │
                     │ isPathProgressValid(now)              │
                     │ isPoseValid(now)                      │
                     │                                      │
                     │ 검사 대상:                            │
                     │ - PathProgress 수신 여부              │
                     │ - Pose 수신 여부                      │
                     │ - robot_id 일치                       │
                     │ - frame_id == mission_map             │
                     │ - PathProgress.path_valid             │
                     │ - PathProgress.pose_valid             │
                     │ - finite 값 여부                      │
                     │ - timeout 여부                        │
                     └───────────────┬──────────────────────┘
                                     │
              ┌──────────────────────┼─────────────────────────────┐
              │                      │                             │
              │ 조건 A               │ 조건 B                      │ 조건 C
              │ 입력 invalid         │ 입력 valid                  │ 입력 valid
              │                      │ goal_reached == false       │ goal_reached == true
              ▼                      ▼                             ▼
    ┌────────────────────────┐ ┌──────────────────────────┐ ┌────────────────────────────┐
    │     INVALID_INPUT      │ │    GLOBAL_SUB_GOAL       │ │   GLOBAL_GOAL_REACHED      │
    │ 입력 불가 / timeout    │ │ 정상 local path 생성     │ │ 최종 목적지 도착            │
    │ planner_state = 5      │ │ planner_state = 1        │ │ planner_state = 6           │
    │                        │ │                          │ │                            │
    │ 실행 함수:             │ │ 실행 함수:               │ │ 실행 함수:                  │
    │ publishInvalidInput()  │ │ publishGlobalSubGoalPath()│ │ publishGlobalGoalReached() │
    │                        │ │                          │ │                            │
    │ 생성 path:             │ │ 생성 path:               │ │ 생성 path:                  │
    │ - pose valid면         │ │ - current pose           │ │ - current pose              │
    │   hold local path      │ │   → PathProgress target  │ │   → current pose            │
    │ - pose invalid면       │ │ - makeLocalPathToTarget()│ │ - makeHoldLocalPath()       │
    │   empty path           │ │                          │ │                            │
    │                        │ │ 목적:                    │ │ 목적:                       │
    │ stop_required = true   │ │ follower가 따라갈        │ │ follower/controller 정지    │
    │ local_path_available   │ │ 짧은 주행용 local path   │ │ 현재 위치 유지              │
    │ = pose valid 여부      │ │ 생성                     │ │                            │
    └───────────┬────────────┘ └───────────┬──────────────┘ └─────────────┬──────────────┘
                │                          │                              │
                │ 다음 timer cycle         │ 다음 timer cycle             │ 다음 timer cycle
                │ 입력 회복 시             │ 계속 goal 미도착이면         │ goal 도착 유지 시
                │ GLOBAL_SUB_GOAL 가능     │ GLOBAL_SUB_GOAL 유지         │ GLOBAL_GOAL_REACHED 유지
                │                          │                              │
                └──────────────────────────┴──────────────────────────────┘
                                           │
                                           ▼
                               ┌──────────────────────┐
                               │   다음 timer cycle    │
                               │ publishLocalPlanning()│
                               └──────────────────────┘
    ```
    
    1차 MVP 상태 결정 우선 순위 :
    
    ```bash
    if (!path_progress_valid || !pose_valid) {
      state = INVALID_INPUT;
    } else if (progress.goal_reached) {
      state = GLOBAL_GOAL_REACHED;
    } else {
      state = GLOBAL_SUB_GOAL;
    }
    ```
    
    - 이 구조가 맞는 이유는 1차 MVP에서는 Perception을 아직 보지 않기 때문이야. 즉, 이전 상태가 `GLOBAL_SUB_GOAL`이었는지, `AVOIDANCE`였는지를 기억해서 복잡하게 전이할 필요가 아직 없어. 현재 코드도 `PathProgress`와 `Pose` 유효성을 먼저 검사하고, 그다음 `progress.goal_reached` 여부에 따라 goal 도착 상태를 처리한 뒤, 나머지는 `GLOBAL_SUB_GOAL`로 보내는 구조
    - **상태별 상세 정리**
        
        ## 3-1. `IDLE`
        
        ```
        의미:
          노드가 켜진 직후의 초기 상태
        
        진입 조건:
          LocalPathPlannerNode 생성자에서 planner_state_ = IDLE로 초기화
        
        실행 함수:
          생성자
          loadParameters()
          subscriber / publisher / timer 생성
        
        생성 path:
          없음
        
        출력 상태:
          timer가 돌기 전까지 별도 status publish 없음
        
        다음 전이:
          timer 시작
          → publishLocalPlanning()
          → 입력 검사 결과에 따라 INVALID_INPUT / GLOBAL_SUB_GOAL / GLOBAL_GOAL_REACHED
        ```
        
        `IDLE`은 오래 유지되는 주행 상태라기보다 **노드 초기값**에 가까워. timer가 한 번 돌면 바로 입력 상태를 보고 다른 상태로 넘어간다.
        
        ---
        
        ## 3-2. `INVALID_INPUT`
        
        ```
        의미:
          local path를 만들기 위한 입력이 유효하지 않은 상태
        
        진입 조건:
          isPathProgressValid(now) == false
          또는
          isPoseValid(now) == false
        
        실행 함수:
          publishInvalidInput(stamp, path_progress_valid, pose_valid, reason)
        
        생성 path:
          1) pose_valid == true
             → makeHoldLocalPath()
             → 현재 pose 2개로 구성된 hold path 생성
        
          2) pose_valid == false
             → makeEmptyPath()
             → poses가 비어 있는 empty path 생성
        
        주요 status:
          planner_state = INVALID_INPUT
          stop_required = true
          blocked = false
          local_path_available = pose_valid이면 true, 아니면 false
          reason =
            - invalid path progress and pose
            - invalid path progress
            - invalid pose
        ```
        
        여기서 중요한 점은 `blocked=false`가 맞다는 거야. `INVALID_INPUT`은 장애물 때문에 막힌 게 아니라, 입력 데이터가 없거나 유효하지 않은 상태이기 때문이야.
        
        입력 유효성 검사에서는 `PathProgress` 수신 여부, `robot_id`, `frame_id`, `path_received`, `pose_received`, `path_valid`, `pose_valid`, target 관련 값의 finite 여부, timeout 등을 확인한다.
        
        ---
        
        ## 3-3. `GLOBAL_SUB_GOAL`
        
        ```
        의미:
          1차 MVP의 정상 주행 상태.
          PathProgress가 제공한 lookahead target을 local target으로 사용한다.
        
        진입 조건:
          path_progress_valid == true
          pose_valid == true
          progress.goal_reached == false
        
        실행 함수:
          publishGlobalSubGoalPath(stamp, pose, progress)
        
        내부 path 생성 함수:
          makeLocalPathToTarget(
            pose,
            progress.target_x_m,
            progress.target_y_m,
            local_target_yaw,
            progress.target_heading_rad,
            stamp,
            local_path_length_m
          )
        
        생성 path:
          current pose → PathProgress target 방향의 local path
        
        path 의미:
          전체 global path가 아니라,
          현재 cycle에서 follower/controller가 따라갈 짧은 path window
        
        주요 status:
          planner_state = GLOBAL_SUB_GOAL
          local_path_available = true
          stop_required = false
          blocked = false
          used_global_sub_goal = true
          used_free_space_heading = false
          used_rejoin_target = false
          reason = "follow global sub-goal"
        ```
        
        ## 3-4. `GLOBAL_GOAL_REACHED`
        
        ```
        의미:
          global path의 마지막 waypoint, 즉 최종 목적지에 도착한 상태
        
        진입 조건:
          path_progress_valid == true
          pose_valid == true
          progress.goal_reached == true
        
        실행 함수:
          publishGlobalGoalReached(stamp, pose, progress)
        
        내부 path 생성 함수:
          makeHoldLocalPath(pose, stamp)
        
        생성 path:
          current pose → current pose
        
        구성:
          poses[0] = 현재 pose
          poses[1] = 현재 pose
        
        path 의미:
          이동용 path가 아니라 정지/유지용 hold path
        
        주요 status:
          planner_state = GLOBAL_GOAL_REACHED
          local_path_available = true
          stop_required = true
          blocked = false
          local_target_reached = true
          local_path_length_m = 0.0
          local_path_pose_count = 2
          used_global_sub_goal = false
          used_free_space_heading = false
          used_rejoin_target = false
          reason = "global goal reached, hold current pose"
        ```
        
        이 상태에서는 더 이상 진행용 local path를 만들지 않는다. 대신 follower/controller 안정성을 위해 현재 pose 2개로 구성된 hold path를 publish하고, `stop_required=true`로 명확하게 정지 상태를 알린다. goal 도착 상태의 status 값도 `local_path_length_m=0.0`, `local_path_pose_count=2`, `used_global_sub_goal=false`로 채워진다.
        
        상태 전이 표:
        
        | 현재 상태 | 전이 조건 | 다음 상태 | 실행 함수 | 생성 path | 핵심 status |
        | --- | --- | --- | --- | --- | --- |
        | `IDLE` | timer 시작 | 입력 검사 단계 | `publishLocalPlanning()` | 없음 | 아직 publish 없음 |
        | 입력 검사 | `!path_progress_valid |  | !pose_valid` | `INVALID_INPUT` | `publishInvalidInput()` |
        | 입력 검사 | 입력 valid && `goal_reached=false` | `GLOBAL_SUB_GOAL` | `publishGlobalSubGoalPath()` | `makeLocalPathToTarget()` | `local_path_available=true`, `stop_required=false`, `used_global_sub_goal=true` |
        | 입력 검사 | 입력 valid && `goal_reached=true` | `GLOBAL_GOAL_REACHED` | `publishGlobalGoalReached()` | `makeHoldLocalPath()` | `stop_required=true`, `local_path_length_m=0.0`, `local_path_pose_count=2` |
        | `INVALID_INPUT` | 다음 cycle에도 입력 invalid | `INVALID_INPUT` 유지 | `publishInvalidInput()` | hold 또는 empty | 안전 정지 유지 |
        | `INVALID_INPUT` | 입력 회복 && `goal_reached=false` | `GLOBAL_SUB_GOAL` | `publishGlobalSubGoalPath()` | target 방향 local path | 정상 주행 복귀 |
        | `GLOBAL_SUB_GOAL` | 입력 valid && `goal_reached=false` | `GLOBAL_SUB_GOAL` 유지 | `publishGlobalSubGoalPath()` | target 방향 local path 갱신 | 정상 추종 |
        | `GLOBAL_SUB_GOAL` | `goal_reached=true` | `GLOBAL_GOAL_REACHED` | `publishGlobalGoalReached()` | hold path | 최종 정지 |
        | `GLOBAL_GOAL_REACHED` | goal 도착 유지 | `GLOBAL_GOAL_REACHED` 유지 | `publishGlobalGoalReached()` | hold path | 정지 유지 |
        | `GLOBAL_GOAL_REACHED` | 새 mission/path 수신 후 `goal_reached=false` | `GLOBAL_SUB_GOAL` | `publishGlobalSubGoalPath()` | 새 target 기준 local path | 새 임무 추종 |

| 현재 상태 | 조건 | 다음 상태 | local path 출력 | status 핵심값 |
| --- | --- | --- | --- | --- |
| `IDLE` | 아직 입력 없음 | `INVALID_INPUT` | pose 없으면 빈 path | `stop_required=true` |
| `INVALID_INPUT` | PathProgress 또는 pose invalid | `INVALID_INPUT` | pose 유효하면 hold path, pose도 없으면 빈 path | `local_path_available=false 또는 hold 가능 시 true`, `stop_required=true` |
| `INVALID_INPUT` | PathProgress valid && pose valid && goal 미도착 | `GLOBAL_SUB_GOAL` | 현재 pose → target 직선 local path | `stop_required=false` |
| `GLOBAL_SUB_GOAL` | 입력 유효 && goal 미도착 | `GLOBAL_SUB_GOAL` | 현재 pose → PathProgress target | `used_global_sub_goal=true` |
| `GLOBAL_SUB_GOAL` | `PathProgress.goal_reached == true` | `GLOBAL_GOAL_REACHED` | hold path | `stop_required=true` |
| `GLOBAL_GOAL_REACHED` | goal 도착 유지 | `GLOBAL_GOAL_REACHED` | hold path | `local_target_reached=true` |
| `GLOBAL_GOAL_REACHED` | 새 global path 수신 후 goal 미도착 | `GLOBAL_SUB_GOAL` | 새 target 기준 local path | `used_global_sub_goal=true` |
| 어느 상태든 | PathProgress/pose timeout, frame mismatch, robot_id mismatch | `INVALID_INPUT` | hold path 또는 빈 path | `stop_required=true` |

[State별 생성 Path 유형]

- `GLOBAL_SUB_GOAL` : global path를 따라가기 위한 일반 주행용 local path
- `GLOBAL_GOAL_REACHED` : 움직이기 위한 path가 아니라 정지/유지용 path
- `INVALID_INPUT` : 안전 정지를 위한 path (empty path)
- `AVOIDANCE` : 장애물 회피용 local path(현재 pose → 회피 target)
- `REJOIN` : 원래 경로로 복귀하기 위한 local path (현재 pose → global path 근처의 복귀 target)
- `BLOCKED` : 정지용 path (hold path)

| planner_state | local path 의미 |
| --- | --- |
| `GLOBAL_SUB_GOAL` | 일반 추종 path |
| `AVOIDANCE` | 회피 path |
| `REJOIN` | 복귀 path |
| `GLOBAL_GOAL_REACHED` | 정지/유지 path |
| `BLOCKED` | 정지/유지 path |
| `INVALID_INPUT` | 안전 정지 path 또는 empty |

즉, **출력 타입은 같지만 path의 의미와 생성 방식이 상태에 따라 달라진다**

---

**개발 순서 :**

```bash
1. LocalPlannerStatus.msg 수정 반영
2. robot_interfaces 빌드
3. local_path_planner.param.yaml 작성
4. local_path_planner_node.hpp 작성
5. local_path_planner_node.cpp 작성
6. CMakeLists.txt / package.xml 수정
7. launch 파일에 local_path_planner_node 추가
8. RViz에서 /navigation/local_path/spot_01 표시
```

#### Step 1. 입력 수신 상태 저장

노드는 내부에 최신 메시지를 저장한다.

```
latest_path_progress_
latest_pose_

path_progress_received_
pose_received_

last_path_progress_receive_time_
last_pose_receive_time_
```

각 callback에서는 메시지를 저장하고 수신 시간을 갱신한다.

#### Step 2. timer 주기마다 planning 수행

`publish_rate_hz` 주기로 아래 순서를 실행한다.

```
1. 기본 status 메시지 생성
2. 입력 유효성 검사
3. global goal reached 처리
4. local target 결정
5. local path 생성
6. status 채우기
7. local path + status publish
```

#### Step 3. 입력 유효성 검사

### PathProgress 유효 조건

```
path_progress_received == true
latest_path_progress != nullptr
latest_path_progress.robot_id == robot_id
latest_path_progress.header.frame_id == global_frame
latest_path_progress.path_received == true
latest_path_progress.pose_received == true
latest_path_progress.path_valid == true
latest_path_progress.pose_valid == true
latest_path_progress.target_x_m/y_m/yaw_rad finite
receive_age <= path_progress_timeout_sec
```

실패 시:

```
planner_state = INVALID_INPUT
local_path_available = false
stop_required = true
blocked = false
reason = "invalid path progress"
```

이때 빈 `nav_msgs/Path`를 publish한다.

---

### Pose 유효 조건

```
pose_received == true
latest_pose != nullptr
latest_pose.robot_id == robot_id
latest_pose.header.frame_id == global_frame
x_m, y_m, yaw_rad finite
receive_age <= pose_timeout_sec
```

실패 시:

```
planner_state = INVALID_INPUT
local_path_available = false
stop_required = true
blocked = false
reason = "invalid pose"
```

#### Step 4. global goal reached 처리

`PathProgress.goal_reached == true`이면 local planner는 더 이상 local path를 만들 필요가 없다.

```
planner_state = GLOBAL_GOAL_REACHED
local_path_available = false
stop_required = true
blocked = false
local_path_pose_count = 0
reason = "global goal reached"
```

이때 `/navigation/local_path/spot_01`은 빈 path로 publish하거나 현재 pose 하나만 담아도 된다.

나는 MVP에서는 **빈 path publish**를 추천해.

후속 follower가 `local_path_available=false`와 `stop_required=true`를 보고 정지하도록 만드는 게 더 명확하다.

#### Step 5. Local target 결정

MVP에서는 장애물 회피가 없으므로 local target은 PathProgress target과 동일하다.

```
local_target_x = PathProgress.target_x_m
local_target_y = PathProgress.target_y_m
```

yaw는 두 가지 선택지가 있다.

### 선택 A: PathProgress target yaw 사용

```
local_target_yaw = PathProgress.target_yaw_rad
```

장점:

```
global path waypoint가 가진 자세 방향을 유지한다.
```

### 선택 B: selected heading 사용

```
local_target_yaw = selected_heading_rad
```

장점:

```
현재 pose에서 target을 바라보는 방향과 local path 방향이 일치한다.
```

MVP에서는 `use_path_progress_target_yaw=true`로 두고, 기본은 `PathProgress.target_yaw_rad`를 쓰면 된다.

selected heading은:

```
selected_heading_rad = atan2(local_target_y - current_y, local_target_x - current_x)
```

또는 PathProgress가 이미 계산한:

```
selected_heading_rad = PathProgress.target_heading_rad
```

를 써도 된다. MVP에서는 PathProgress의 `target_heading_rad`를 그대로 쓰는 게 일관성이 좋다.

---

#### Step 6. Local path 생성

현재 pose와 local target 사이 거리를 계산한다.

```
distance = hypot(local_target_x - current_x, local_target_y - current_y)
```

local path 길이는 최대 길이로 제한한다.

```
path_length = min(distance, max_local_path_length_m)
```

여기서 중요한 점은, target이 2m 앞에 있더라도 local path를 매번 2m 전체로 만들 필요는 없다는 거야.

local path는 순간적인 짧은 path니까 `max_local_path_length_m`까지만 생성한다.

방향 벡터:

```
dx = cos(selected_heading_rad)
dy = sin(selected_heading_rad)
```

끝점:

```
end_x = current_x + path_length * dx
end_y = current_y + path_length * dy
```

단, target이 max length보다 가까우면:

```
end_x = local_target_x
end_y = local_target_y
```

pose 개수:

```
segment_count = ceil(path_length / local_path_spacing_m)
pose_count = segment_count + 1
```

각 pose:

```
ratio = i / segment_count

x = current_x + ratio * (end_x - current_x)
y = current_y + ratio * (end_y - current_y)
z = current_z
yaw = selected_heading_rad
```

마지막 pose의 yaw는 `local_target_yaw_rad`로 둘 수도 있다.

곡선형 local path를 만들고 싶다면 local planner가 추가로 아래 중 하나를 받아야 해.

```
1. GlobalPathWaypoints 전체
2. PathProgress 기준 local window waypoints
3. PathProgress.msg 안에 target 주변 waypoint 배열 추가
```

지금 1차 MVP에서는 이걸 안 하기로 했으니까, 직선 보간 local path가 가장 적절해.

---

#### 결과

![image.png](image%2057.png)

- 하얀색 Arrows가 Local Path를 이루는 점들의 갯수 만큼 생성되고, 각 점들의 위치에서 나타내는 방향이다.

[1차 MVP에서 핵심 데이터 검증 명령어]

```bash
ros2 topic echo /navigation/local_planner_status/spot_01 | grep -E --line-buffered "planner_state|local_path_available|stop_required|blocked|source_target_index|distance_to_local_target_m|local_path_length_m|local_path_pose_count|used_global_sub_goal|reason|---"
```

[Local path 정상 추종 주행 로그]

![image.png](image%2058.png)

[global path 최종 목적지 근처에 도달하는 상황 로그]

![image.png](image%2059.png)

![image.png](image%2060.png)

### 3-3. Obstacle/FreeSpace 반영 Local Path - 2차

> LiDAR 기반 인지 환경 모델 데이터를 활용해 장애물까지 고려한 local path 생성
> 

```bash
Input :
	- /navigation/path_progress/spot_01
	- /navigation/path_progress/spot_01
	- /perception/lidar/obstacle_model
	- /perception/lidar/free_space_model

Output :
	- /navigation/local_path/spot_01
		Type: nav_msgs/msg/Path
	- /navigation/local_planner_status/spot_01
	Type: robot_interfaces/msg/LocalPlannerStatus
```

### 2차 MVP에서 활용할 데이터

```bash
1. ObstacleModel
   → “위험이 발생했는가?”를 판단하는 데이터

2. FreeSpaceModel
   → “회피 가능한 방향이 있는가?”를 판단하는 데이터
```

즉, `ObstacleModel`은 **상태 전이 트리거**, `FreeSpaceModel`은 **회피 방향/회피 가능성 판단 근거**로 쓰면 된다.

#### 좌표계 :

```bash
PathProgress / current pose / local path
= mission_map 기준

ObstacleModel / FreeSpaceModel
= base_link 기준
```

| 구분 | 데이터 | 기준 좌표계 | 2차 MVP 용도 |
| --- | --- | --- | --- |
| ObstacleModel | `front.valid` | base_link | 전방 장애물 존재 여부 |
| ObstacleModel | `front.nearest_distance_xy` | base_link | `front_danger` 판단 |
| ObstacleModel | `left/right.valid` | base_link | 좌우 보조 위험 판단 |
| ObstacleModel | `left/right.nearest_distance_xy` | base_link | side danger 보조 판단 |
| ObstacleModel | `front.azimuth_angle_rad` | base_link | 장애물 방향 디버그 |
| FreeSpaceModel | `path_available` | base_link | 회피 후보 gap 존재 여부 |
| FreeSpaceModel | `risk_level` | base_link | SAFE/CAUTION/BLOCKED 판단 |
| FreeSpaceModel | `best_heading_angle_rad` | base_link | 회피 heading 선택 |
| FreeSpaceModel | `best_clearance` | base_link | 회피 가능 최소 여유 거리 판단 |
| FreeSpaceModel | `best_score` | base_link | 회피 방향 품질 디버그 |
| FreeSpaceModel | `best_free/unknown/occupied_ratio` | base_link | 선택 gap 신뢰도 확인 |
| FreeSpaceModel | `candidate_gap_count` | base_link | gap 존재 디버그 |
| PathProgress | `distance_to_nearest_m` | mission_map | REJOIN 복귀 판정 |
| Pose | `x_m/y_m/yaw_rad` | mission_map | 회피 heading을 map 기준 target으로 변환 |

Obstacle Model :

> `ObstacleModel`은 front/left/right 섹터별 대표 장애물을 담는 요약 메시지
> 
> 
> `obstacle_model_node`는 `/perception/lidar/obstacle_clusters` 안의 클러스터 중에서 각 섹터에 해당하고 가장 가까운 클러스터를 대표 장애물로 선택한다.
> 

```bash
obstacle_model.header.frame_id

# front / left / right 중 하나라도 대표 장애물이 있으면 true
obstacle_model.obstacle_detected

# *.valid: 전방/좌/우 섹터에 대표 장애물이 존재하는가?
# *.nearest_distance_xy: base_link 기준 전방/좌/우 대표 장애물의 최근접 포인트까지의 수평 거리
# azimuth_angle_rad: 전방 대표 장애물이 base_link 기준 어느 방향에 있는지 나타내는 각도
# sector_mask: nearest point 기준 대표 섹터 1개
obstacle_model.front.valid
obstacle_model.front.nearest_distance_xy
obstacle_model.front.azimuth_angle_rad
obstacle_model.front.sector_mask

obstacle_model.left.valid
obstacle_model.left.nearest_distance_xy

obstacle_model.right.valid
obstacle_model.right.nearest_distance_xy
```

FreeSpaceModel :

> `FreeSpaceModel`은 회피 판단의 핵심
> 
> 
>  `free_space_model_node`는 local occupancy grid를 angular bin으로 나누고, bin별 free/unknown/occupied 통계와 clearance를 계산한 뒤, 연속된 candidate bin을 gap으로 묶고, 가장 안전한 gap을 selected gap으로 고른다. 
> 

```bash
free_space_model.header.frame_id

# path_available: 통과 가능한 candidate gap이 하나 이상 있는가?
# risk_level: 위험도
free_space_model.path_available
free_space_model.risk_level

# best_heading_angle_rad: base_link 기준 선택된 최적 gap의 중심 방향
free_space_model.best_heading_angle_rad
free_space_model.best_heading_angle_deg

# best_clearance: 선택된 회피 gap 안에서 확보된 최소 장애물 여유 거리
free_space_model.best_clearance

# best_score: 선택된 gap의 종합 점수
# = free ratio, unknown ratio, occupied ratio, clearance, heading bias
free_space_model.best_score

free_space_model.best_free_ratio
free_space_model.best_unknown_ratio
free_space_model.best_occupied_ratio

# candidate_gap_count: 통과 후보 gap 개수
free_space_model.candidate_gap_count
```

### 3-4. 2차 MVP 설계 과정

MVP 진행 :

```bash
1. local_path_planner_node가 ObstacleModel 구독
2. local_path_planner_node가 FreeSpaceModel 구독
3. front_danger 판단 추가
4. free_space_safe 판단 추가
5. AVOIDANCE 상태 추가
6. BLOCKED 상태 추가
7. REJOIN 상태 추가
8. LocalPlannerStatus에 free_space 관련 값 채우기
```

판단 흐름 :

```bash
1. PathProgress 또는 Pose invalid
   → INVALID_INPUT

2. goal_reached == true
   → GLOBAL_GOAL_REACHED

3. Perception invalid
   → require_perception_input == true이면 INVALID_INPUT
   → false이면 1차 MVP처럼 GLOBAL_SUB_GOAL

4. front_danger == false
   → 기존 상태가 AVOIDANCE였다면 REJOIN
   → 기존 상태가 REJOIN이고 복귀 완료면 GLOBAL_SUB_GOAL
   → 그 외 GLOBAL_SUB_GOAL

5. front_danger == true && free_space_safe == true
   → AVOIDANCE

6. front_danger == true && free_space_safe == false
   → BLOCKED
```

MVP 고도화 :

```bash
1. candidate_gaps 전체 배열을 직접 재탐색해서 gap 재선택
2. 곡선 path 생성
3. dynamic obstacle prediction
4. track_id 기반 장애물 추적 판단
5. path follower 속도 제어까지 수정
```

### 3-5. 2차 MVP 구현 과정

```bash
ObstacleModel:
  front.valid + front.nearest_distance_xy
  → front_danger 판단

FreeSpaceModel:
  path_available + risk_level + best_clearance + best_heading_angle_rad
  → free_space_safe 판단 및 회피 heading 생성
```

- `obstacle_model_node`는 sector별 대표 장애물을 가장 가까운 클러스터 기준으로 선택하고, `ObstacleModel.front/left/right`에 담는 구조
- `free_space_model_node`는 local occupancy grid에서 gap을 뽑고, 선택된 gap의 `best_heading_angle_rad`, `best_clearance`, `best_score`를 publish하는 구조

### 3-6. 결과

![image.png](image%2061.png)

![image.png](image%2062.png)

```yaml
    roi_min_x: 0.35  
    roi_max_x: 1.5    # 좌우 범위
    roi_min_y: -1.0   # 왼쪽 1m
    roi_max_y:  1.0   # 오른쪽 1m

    # 상하 범위
    roi_min_z: 0.05   # 지면 5cm 위 부터 인식
    roi_max_z: 1.0
```

자 lidar perception 패키지를 대표하는 Obstacle Model과 Free Space Model에 대해서 아래와 같이 정리해봤거든? 이 데이터들을 기반으로 2차 MVP인 Perception 기반 분기 조건 추가하는 작업인지 알려줘.

1. Obstacle Model은 lidar ROI 영역(roi_x: 0.35~1.5, roi_y: -1.0 ~ 1.0, roi_z: 0.05 ~ 1.0) 내에 잡히는 lidar point 기반으로 clustering화 시키고, 여러 클러스터들을 다시 base_link 기준으로 lidar의 수평 FOV 영역(-60도~+60도)을 Front: -30.0~+30.0, Left: +30.0~+60.0, Right: -30.0~-60.0)으로 세분화된 Front, Left, Right 영역에 속하는 cluster를 배치하고, sector_mask를 이용해서 각 클러스터들이 수평 FOV 기준으로 어디에 배치되어 있는지에 대한 정보들을 내보내는 거 같아. 즉, Obstacle Model은 lidar로 잡히는 모든 클러스터들에 대한 정보들을 ObstacleCluster.msg 구조를 기반으로 base_link 기준으로 잡힌 모든 클러스터에 대한 방향각, 거리, 그 클러스터가 lidar FOV 영역 내에서 어디 섹터에 배치되어 있는 지 등에 대한 정보들을 내보내고 있는 중인거지. 그래서 드는 생각이 2차 MVP 설계에서 Obstacle Model은 장애물 감지와 위험 트리거로 활용된다는 거에 동의는 해. 하지만 의문이 드는 점은 단지 front.valid, front.nearest_distance_xy 등으로 장애물 감지/위험 트리거로 쓴다고 했는데 여기서의 front는 sector를 Front, Left, Right로 나눈 이 sector 영역 중 Front를 의미하는 지 궁금해. 이게 맞다면 왜 Front Sector에 속한 cluster만을 상태 전이 조건을 사용하는 지도 궁금해. 차라리 위험 장애물 탐지 반경 threshold를 정해두고, Obstacle Model로 잡히는 모든 클러스터 중에 해당 반경 내에 잡히게 되는 순간에

---

Local Path Planner FSM

`GLOBAL_SUB_GOAL` 앞에 Perception 기반 판단을 추가하고, `AVOIDANCE → REJOIN → GLOBAL_SUB_GOAL` 복귀 흐름을 안정적으로 만드는 것

```bash
                    ┌────────────────────────┐
                    │          IDLE          │
                    │ 노드 초기 상태          │
                    └───────────┬────────────┘
                                │
                                │ timer 시작
                                ▼
                 ┌──────────────────────────────┐
                 │  1차 입력 유효성 검사          │
                 │  PathProgress + Pose         │
                 └───────────┬──────────────────┘
                             │
          ┌──────────────────┼─────────────────────┐
          │                  │                     │
          ▼                  ▼                     ▼
 ┌────────────────┐  ┌───────────────────┐  ┌──────────────────────┐
 │ INVALID_INPUT  │  │GLOBAL_GOAL_REACHED│  │  Perception 검사 단계 │
 │ 입력 불가       │  │ 최종 목적지 도착    │  │ Obstacle + FreeSpace │
 └───────┬────────┘  └─────────┬─────────┘  └──────────┬───────────┘
         │                     │                       │
         │                     │                       ▼
         │                     │          ┌────────────────────────┐
         │                     │          │   Perception 판단       │
         │                     │          │ front_danger/free_safe │
         │                     │          └──────────┬─────────────┘
         │                     │                     │
         │        ┌────────────┼──────────────┬──────┴──────────────┐
         │        │            │              │                     │
         ▼        ▼            ▼              ▼                     ▼
 ┌────────────┐ ┌────────────────┐ ┌──────────────┐        ┌──────────────┐
 │ 다음 cycle │ │ GLOBAL_SUB_GOAL│ │  AVOIDANCE   │        │   BLOCKED    │
 │ 재판단     │ │ 정상 path 추종  │ │ 회피 path     │        │ 정지/hold    │
 └────────────┘ └───────┬────────┘ └──────┬───────┘        └──────┬───────┘
                         │                 │                       │
                         │                 │ front_clear           │ free_safe 회복
                         │                 ▼                       ▼
                         │          ┌──────────────┐        ┌──────────────┐
                         └────────▶│    REJOIN    │◀──────│  AVOIDANCE   │
                                    │ global 복귀  │        │ 재진입 가능   │
                                    └──────┬───────┘        └──────────────┘
                                           │
                                           │ rejoin_done
                                           ▼
                                   ┌────────────────┐
                                   │ GLOBAL_SUB_GOAL│
                                   │ global 추종 복귀│
                                   └────────────────┘
```

- **판단 구조**
    
    ```bash
    ObstacleModel:
      front.valid
      front.nearest_distance_xy
      ↓
      front_danger / front_clear 판단
    
    FreeSpaceModel:
      path_available
      risk_level
      best_clearance
      best_heading_angle_rad
      ↓
      free_space_safe / avoidance heading 판단
    
    PathProgress:
      target_x_m
      target_y_m
      target_heading_rad
      distance_to_nearest_m
      heading_error_rad
      ↓
      GLOBAL_SUB_GOAL path 생성
      REJOIN path 생성
      rejoin_done 판단
    ```
    
- **상태 전이표**
    
    ```bash
    GLOBAL_SUB_GOAL
    = global path target 추종
    
    AVOIDANCE
    = free-space best heading 방향 회피
    
    REJOIN
    = 회피 후 global path 복귀
    
    BLOCKED
    = 회피 불가, 현재 위치 hold
    
    INVALID_INPUT
    = 입력 불량, 안전 정지
    
    GLOBAL_GOAL_REACHED
    = 최종 목적지 도착, 현재 위치 hold
    
    IDLE
    = 노드 초기 상태
    ```
    
    | 현재 상태 | 조건 | 다음 상태 | path 생성 함수 | 생성 path 의미 |
    | --- | --- | --- | --- | --- |
    | `IDLE` | timer 시작 | 입력 검사 | `publishLocalPlanning()` | 아직 path 없음 |
    | Any | `!path_progress_valid |  | !pose_valid` | `INVALID_INPUT` |
    | Any | perception required인데 perception invalid | `INVALID_INPUT` | `publishInvalidInput()` | 안전 정지 |
    | Any | `goal_reached == true` | `GLOBAL_GOAL_REACHED` | `publishGlobalGoalReached()` | hold path |
    | `GLOBAL_SUB_GOAL` | `front_danger == false` | `GLOBAL_SUB_GOAL` | `publishGlobalSubGoalPath()` | PathProgress target 추종 |
    | `GLOBAL_SUB_GOAL` | `front_danger && free_space_safe` | `AVOIDANCE` | `publishAvoidancePath()` | best heading 회피 |
    | `GLOBAL_SUB_GOAL` | `front_danger && !free_space_safe` | `BLOCKED` | `publishBlocked()` | 현재 위치 hold |
    | `AVOIDANCE` | `front_danger && free_space_safe` | `AVOIDANCE` | `publishAvoidancePath()` | 회피 유지 |
    | `AVOIDANCE` | `front_danger && !free_space_safe` | `BLOCKED` | `publishBlocked()` | 회피 불가 정지 |
    | `AVOIDANCE` | `front_clear` | `REJOIN` | `publishRejoinPath()` | global path 복귀 |
    | `REJOIN` | `front_danger && free_space_safe` | `AVOIDANCE` | `publishAvoidancePath()` | 장애물 재출현 회피 |
    | `REJOIN` | `front_danger && !free_space_safe` | `BLOCKED` | `publishBlocked()` | 정지 |
    | `REJOIN` | `!front_danger && !rejoin_done` | `REJOIN` | `publishRejoinPath()` | 복귀 계속 |
    | `REJOIN` | `!front_danger && rejoin_done` | `GLOBAL_SUB_GOAL` | `publishGlobalSubGoalPath()` | 정상 추종 복귀 |
    | `BLOCKED` | `front_danger && !free_space_safe` | `BLOCKED` | `publishBlocked()` | 정지 유지 |
    | `BLOCKED` | `front_danger && free_space_safe` | `AVOIDANCE` | `publishAvoidancePath()` | 회피 재개 |
    | `BLOCKED` | `!front_danger` | `REJOIN` | `publishRejoinPath()` | path 복귀 |
- **상태별 local path 생성 전략**
    
    
    | 상태 | local target | selected heading | 함수 |
    | --- | --- | --- | --- |
    | `GLOBAL_SUB_GOAL` | `progress.target_x_m`, `progress.target_y_m` | `progress.target_heading_rad` | 기존 `publishGlobalSubGoalPath()` |
    | `AVOIDANCE` | 현재 pose + `avoidance_horizon_m` 방향 target | `pose.yaw_rad + free_space.best_heading_angle_rad` | 신규 `publishAvoidancePath()` |
    | `REJOIN` | `progress.target_x_m`, `progress.target_y_m` | `progress.target_heading_rad` | 신규 `publishRejoinPath()` |
    | `BLOCKED` | 현재 pose | 현재 yaw | 신규 `publishBlocked()` |
    | `INVALID_INPUT` | 현재 pose 또는 없음 | 현재 yaw 또는 0 | 기존 `publishInvalidInput()` |
    | `GLOBAL_GOAL_REACHED` | 현재 pose | 현재 yaw | 기존 `publishGlobalGoalReached()` |

Planner State 정리

```bash
uint8 IDLE=0                    # 초기 상태
uint8 GLOBAL_SUB_GOAL=1         # PathProgress의 lookahead target을 local target으로 사용해 local path를 생성한 상태
uint8 AVOIDANCE=2               # free space heading 기반 local path 생성 상태
uint8 REJOIN=3                  # 회피 후 global path 근처로 복귀하기 위한 local path를 생성한 상태
uint8 BLOCKED=4                 # 장애물 존재 + free space gap X 기반 local path 생성 실패한 상태
uint8 INVALID_INPUT=5           # planning 수행할 수 없는 상태 (ex: pose 없음, path_progress 없음, frame_id 불일치 등)
uint8 GLOBAL_GOAL_REACHED=6     # global path의 최종 goal 도착, 정지 필요
```

- Q&A
    - Q1. Obstacle Model에서 추출한 데이터를 바탕으로 상태 전이를 위한 판단 변수가 front_danger과 front_clear 같아. 근데 front_danger는 obstacle_model.front로 뽑히는데 이때 뽑히는 것은 아마 ObstacleCluster.msg 메시지 구조에 맞춰진 하나의 대표 클러스터를 의미하는 게 맞나? 뭔가 변수명이 front다 보니깐 lidar의 수평 FOV에 서 Front: -30.0~+30.0, Left: +30.0~+60.0, Right: -30.0~-60.0)으로 세분화한 sector들이 있는데, 이 중에서 Front Sector에만 해당하는 클러스터만 보는 건지 궁금해. 아니면 Obstacle Model 자체가 lidar에 의해 잡힌 point들을 여러 클러스터들로 만든 후에 만들어진 모든 클러스터를 내보내는게 아니라 그 중에 base_link와 가장 가까운 대표 cluster 1개를 내보내는 중인건가?
        - A1.  Obstacle Model Node는 Front/Left/Right Sector 별 대표 Cluster 1개씩 고른다. 이후 Local Planner은 대표 Cluster 선택을 아래의 기준에 맞춰서 선택함.
            
            ```bash
            cluster.valid == true
            (cluster.sector_mask & SECTOR_FRONT) != 0
            nearest_distance_xy가 가장 작음
            ```
            
            따라서 `front_danger`는 **전체 LiDAR FOV 중 가장 가까운 장애물**을 보는 게 아니라, **Front sector를 점유하는 대표 장애물의 거리**를 기준으로 판단하는 값
            
    
    ---
    
    - Q2. 그리고 front_block_distance_m이라는 값은 뭔가 Spot의 base_link 기준으로 60cm 이내에 잡히는 클러스터가 있다면 장애물로 인식하기 위한 threshold 값인 거 같은데 맞나?
        - A2. `front_block_distance_m = 0.60`은 base_link 기준 전방 대표 cluster의 `nearest_distacne_xy` 가 60cm 보다 가까울때 전방 위험으로 판단하는 threshold이다. 즉, **장애물로 추출된 front 대표 cluster를 local planner가 위험으로 볼지 판단하는 threshold**
            
            ```bash
            front_danger =
              obstacle_model.front.valid &&
              obstacle_model.front.nearest_distance_xy < front_block_distance_m_;
            ```
            
            ```bash
            front.valid == true
            → Front sector에 대표 장애물이 있다.
            
            front.nearest_distance_xy < 0.60
            → 그 장애물이 base_link 기준 60cm 이내라서 주행 지속이 위험하다.
            
            따라서 front_danger = true
            ```
            
            ```bash
            front.valid: true
            front.nearest_distance_xy: 1.20
            front_block_distance_m: 0.60
            ```
            
    
    ---
    
    - Q3. 그리고 AVOIDANCE에서 ERJOIN으로 전이하기 위해서 front_clear_distance_m라는 thresohold 값이 있는 거 같은데, 이 값은 결국 자율주행 중에 장애물을 회피하기 위해 AVOIDANCE 상태로 전이되고, 이 장애물을 회피하기 위한 방향으로 local path가 생성되서 생성된 회피 local path에 따라 움직이면서 lidar를 통해 잡히는 대표 cluster 중에 front_clear_distance_m의 0.8값 즉, 80cm 안에 잡히는 클러스터가 없다면 회피 성공으로 간주하고 ERJOIN 상태인 기존 global path로 복귀하기 위한 local path를 생성한다는 소리인가?
        - A3. `front_clear_distance_m = 0.80`이라고 하면, **AVOIDANCE 상태에서 전방 대표 장애물이 더 이상 80cm 안에 없다고 판단되면 회피가 어느 정도 끝났다고 보고 REJOIN으로 넘어가기 위한 기준**
            
            상태 전이 기준 :
            
            ```bash
            GLOBAL_SUB_GOAL → AVOIDANCE
            조건:
              front.nearest_distance_xy < 0.60
            
            AVOIDANCE → REJOIN
            조건:
              front invalid
              또는 front.nearest_distance_xy > 0.80
            ```
            
            여기서 `0.60`과 `0.80`을 분리하는 이유는 **hysteresis** 때문이야. 만약 진입과 해제 기준을 둘 다 0.60으로 두면, 거리값이 0.59 ↔ 0.61처럼 조금만 흔들려도 상태가 `GLOBAL_SUB_GOAL ↔ AVOIDANCE`로 계속 튈 수 있다.
            
            설계 의미 :
            
            ```bash
            front_block_distance_m = 0.60
            → 이보다 가까우면 회피 시작
            
            front_clear_distance_m = 0.80
            → 이보다 충분히 멀어져야 회피 종료
            ```
            
            그리고 `AVOIDANCE`에서 생성된 회피 local path를 따라 움직이다가, 매 cycle마다 LiDAR perception이 갱신되고, Front sector 위험이 해소되면 `REJOIN`으로 넘어가서 기존 global path 복귀용 local path를 만든다.
            
    
    ---
    
    - Q4. Free Space Model 또한 주행 가능한 gap 중에서 가장 좋은 gap 하나만을 publish하는 건가? 그래서 가장 좋은 gap의 정보를 FreeSpaceModel.msg에 맞춰서 publish 되는 정보들 중에 risk_level과 best_clearance를 참고해서 free_space_safe라는 판단 변수를 만드는 거 같은데, 해당 free_space_safe의 역할이 정확히 무엇인지, 이때 새로 나오는 avoidance_min_clearance_m라는 threshold 값도 무엇을 위한 것인지도 궁금해.
        - A4. `free_space_model_node`는 candidate gap들을 여러 개 만들고, 그중 score가 가장 높은 gap을 selected gap으로 고른다. 코드에서는 연속된 candidate bin을 `FreeSpaceGap`으로 묶고, gap score가 가장 높은 것을 best gap으로 선택한다.
            
            ```bash
            FreeSpaceModel:
              “이 공간이 후보 gap인가?”
            
            Local Planner:
              “이 후보 gap을 실제로 회피 경로로 사용할 것인가?”
              
            min_clearance_block_threshold
            = FreeSpaceModel 내부에서 bin/gap을 blocked로 분류하는 perception 기준
            
            selected_gap.min_clearance
            = 선택된 gap 안에서 가장 가까운 occupied cell까지의 최소 거리
            
            FreeSpaceModel.risk_level == RISK_BLOCKED
            = perception 관점에서 통과 후보가 없거나, selected gap의 clearance가 부족한 상태
            
            avoidance_min_clearance_m
            = Local Planner가 실제 AVOIDANCE path를 발행하기 위해 요구하는 추가 안전 여유
            
            따라서 둘은 비슷하지만,
            하나는 환경 해석 기준이고,
            다른 하나는 실제 행동 실행 기준이다.
            ```
            
            출력 메시지 관점 :
            
            ```bash
            candidate_gaps
            = 주행 가능 후보 gap 목록
            
            selected_gap
            = 그중 선택된 최적 gap
            
            best_heading_angle_rad
            = selected_gap의 중심 방향
            
            best_clearance
            = selected_gap의 최소 장애물 여유 거리
            
            best_score
            = selected_gap의 점수
            ```
            
            따라서 FreeSpaceModel이 “좋은 gap 하나만 publish한다”기보다는, **후보 gap 목록도 담고, local planner가 바로 쓰기 좋도록 selected/best 정보를 별도로 요약해서 publish한다**고 보는 게 정확해.
            
            `free_space_safe`: selected gap을 실제 회피 path 생성에 써도 되는지 판단하는 **local planner 쪽 boolean**
            
            ```bash
            free_space_safe =
              free_space_model.path_available &&
              free_space_model.risk_level != FreeSpaceModelMsg::RISK_BLOCKED &&
              free_space_model.best_clearance >= avoidance_min_clearance_m_;
            
            ```
            
            역할: 
            
            ```
            front_danger가 발생했을 때,
            정지해야 하는지,
            아니면 FreeSpaceModel의 best heading 방향으로 회피해도 되는지를 판단한다.
            ```
            
            상태 전이로 보면:
            
            ```
            front_danger == true && free_space_safe == true
            → AVOIDANCE
            
            front_danger == true && free_space_safe == false
            → BLOCKED
            ```
            
            `avoidance_min_clearance_m`는 **local planner가 요구하는 최소 회피 여유 거리**야. `best_clearance`가 이 값보다 작으면 gap이 있더라도 로봇이 지나가기에는 위험하다고 보고 `BLOCKED`로 보내기 위한 기준
            
    
    ---
    
    - Q5. 마찬가지로 rejoin_done에서 나오는 rejoin_tolerance_m와 rejoin_heading_tolerance_rad 값들은 장애물을 회피하고나서 global path로 복귀하기 위한 상태에서의 PathProgress에서 제공해주는 회피하고나서의 현재 Spot의 Pose에서 가장 가까운 global path에 속한 target point 중에서 rejoin_tolerance_m과 rejoin_heading_tolerance_rad 범위 안에 있는 target point를 잡아서 그 target point로 가기 위한 복귀 local path를 만든다는 소리인가?
        - A5.
        
        `rejoin_tolerance_m`와 `rejoin_heading_tolerance_rad`는 **복귀 target point를 새로 고르는 값이 아니라, REJOIN 상태를 끝내고 GLOBAL_SUB_GOAL로 복귀해도 되는지 판단하는 기준**이야.
        
        PathProgressTracker는 이미 현재 pose 기준으로:
        
        ```
        nearest_index
        target_index
        distance_to_nearest_m
        target_x_m
        target_y_m
        target_heading_rad
        heading_error_rad
        ```
        
        를 계산해서 publish한다. PathProgressTracker의 역할 자체가 현재 pose와 global path를 비교해서 nearest index, lookahead target index, heading error, goal reached 등을 계산하는 것이다.
        
        따라서 REJOIN에서 target을 새로 찾는 게 아니라, 2차 MVP에서는 우선 아래처럼 가면 된다.
        
        ```
        REJOIN path 생성:
          current pose → PathProgress target_x_m / target_y_m
        
        REJOIN 완료 판단:
          distance_to_nearest_m < rejoin_tolerance_m
          또는 heading_error_rad가 충분히 작음
        ```
        
        예상 판단은:
        
        ```
        rejoin_done =
        progress.distance_to_nearest_m<rejoin_tolerance_m_||
          std::fabs(progress.heading_error_rad)<rejoin_heading_tolerance_rad_;
        ```
        
        다만 실제 안정성 관점에서는 `OR`보다 `AND`가 더 보수적일 수 있어.
        
        ```
        rejoin_done =
        progress.distance_to_nearest_m<rejoin_tolerance_m_&&
          std::fabs(progress.heading_error_rad)<rejoin_heading_tolerance_rad_;
        ```
        
        2차 MVP에서는 처음에 `OR`로 쉽게 복귀시키고, 상태가 너무 빨리 `GLOBAL_SUB_GOAL`로 돌아가면 `AND`로 바꾸는 식으로 튜닝하면 된다.
        
        정리하면:
        
        ```
        rejoin_tolerance_m
        = 현재 로봇이 global path 근처로 돌아왔는지 보는 거리 기준
        
        rejoin_heading_tolerance_rad
        = global path target 방향과 현재 yaw 차이가 충분히 작아졌는지 보는 각도 기준
        
        둘 다 target point 선택용이 아니라,
        REJOIN 종료 조건 판단용이다.
        ```
        
    
    ---
    
    - Q6. 그리고 내가 궁금한게 현재 Planner State에서 사용되는 상태들은 총 7개로, 그 중에 GLOBAL_SUB_GOAL이라는 상태가 있는데, 이 상태는 lidar perception이 들어오면서 결국 현재 Planner 가 생성한 local path에 따라 주행 중인 상태로 느껴지거든? 맞나?
        - A6. 대체로 맞아. 다만 더 정확히는:
        
        ```
        GLOBAL_SUB_GOAL
        = Perception 회피 override 없이,
          PathProgress가 제공한 global lookahead target을 따라가는 local path를 생성하는 상태
        ```
        
        이 상태에서는 기존 1차 MVP와 동일하게 `progress.target_x_m`, `progress.target_y_m`, `progress.target_heading_rad`를 사용해서 local path를 생성한다. 현재 `local_path_planner_node`도 `GLOBAL_SUB_GOAL`에서 `makeLocalPathToTarget()`을 호출하고, status에 `used_global_sub_goal=true`, `stop_required=false`, `blocked=false`를 채운다.
        
        다만 “주행 중인 상태”는 `GLOBAL_SUB_GOAL`만 있는 건 아니야.
        
        ```
        GLOBAL_SUB_GOAL
        = global path 추종 주행 local path 생성
        
        AVOIDANCE
        = 장애물 회피 주행 local path 생성
        
        REJOIN
        = global path 복귀 주행 local path 생성
        
        BLOCKED
        = 정지 local path 생성
        ```
        
        그리고 중요한 건 `local_path_planner_node`는 실제 모터를 움직이는 노드가 아니라, follower/controller가 따라갈 local path와 status를 publish하는 노드라는 점이야. 실제 주행은 이후 follower/controller/Safety Supervisor가 이 local path를 보고 수행한다.
        
    
    ---
    
    - Q7. 그리고 BLOCKED 상태는 결국 장애물 위험 거리 안에 장애물이 존재함과 free space gap도 없고, best_cleareance 부족한 상태로 완전 아무것도 할 수 없는 상태인 거 같은데 이 상태에서는 local path 생성 전략은 새로운 공간을 찾기 위한 local path를 생성하려고 하는 것인지 아니면 BLOCKED 상태가 되면 아무것도 안하는 것인지 그것도 궁금해
        - A7.
        
        2차 MVP 기준으로는 **아니야.**
        
        `BLOCKED` 상태에서는 새로운 공간을 찾기 위해 움직이는 path를 만들지 않고, **현재 위치를 유지하는 hold path를 publish하고 `stop_required=true`를 내보내는 상태**로 보는 게 맞아.
        
        즉:
        
        ```
        BLOCKED
        = front_danger는 있는데,
          free_space_safe가 false라서 회피 path를 만들 수 없는 상태
        
        행동:
          이동하지 않음
          hold path publish
          stop_required = true
          blocked = true
        ```
        
        local path 전략은:
        
        ```
        current pose → current pose
        ```
        
        즉, 기존 `makeHoldLocalPath()`를 재사용하면 된다.
        
        ```
        poses[0] = current pose
        poses[1] = current pose
        ```
        
        그리고 중요한 점은 `BLOCKED`가 되어도 perception 판단은 계속 돈다는 거야. 로봇은 멈춰 있지만, 매 timer cycle마다 최신 `ObstacleModel`, `FreeSpaceModel`을 계속 보고 있다가 다음 조건이 생기면 상태를 다시 바꾼다.
        
        ```
        BLOCKED 유지:
          front_danger == true
          free_space_safe == false
        
        BLOCKED → AVOIDANCE:
          front_danger == true
          free_space_safe == true
        
        BLOCKED → REJOIN 또는 GLOBAL_SUB_GOAL:
          front_danger == false
        ```
        
        그러니까 2차 MVP에서 `BLOCKED`는 “아무 것도 안 한다”라기보다, **움직이지 않고 안전 정지하면서 perception 회복을 기다리는 상태**야.
        
        추후 고도화에서는 `BLOCKED`에서 다음 기능을 추가할 수 있어.
        
        ```
        - 제자리 회전해서 새로운 free-space 탐색
        - 짧은 후진 path 생성
        - 상위 mission planner에 path 재계획 요청
        - 관제 시스템에 blocked event publish
        ```
        
        하지만 2차 MVP에서는 여기까지 넣지 않는 게 좋아. 지금 단계에서는 `BLOCKED = hold path + stop_required + 상태 회복 감시`로 고정하는 게 안전하고 검증도 쉽다.
        

```bash
초기 2차 MVP:
  rejoin_done = distance_ok || heading_ok

실제 주행 안정화 단계:
  rejoin_done = distance_ok && heading_ok
```

## local path 생성 방식

처음에는 복잡한 A*나 DWA가 아니라 이렇게 가면 된다.

```
1. 현재 pose를 시작점으로 둔다.
2. 선택된 heading 방향으로 local_target_distance만큼 떨어진 점을 만든다.
3. 현재 pose와 local target 사이를 2~5개 waypoint로 보간한다.
4. nav_msgs/Path로 publish한다.
```

---

## 4. 트러블슈팅

### 4-1. “순간 관측 기반 local planning의 한계”

#### 현재 상황 파악

ROI :

```bash
# ROI 범위 (x, y, z) = (전방, 좌우, 높이) 제한 [m]
# 앞뒤 범위
roi_min_x: 0.35  
roi_max_x: 1.5

# 좌우 범위
roi_min_y: -1.0   # 왼쪽 1m
roi_max_y:  1.0   # 오른쪽 1m

# 상하 범위
roi_min_z: 0.05   # 지면 5cm 위 부터 인식
roi_max_z: 1.0

# Voxel downsampling leaf size [m]
voxel_leaf_size: 0.03
```

![image.png](image%2063.png)

ROI:

```bash
# ROI 범위 (x, y, z) = (전방, 좌우, 높이) 제한 [m]
# 앞뒤 범위
roi_min_x: 0.2    # 0.35
roi_max_x: 1.5

# 좌우 범위
roi_min_y: -1.0   # 왼쪽 1m
roi_max_y:  1.0   # 오른쪽 1m

# 상하 범위
roi_min_z: 0.05   # 지면 5cm 위 부터 인식
roi_max_z: 1.0

# Voxel downsampling leaf size [m]
voxel_leaf_size: 0.03
```

![image.png](image%2064.png)

ROI:

```bash
# ROI 범위 (x, y, z) = (전방, 좌우, 높이) 제한 [m]
# 앞뒤 범위
roi_min_x: 0.2    # 0.35
roi_max_x: 1.5

# 좌우 범위
roi_min_y: -2.0   # 왼쪽 1m
roi_max_y:  2.0   # 오른쪽 1m

# 상하 범위
roi_min_z: 0.05   # 지면 5cm 위 부터 인식
roi_max_z: 1.0

# Voxel downsampling leaf size [m]
voxel_leaf_size: 0.03
```

![image.png](image%2065.png)

ROI:

```bash
# ROI 범위 (x, y, z) = (전방, 좌우, 높이) 제한 [m]
# 앞뒤 범위
roi_min_x: 0.1    # 0.35
roi_max_x: 1.5

# 좌우 범위
roi_min_y: -2.0   # 왼쪽 1m
roi_max_y:  2.0   # 오른쪽 1m

# 상하 범위
roi_min_z: 0.05   # 지면 5cm 위 부터 인식
roi_max_z: 1.0

# Voxel downsampling leaf size [m]
voxel_leaf_size: 0.03
```

![image.png](image%2066.png)

```bash
1. 장애물이 안 보이는 순간 바로 clear로 판단하지 않기
2. 최근 관측 장애물을 일정 시간/거리 동안 기억하기
3. publish 직전 local path가 occupancy grid와 충돌하는지 검사하기
```

구조 개선 순서: 

```bash
1. AVOIDANCE 상태에서 front/left/right 모두 반영
2. front obstacle이 사라져도 바로 clear 처리하지 않기
3. AVOIDANCE 최소 유지 시간/거리 추가
4. inflation_radius 증가
5. 그 다음 avoidance_horizon_m 튜닝
```

### 트러블슈팅 문서

### 1. 문제 정의

> **LiDAR FOV 제한과 순간 관측 기반 local planning으로 인해, 회피 중 장애물이 사라진 것으로 오판되어 장애물 측면을 비비는 문제**
> 

즉, 단순히 “local path가 이상하다”가 아니라,

```
장애물 감지 → AVOIDANCE local path 생성 → 회피 중 장애물이 front/FOV 밖으로 빠짐
→ planner가 장애물 없음 또는 clear로 오판
→ REJOIN 또는 global path 복귀가 빨라짐
→ 실제 장애물 옆면을 충분히 벌리지 못하고 접촉/근접 주행
```

---

### 2. 현재 구조에서 발생하는 원인

#### 원인 1. local occupancy grid는 `base_link` 기준 “순간 지도”다

현재 `/perception/lidar/local_occupancy_grid`는 `base_link` 기준 로컬 OccupancyGrid다. 코드 주석과 파라미터에서도 `/perception/lidar/points_filtered`를 입력으로 받고, `frame_id: base_link` 기준 grid를 만든다고 되어 있다.

그리고 callback마다 새 `grid_msg`를 만들고 `grid_msg.data.assign(...)`으로 전체 grid를 다시 초기화한다. 즉, 이전 프레임에서 봤던 장애물을 기억하지 않는다.

따라서 현재 구조는

```
- 현재 LiDAR에 찍힌 점만 occupied
- 이전 프레임의 occupied 기록 없음
- 로봇이 움직여서 장애물이 안 보이면 grid에서도 사라짐
```

이게 **가장 큰 구조적 한계**다.

---

#### 원인 2. ObstacleModel은 front/left/right를 모두 만들지만, local planner가 front 중심으로 판단한다

`ObstacleModelNode`는 sector mask를 사용해서 front, left, right 대표 장애물을 각각 만든다. `selectRepresentativeCluster()`에서 `cluster.sector_mask & target_sector_mask`를 확인하고, front/left/right 각각 대표 클러스터를 선택한다.

즉, perception 쪽에서는 left/right 정보가 이미 있다.

문제는 local path planner의 상태 전환 로직이 실질적으로 `front_danger`, `front_clear` 중심이라는 점이야. 그래서 장애물이 회피 중 front sector에서 left/right sector로 넘어가면 planner 입장에서는 “front가 clear해졌다”고 해석할 수 있다.

현재 실제 현상은 이 흐름에 가깝다.

```
처음: obstacle_model.front.valid == true
→ AVOIDANCE 진입

회피 중: 장애물이 옆으로 이동하거나 로봇 자세가 바뀌면서 front에서 사라짐
→ obstacle_model.front.valid == false
→ front_clear로 판단될 가능성 증가

하지만 실제로는 obstacle_model.left 또는 right에 아직 가까운 장애물이 있음
→ 그런데 planner가 이를 충분히 반영하지 않음
→ 장애물 측면을 비빔
```

---

#### 원인 3. AVOIDANCE local path는 곡선이 아니라 짧은 직선 path다

AVOIDANCE에서는 FreeSpaceModel의 best heading 방향으로 `avoidance_horizon_m`만큼 떨어진 임시 target을 찍고, 현재 pose에서 그 target까지 직선 local path를 만든다. `makeLocalPathToTarget()`은 현재 pose와 target 사이를 보간해서 path를 생성하는 구조다.

즉, 현재 회피 path는 이런 형태이다.

```
현재 pose ───────── target point
```

한 번 생성된 local path 자체는 곡선 회피 경로가 아니다. 짧은 직선 path를 주기적으로 다시 만드는 구조다.

그래서 local path 중간에 장애물이 겹치는지 검사하지 않으면, FreeSpaceModel heading이 순간적으로 괜찮아 보여도 실제 path가 장애물 표면을 스칠 수 있다.

---

#### 원인 4. path collision check가 없다

현재 local path 생성은 target까지 직선 보간에 가깝고, 생성된 path가 local occupancy grid의 occupied cell과 겹치는지 검사하는 단계가 없다.

즉, 지금 구조는:

```
FreeSpaceModel이 준 heading이 괜찮음
→ target 생성
→ local path 생성
→ publish
```

에 가깝고,

```
생성된 local path가 occupied/inflated cell을 통과하는가?
unknown 영역을 과도하게 지나가는가?
장애물 memory 영역과 겹치는가?
```

를 최종 검증하지 않는다.

---

### 3. 최종 트러블슈팅 정의문

문서화한다면 이렇게 쓰면 된다.

> 현재 local planner는 동적 장애물 감지 시 FreeSpaceModel 기반 회피 heading으로 짧은 직선 local path를 생성한다. 그러나 LiDAR의 수평 FOV가 제한되어 있고 local occupancy grid가 매 프레임 새로 생성되는 base_link 기준 순간 지도이기 때문에, 회피 중 장애물이 front sector 또는 LiDAR 관측 범위 밖으로 빠지면 장애물이 사라진 것으로 판단될 수 있다. 또한 local planner의 상태 전환이 front obstacle 중심으로 이루어져 left/right 측면 장애물 정보를 충분히 반영하지 못하고, 생성된 local path에 대한 occupied grid 충돌 검사가 없어 장애물 측면을 충분히 이격하지 못한 채 비비며 주행하는 문제가 발생한다.
> 

---

### 4. 해결 설계 방향

해결책은 한 번에 거대한 구조를 만들기보다, 아래 순서로 가는 게 맞다.

```
1단계: 파라미터 보수화
2단계: FSM 상태 전환 개선
3단계: AVOIDANCE 유지 조건 추가
4단계: local path collision check 추가
5단계: obstacle memory grid 추가
6단계: 최종적으로 safety supervisor와 연동
```

---

### 5. 1단계 — 파라미터 보수화

이건 코드 구조를 바꾸기 전에 바로 할 수 있는 완화책이다.

#### 1️⃣ local path planner

```yaml
publish_rate_hz: 10.0

avoidance_horizon_m: 0.50
front_block_distance_m: 0.65
front_clear_distance_m: 1.00

side_block_distance_m: 0.45
side_clear_distance_m: 0.70
```

현재 `avoidance_horizon_m = 0.7`이 무조건 잘못된 건 아니지만, 지금처럼 동적 장애물과 사각 문제가 있는 상황에서는 0.45~0.55m 정도로 줄이고, 더 자주 재계획하는 편이 안정적이다.

중요한 건 `publish_rate_hz`다. local planner가 느리면 horizon을 줄여도 효과가 떨어진다.

---

#### 2️⃣ local occupancy grid

현재 obstacle inflation은 장애물 주변 cell을 occupied로 확장하는 구조다. `markInflatedOccupied()`에서 중심 cell 주변을 돌며 `distance <= inflation_radius_`인 cell을 occupied로 만든다.

추천값:

```yaml
inflation_radius: 0.20
```

그래도 비비면:

```yaml
inflation_radius: 0.25
```

---

#### 3️⃣pointcloud preprocess ROI

전방 가까운 장애물 누락 가능성을 줄이려면:

```yaml
roi_min_x: 0.15
roi_max_x: 2.0
roi_min_y: -1.3
roi_max_y:  1.3
roi_min_z: 0.03
roi_max_z: 1.0
voxel_leaf_size: 0.03
```

`roi_min_x = 0.35`는 base_link 기준 전방 35cm 안쪽 point를 버리는 의미라서, 장애물과 가까워졌을 때 위험 정보를 잃을 수 있다.

---

### 6. 2단계 — local planner FSM을 front-only에서 front+side 구조로 바꾸기

현재 해결의 핵심:

```
기존:
front_danger → AVOIDANCE
front_clear  → REJOIN

개선:
front_danger || side_danger → AVOIDANCE 유지
front_clear && side_clear   → REJOIN 허용
```

추가해야 할 판단값은 다음과 같다.

```cpp
bool front_danger;
bool front_clear;

bool left_danger;
bool right_danger;
bool side_danger;

bool left_clear;
bool right_clear;
bool side_clear;

bool obstacle_danger = front_danger || side_danger;
bool obstacle_clear  = front_clear && side_clear;
```

핵심은 **front가 clear여도 side가 clear가 아니면 REJOIN하지 않는 것**이다.

---

### 7. 3단계 — AVOIDANCE 상태를 최소 시간/거리 동안 유지하기

지금 문제는 장애물이 순간적으로 안 보이면 AVOIDANCE가 너무 빨리 풀릴 수 있다는 점이다.

따라서 AVOIDANCE 진입 시점을 저장해야 한다.

```cpp
rclcpp::Time avoidance_start_time_;
double avoidance_start_x_m_;
double avoidance_start_y_m_;

double avoidance_min_hold_sec_ = 1.0;
double avoidance_min_distance_m_ = 0.5;
```

그리고 AVOIDANCE 중에는 아래 조건을 만족하기 전까지 REJOIN을 금지한다.

```
AVOIDANCE 진입 후 최소 1.0초 이상 경과
또는
회피 시작 위치에서 최소 0.5m 이상 이동
그리고
front_clear && side_clear
```

이렇게 하면 장애물이 front에서 순간적으로 사라져도 바로 global path로 복귀하지 않는다.

---

### 8. 4단계 — local path collision check 추가

이건 구조적으로 매우 중요하다.

현재 `makeLocalPathToTarget()`이 만든 path를 바로 publish하지 말고, publish 전에 local occupancy grid와 검사해야 한다.

구조는 이렇게 잡으면 된다.

```
makeLocalPathToTarget()
→ candidate local path 생성
→ checkLocalPathCollision(candidate_path, local_occupancy_grid)
→ 안전하면 publish
→ 위험하면 다른 heading 재선택 또는 BLOCKED/HOLD
```

검사 기준은 다음과 같다.

```
1. path pose 주변 cell에 occupied가 있는가?
2. path pose 주변 cell이 inflated occupied인가?
3. unknown cell 비율이 너무 높은가?
4. obstacle memory grid와 겹치는가?
```

MVP에서는 일단 `occupied`만 검사해도 된다.

```
path 위 pose를 base_link 기준으로 변환
→ local occupancy grid index로 변환
→ 주변 반경 N cell 검사
→ occupied 발견 시 path reject
```

이 단계가 들어가면 FreeSpaceModel heading이 순간적으로 잘못되어도, 최종 path가 장애물과 겹치면 막을 수 있다.

---

### 9. 5단계 — obstacle memory grid 추가

이게 근본 해결에 가장 가깝다.

현재 local occupancy grid는 base_link 기준 순간 지도다. 따라서 과거 장애물을 기억하려면 별도 memory layer가 필요하다.

추천 구조:

```
/perception/lidar/points_filtered
        ↓
obstacle_memory_node
        ↓
/perception/lidar/obstacle_memory_grid
```

memory grid 좌표계는 `odom` 또는 `mission_map` 기준이 좋다.

#### 왜 base_link memory는 비추천인가?

base_link에 과거 장애물을 저장하면, 로봇이 움직일 때 장애물도 로봇과 같이 움직이는 것처럼 보인다.

```
실제 장애물: 고정
로봇: 앞으로 이동
base_link memory: 장애물이 로봇 기준으로 같이 따라오는 것처럼 왜곡
```

그래서 base_link memory는 0.3~0.5초짜리 짧은 latch 정도로만 쓸 수 있다.

#### 추천은 odom 또는 mission_map

현재 프로젝트에서 localization pose가 mission_map 기준으로 나오고 있으니, MVP에서는 mission_map 기준 short-term memory도 가능하다.

구조는 이렇다.

```
points_filtered의 point: base_link 기준
현재 robot pose: mission_map 기준 x, y, yaw

x_map = robot_x + cos(yaw) * x_base - sin(yaw) * y_base
y_map = robot_y + sin(yaw) * x_base + cos(yaw) * y_base

→ mission_map 기준 obstacle memory cell에 기록
→ 최근 1~3초 동안 유지
→ 시간이 지나면 decay/delete
```

중요한 점은 **영구 지도처럼 저장하면 안 된다**는 것. 동적 장애물이기 때문에 TTL이 필요하다.

```yaml
obstacle_memory_ttl_sec: 2.0
obstacle_memory_resolution: 0.10
obstacle_memory_inflation_radius: 0.25
```

---

### 10. 최종 설계 구조

최종 구조는 이렇게 잡는 게 좋다.

```
/scan_3D
  ↓
pointcloud_preprocess_node
  ↓
/perception/lidar/points_filtered
  ├─ obstacle_cluster_node
  │    ↓
  │  /perception/lidar/obstacle_clusters
  │    ↓
  │  obstacle_model_node
  │    ↓
  │  /perception/lidar/obstacle_model
  │      - front
  │      - left
  │      - right
  │
  ├─ local_occupancy_grid_node
  │    ↓
  │  /perception/lidar/local_occupancy_grid
  │      - base_link 기준 순간 grid
  │
  ├─ free_space_model_node
  │    ↓
  │  /perception/lidar/free_space_model
  │      - best_heading
  │      - clearance
  │      - risk
  │
  └─ obstacle_memory_node
       ↓
     /perception/lidar/obstacle_memory_grid
       - odom 또는 mission_map 기준 short-term memory

/navigation/path_progress/spot_01
/localization/pose
/perception/lidar/obstacle_model
/perception/lidar/free_space_model
/perception/lidar/local_occupancy_grid
/perception/lidar/obstacle_memory_grid
        ↓
local_path_planner_node
        ↓
/navigation/local_path/spot_01
/navigation/local_planner_status/spot_01
        ↓
safety_supervisor
        ↓
/control/cmd_vel/robot_1
```

---

### 11. 적용 순서 추천

#### 1차 적용: 바로 해야 할 것

```
1. local_path_planner publish_rate_hz 증가
2. avoidance_horizon_m 0.45~0.55로 조정
3. inflation_radius 0.20~0.25로 증가
4. ROI min_x 0.35 → 0.15 정도로 완화
```

이건 빠른 완화책이다.

---

#### 2차 적용: local planner FSM 개선

```
1. isLeftDanger()
2. isRightDanger()
3. isSideDanger()
4. isSideClear()
5. obstacle_danger = front_danger || side_danger
6. obstacle_clear = front_clear && side_clear
```

그리고 AVOIDANCE 상태에서는:

```
front가 clear여도 side가 가까우면 AVOIDANCE 유지
front/side 모두 clear일 때만 REJOIN
```

이게 현재 문제에 가장 직접적인 해결책이다.

---

#### 3차 적용: AVOIDANCE hysteresis/latch

```
1. AVOIDANCE 진입 시간 저장
2. AVOIDANCE 진입 pose 저장
3. 최소 유지 시간 또는 최소 회피 거리 조건 추가
4. front obstacle이 사라져도 즉시 clear 처리 금지
```

이걸 넣으면 LiDAR FOV에서 순간적으로 장애물이 사라지는 문제를 많이 줄일 수 있다.

---

#### 4차 적용: local path collision check

```
1. candidate local path 생성
2. local occupancy grid 기준 충돌 검사
3. occupied와 겹치면 reject
4. reject 시 다른 heading 선택 또는 BLOCKED/HOLD
```

이건 회피 path 품질을 안정화하는 핵심 단계다.

---

#### 5차 적용: obstacle memory grid

```
1. points_filtered를 mission_map 또는 odom 기준으로 변환
2. 최근 장애물 cell을 TTL 방식으로 저장
3. memory grid publish
4. local path collision check에서 memory grid도 함께 확인
```

이게 들어가면 “방금 봤던 장애물이 FOV 밖으로 사라져서 없는 것으로 판단”하는 문제가 구조적으로 줄어든다.

---

### 12. 최종 목표 상태

개선 후 local planner는 이렇게 동작해야 한다.

```
장애물 감지
→ AVOIDANCE 진입
→ FreeSpaceModel heading으로 짧은 candidate path 생성
→ local occupancy grid + obstacle memory grid로 path collision check
→ 안전한 경우에만 local path publish
→ 회피 중 front에서 장애물이 사라져도 left/right 또는 memory가 위험하면 AVOIDANCE 유지
→ front_clear && side_clear && memory_clear && 최소 회피 조건 만족
→ REJOIN
→ global path 복귀
```

즉, 최종 설계의 핵심은 이거야.

```
순간 관측 기반 회피
→ 상태 기억 기반 회피
→ 경로 검증 기반 회피
```

지금 가장 먼저 해야 할 건 **local path planner에서 front-only 판단을 front+left+right 판단으로 바꾸고, AVOIDANCE 최소 유지 조건을 추가하는 것**이다. 그다음에 local path collision check, 마지막으로 obstacle memory grid를 붙이는 순서가 가장 안전하고 현실적이야.

---

### 1차 수정:  obstacle model의 측면 위험 감지 추가 -  `local_path_planner_node`

> front만 보던 planner를 front/left/right 기반의 “측면 위험 유지형 FSM(side-aware FSM)”으로 바꾸는 것
> 
- `local_path__planner.param.yaml`
    
    ```bash
    local_path_planner_node:
      ros__parameters:
        robot_id: "spot_01"
        global_frame: "mission_map"
    
        # Input: Obstacle Model & Free Space Model Topic
        obstacle_model_topic: "/perception/lidar/obstacle_model"
        free_space_model_topic: "/perception/lidar/free_space_model"
    
        # Input: Path Progress & Localization Pose
        path_progress_topic: "/navigation/path_progress/spot_01"
        localization_pose_topic: "/localization/pose"
    
        # Output: Local Path & Local Planner Status
        local_path_topic: "/navigation/local_path/spot_01"
        local_planner_status_topic: "/navigation/local_planner_status/spot_01"
        
        # local path/status publish 주기
        publish_rate_hz: 10.0
        
        obstacle_model_timeout_sec: 0.5     # Obstacle Model's timeout: 0.5s
        free_space_model_timeout_sec: 0.5   # Free Space Model's timeout: 0.5s
        path_progress_timeout_sec: 0.5      # PathProgress's timeout: 0.5s
        pose_timeout_sec: 0.5               # Localization Pose's timeout: 0.5s
    
        # Front obstacle hysteresis
        # - 0.60m 이내: front danger
        # - 1.00m 이상: 멀어져야 front clear
        front_block_distance_m: 0.60        # GLOBAL_SUB_GOAL → AVOIDANCE/BLOCKED 진입 기준 (기존: 0.60)
        front_clear_distance_m: 1.00        # AVOIDANCE → REJOIN 전이 기준 (기존: 0.80)
    
        # Side obstacle hysteresis
        # AVOIDANCE 중 장애물이 front에서 left/right로 빠져도
        # side가 충분히 멀어지기 전까지 REJOIN하지 않도록 한다.
        side_block_distance_m: 0.45
        side_clear_distance_m: 0.70
    
        avoidance_min_clearance_m: 0.60     # FreeSpaceModel's Gap을 실제 회피 path로 쓸 수 있는지의 Planner 기준
        avoidance_horizon_m: 0.50           # 회피 target을 현재 pose 기준 몇 m 앞에 둘지 (기존: 0.70)
    
        rejoin_tolerance_m: 0.30            # REJOIN 상태에서 global path로 복귀했는지 판단하는 거리 기준
        rejoin_heading_tolerance_rad: 0.35  # REJOIN 상태에서 로봇 yaw가 global path target 방향과 정렬되었는지 판단하는 각도 기준
    
        local_path_spacing_m: 0.10          # local path 점 사이 간격
        max_local_path_length_m: 1.50       # 한 번에 만들 local path 최대 길이
        local_target_tolerance_m: 0.20      # 현재 pose가 local target에 도달했다고 판단하는 거리
    
        # local target yaw를 PathProgress.target_yaw_rad로 둘지,
        # selected_heading_rad로 둘지 선택
        use_path_progress_target_yaw: true
    ```
    
- `local_path_planner_node.hpp/cpp`
    
    #### 1. local_path_planner_node.hpp
    
    ```bash
    // [추가] 측면 장애물 위험/해소 판단 함수
    // - Left/Right sector의 대표 장애물 기반으로
    //   AVOIDANCE 유지 및 REJOIN 허용 여부 판단
    bool isLeftDanger(const ObstacleModelMsg &obstacle_model) const;
    bool isRightDanger(const ObstacleModelMsg &obstacle_model) const;
    bool isSideDanger(const ObstacleModelMsg &obstacle_model) const;
    
    bool isLeftClear(const ObstacleModelMsg &obstacle_model) const;
    bool isRightClear(const ObstacleModelMSg &obstacle_model) const;
    bool isSideClear(const ObstacleModelMsg &obstacle_model) const;
    
    // [추가] 측면 장애물 위험/해제 거리
    // - AVOIDANCE 중 장애물이 front에서 Left/Right로 빠졌을 때,
    //   너무 빨리 REJOIN하지 않도록 측면 clearance를 함께 판단
    double side_block_distance_m_;          // 측면 위험 진입 거리
    double side_clear_distance_m_;          // 측면 위험 해제 거리
    ```
    
    - 측면 위험 판단 및 해소 판단 함수 추가
    
    ---
    
    #### 2. local_path_planner_node.cpp - publishLocalPlanning() 함수 수정
    
    ```bash
    // ====================
    // [4] 판단 변수 계산
    // =====================
    
    // 최신 obstacle model, free space model 불러오기 
    const auto &obstacle_model   = *latest_obstacle_model_;
    const auto &free_space_model = *latest_free_space_model_;
    
    // 전방 위험 판단 & 해소 여부
    const bool front_danger   = isFrontDanger(obstacle_model);   // 전방 위험 판단 flag
    const bool front_clear    = isFrontClear(obstacle_model);    // 전방 위험 해소 flag
    
    // [추가] 측면 위험 판단 & 해소 여부
    const bool side_danger    = isSideDanger(obstacle_model);    // 측면 위험 판단 Flag
    const bool side_clear     = isSideClear(obstacle_model);     // 측면 위험 해소 Flag
    
    // [추가] front or side 중 하나라도 가까우면 장애물 위험으로 판단
    const bool obstacle_clear = front_clear && side_clear;
    
    const bool free_space_ok  = isFreeSpaceAcceptableForMotion(free_space_model); 
    const bool rejoin_done    = isRejoinDone(progress);
    
    switch (planner_state_) {
        case LocalPlannerStatusMsg::IDLE:
        case LocalPlannerStatusMsg::INVALID_INPUT:
        case LocalPlannerStatusMsg::GLOBAL_GOAL_REACHED:
        case LocalPlannerStatusMsg::GLOBAL_SUB_GOAL:
        {
            /*
            정상 global path 추종 상태.
    
            기존: front_danger만 보고 AVOIDANCE 진입 여부 판단.
    
            변경:
                front 또는 side 중 하나라도 위험하면 obstacle_danger로 판단.
                단, side_block_distance_m_를 너무 크게 잡으면 벽/측면 물체에도 자주 회피하므로
                YAML 튜닝이 중요하다.
            */
            // [추가]
            if (!obstacle_danger) publishGlobalSubGoalPath(stamp, pose, progress);
            else if (free_space_ok) publishAvoidancePath(stamp, pose, progress, obstacle_model, free_space_model);
            else {
                publishBlocked(
                    stamp, pose, progress, obstacle_model, free_space_model,
                    "obstacle danger and no acceptable free-space"
                );
            }
    
            break;
        }
    
        case LocalPlannerStatusMsg::AVOIDANCE:
        {
            /*
            AVOIDANCE 상태.
    
            핵심 변경:
                front가 clear여도 side가 clear가 아니면 REJOIN 금지.
                obstacle_clear = front_clear && side_clear일 때만 REJOIN 허용.
            */
            // [추가]
            if (obstacle_clear) {
                publishRejoinPath(stamp, pose, progress, obstacle_model, free_space_model);
            }
            else if (free_space_ok) {
                publishAvoidancePath(stamp, pose, progress, obstacle_model, free_space_model);
            }
            else {
                publishBlocked(
                    stamp, pose, progress, obstacle_model, free_space_model,
                    "avoidance active but no acceptable free-space"
                );
            }
            break;
        }
    
        case LocalPlannerStatusMsg::REJOIN:
        {
            /*
            REJOIN 상태.
    
            회피 후 global path로 복귀 중에도 장애물이 다시 가까워지면
            AVOIDANCE 또는 BLOCKED로 되돌린다.
    
            또한 front/side가 모두 clear가 아니면 global path 복귀 완료로 보지 않는다.
            */
    
            if (obstacle_danger && free_space_ok) {
                publishAvoidancePath(stmp, pose, progress, obstacle_model, free_space_model);
            }
            else if (obstacle_danger && !free_space_ok) {
                publishBlocked(
                    stamp, pose, progress, obstacle_model, free_space_model,
                    "rejoin interrupted by obstacle danger and no acceptable free-space"
                );
            }
            else if (!obstacle_clear) {
                /*
                danger는 아니지만 clear도 아닌 hysteresis 구간.
                예:
                    side distance가 side_block_distance_m_보다 멀지만
                    side_clear_distance_m_보다 아직 가까운 경우.
    
                이때 global path로 바로 복귀하면 장애물 옆면을 비빌 수 있으므로
                free-space가 가능하면 AVOIDANCE를 조금 더 유지한다.
                */
                if (free_space_ok) {
                    publishAvoidancePath(stamp, pose, progress, obstacle_model, free_space_model);
                } else {
                    publishBlocked(
                        stamp, pose, progress, obstacle_model, free_space_model,
                        "rejoin hysteresis zone and no acceptable free-space"
                    );
                }
            }
            else if (rejoin_done) {
                publishGlobalSubGoalPath(stamp, pose, progress);
            }
            else {
                publishRejoinPath(stamp, pose, progress, obstacle_model, free_space_model);
            }
    
            break;
        }
    
        case LocalPlannerStatusMsg::BLOCKED:
        {
            /*
            BLOCKED 상태.
    
            obstacle_clear가 되면 REJOIN으로 복귀.
            clear는 아니지만 free-space가 회복되면 AVOIDANCE 재개.
            */
            
            if (obstacle_clear) {
                publishRejoinPath(stamp, pose, progress, obstacle_model, free_space_model);
            }
            else if (free_space_ok) {
                publishAvoidancePath(stamp, pose, porgress, obstacle_model, free_space_model);
            }
            else {
                publishBlocked(
                    stamp, pose, progress, obstacle_model, free_space_model,
                    "blocked waiting for obstacle clear or acceptable free-space"
                );
            }
    
            break;
        }
    
        default:
        {
            // GLOBAL_SUB_GOAL 상태 처리 함수 호출
            publishGlobalSubGoalPath(stamp, pose, progress);
            break;
        }
    }
    ```
    
    - 추가된 측면 위험 판단 및 해소 판단 flag 기반으로 switch FSM 수정
    
- **QnA**
    
    ### 1. side 판단 함수 6개의 역할
    
    먼저 전체 그림은 이렇게 보면 돼.
    
    ```
                        base_link 기준 전방 x축
                              ↑
                              |
                  LEFT        |        FRONT        RIGHT
               +30° ~ +60°    |       -30°~+30°   -60°~-30°
                     \        |        /
                      \       |       /
                       \      |      /
                        \     |     /
                         \    |    /
                          \   |   /
                           [ ROBOT ]
    ```
    
    `ObstacleModel`은 이미 `front`, `left`, `right` 대표 장애물을 각각 만들어서 보내고 있어. `ObstacleModelNode`는 sector mask를 보고 front/left/right 대표 클러스터를 각각 선별하고, 각 대표 장애물의 `nearest_distance_xy`도 함께 출력한다.
    
    즉 local planner가 받는 데이터는 대략 이런 형태야.
    
    ```
    /perception/lidar/obstacle_model
    
    front.valid
    front.nearest_distance_xy
    
    left.valid
    left.nearest_distance_xy
    
    right.valid
    right.nearest_distance_xy
    ```
    
    ---
    
    #### 1-1. `isLeftDanger()`
    
    역할:
    
    ```
    왼쪽 sector에 장애물이 있고,
    그 장애물이 side_block_distance_m보다 가까운가?
    ```
    
    도형으로 보면:
    
    ```
                 LEFT sector
                 +30° ~ +60°
    
                      장애물 ●
                           \
                            \
                             \
                              [ROBOT]
    
    side_block_distance_m 안쪽이면
    → isLeftDanger() = true
    ```
    
    판단 로직:
    
    ```cpp
    if (!obstacle_model.left.valid) return false;
    if (!finite(left.nearest_distance_xy)) return false;
    
    return left.nearest_distance_xy < side_block_distance_m_;
    ```
    
    입력 데이터:
    
    ```
    obstacle_model.left.valid
    obstacle_model.left.nearest_distance_xy
    side_block_distance_m_
    ```
    
    return:
    
    ```
    true  = 왼쪽이 위험함
    false = 왼쪽 위험은 아님
    ```
    
    쓰이는 곳:
    
    ```
    isSideDanger()
    → obstacle_danger
    → AVOIDANCE 유지 / REJOIN 차단
    ```
    
    ---
    
    #### 1-2. `isRightDanger()`
    
    역할은 `isLeftDanger()`와 완전히 같고, 방향만 오른쪽이야.
    
    ```
                 RIGHT sector
                 -60° ~ -30°
    
              ● 장애물
              /
             /
            /
       [ROBOT]
    
    side_block_distance_m 안쪽이면
    → isRightDanger() = true
    ```
    
    판단 로직:
    
    ```cpp
    if (!obstacle_model.right.valid) return false;
    if (!finite(right.nearest_distance_xy)) return false;
    
    return right.nearest_distance_xy < side_block_distance_m_;
    ```
    
    ---
    
    #### 1-3. `isSideDanger()`
    
    역할:
    
    ```
    왼쪽 또는 오른쪽 중 하나라도 위험한가?
    ```
    
    도형:
    
    ```
    CASE A
    왼쪽 위험
    → side_danger = true
    
    CASE B
    오른쪽 위험
    → side_danger = true
    
    CASE C
    양쪽 모두 위험 아님
    → side_danger = false
    ```
    
    로직:
    
    ```cpp
    return isLeftDanger(obstacle_model) || isRightDanger(obstacle_model);
    ```
    
    왜 필요하냐면, FSM에서는 왼쪽/오른쪽을 따로따로 쓰기보다 최종적으로는 이렇게 판단해야 하기 때문이야.
    
    ```
    front_danger || side_danger
    → obstacle_danger
    → 회피 필요
    ```
    
    ---
    
    #### 1-4. `isLeftClear()`
    
    역할:
    
    ```
    왼쪽 sector가 충분히 안전하게 비었는가?
    ```
    
    도형:
    
    ```
                 LEFT sector
    
            side_clear_distance_m 바깥
                      ● 장애물
                       \
                        \
                         \
                         [ROBOT]
    
    장애물이 없거나,
    있어도 side_clear_distance_m보다 멀면
    → isLeftClear() = true
    ```
    
    로직:
    
    ```cpp
    if (!obstacle_model.left.valid) return true;
    if (!finite(left.nearest_distance_xy)) return false;
    
    return left.nearest_distance_xy > side_clear_distance_m_;
    ```
    
    여기서 중요한 점은 `side_block_distance_m`이 아니라 `side_clear_distance_m`을 쓴다는 거야.
    
    ```
    side_block_distance_m = 위험 진입 기준
    side_clear_distance_m = 위험 해제 기준
    ```
    
    ---
    
    #### 1-5. `isRightClear()`
    
    오른쪽에 대해 동일한 판단이야.
    
    ```
    오른쪽 대표 장애물이 없거나,
    있어도 side_clear_distance_m보다 멀면
    → isRightClear() = true
    ```
    
    ---
    
    #### 1-6. `isSideClear()`
    
    역할:
    
    ```
    왼쪽과 오른쪽이 모두 clear인가?
    ```
    
    로직:
    
    ```cpp
    return isLeftClear(obstacle_model) && isRightClear(obstacle_model);
    ```
    
    이게 중요한 이유는 REJOIN 조건 때문이다.
    
    ```
    front_clear == true
    side_clear == false
    → 아직 REJOIN하면 안 됨
    
    front_clear == true
    side_clear == true
    → REJOIN 가능
    ```
    
    즉 최종적으로는 이렇게 쓰여.
    
    ```cpp
    const bool obstacle_danger = front_danger || side_danger;
    const bool obstacle_clear  = front_clear && side_clear;
    ```
    
    ---
    
    ### 2. `side_block_distance_m`, `side_clear_distance_m`는 어떻게 거리 측정하나?
    
    local path planner가 raw point를 다시 계산하는 건 아니야.
    
    정확히는:
    
    ```
    ObstacleClusterNode
    → 각 클러스터의 nearest_distance_xy 계산
    
    ObstacleModelNode
    → left/right/front 대표 클러스터 선택
    
    LocalPathPlannerNode
    → obstacle_model.left.nearest_distance_xy 값을 그대로 사용
    → if문으로 side_block/side_clear와 비교
    ```
    
    즉 local planner에서는 거리를 새로 측정하지 않고, **ObstacleModel에서 보내준 `nearest_distance_xy` 값을 그대로 임계값과 비교**한다.
    
    구조는 이거야.
    
    ```
    LiDAR point cloud
      ↓
    ObstacleClusterNode
      - 클러스터 생성
      - nearest_distance_xy 계산
      ↓
    ObstacleModelNode
      - front 대표 장애물
      - left 대표 장애물
      - right 대표 장애물
      ↓
    LocalPathPlannerNode
      - left.nearest_distance_xy < side_block_distance_m ?
      - left.nearest_distance_xy > side_clear_distance_m ?
    ```
    
    `ObstacleModelNode`는 sector mask를 기준으로 front/left/right 대표 클러스터를 각각 고르고, 가장 가까운 클러스터를 대표 장애물로 선택하는 구조야.
    
    따라서 local planner의 판단은 단순히 이런 형태가 맞아.
    
    ```cpp
    if (obstacle_model.left.nearest_distance_xy < side_block_distance_m_) {
        left_danger = true;
    }
    ```
    
    ---
    
    ### 3. 왜 `isLeftDanger()`, `isRightDanger()`가 있는데 `isSideDanger()`가 또 필요한가?
    
    이건 코드 구조를 깔끔하게 만들기 위한 “상위 판단 함수”야.
    
    왼쪽/오른쪽 개별 판단은 세부 상태고,
    
    ```
    isLeftDanger()
    isRightDanger()
    ```
    
    FSM이 실제로 알고 싶은 건 보통 이거야.
    
    ```
    측면 전체가 위험한가?
    ```
    
    그래서 `isSideDanger()`를 둔다.
    
    ```cpp
    bool isSideDanger(...)
    {
        return isLeftDanger(...) || isRightDanger(...);
    }
    ```
    
    이렇게 하면 FSM 코드가 깔끔해져.
    
    나쁜 예:
    
    ```cpp
    if (front_danger || isLeftDanger(obstacle_model) || isRightDanger(obstacle_model)) {
        publishAvoidancePath(...);
    }
    ```
    
    좋은 예:
    
    ```cpp
    const bool side_danger = isSideDanger(obstacle_model);
    const bool obstacle_danger = front_danger || side_danger;
    
    if (obstacle_danger) {
        publishAvoidancePath(...);
    }
    ```
    
    또 나중에 side 판단 기준이 바뀌어도 `isSideDanger()` 내부만 바꾸면 돼.
    
    예를 들어 추후에 이런 조건을 추가할 수 있어.
    
    ```
    왼쪽은 위험하지만 현재 회피 방향이 오른쪽이면 영향 낮게 보기
    오른쪽은 위험하지만 현재 회피 방향이 왼쪽이면 영향 낮게 보기
    양쪽 모두 가까우면 BLOCKED로 보기
    ```
    
    이런 확장을 위해서도 `isSideDanger()`를 따로 두는 게 좋다.
    
    ---
    
    ### 4. AVOIDANCE에서 기존 hysteresis 구간이 사라진 것 아닌가?
    
    수정안의 AVOIDANCE 구조가 아래와 같다면:
    
    ```cpp
    case AVOIDANCE:
    {
        if (obstacle_clear) {
            publishRejoinPath(...);
        }
        else if (free_space_ok) {
            publishAvoidancePath(...);
        }
        else {
            publishBlocked(...);
        }
        break;
    }
    ```
    
    **hysteresis가 사라진 게 아니라, `obstacle_clear` 조건 안에 흡수된 것**이야.
    
    왜냐하면 `obstacle_clear`는 이렇게 정의되기 때문이야.
    
    ```cpp
    obstacle_clear = front_clear && side_clear;
    ```
    
    그리고 side 기준은 이렇게 나뉜다.
    
    ```
    거리 < side_block_distance_m
    → danger
    
    side_block_distance_m <= 거리 <= side_clear_distance_m
    → danger는 아니지만 clear도 아님
    → hysteresis zone
    
    거리 > side_clear_distance_m
    → clear
    ```
    
    예를 들어:
    
    ```
    side_block_distance_m = 0.45
    side_clear_distance_m = 0.70
    
    left.nearest_distance_xy = 0.55
    ```
    
    이 경우:
    
    ```
    isLeftDanger() = false
    isLeftClear()  = false
    ```
    
    즉,
    
    ```
    danger는 아니지만 clear도 아님
    ```
    
    이게 hysteresis 구간이야.
    
    AVOIDANCE 코드에서 이 경우는:
    
    ```cpp
    if (obstacle_clear) {
        // false
    }
    else if (free_space_ok) {
        // 여기로 들어감
        // AVOIDANCE 유지
    }
    ```
    
    그래서 AVOIDANCE 상태에서는 hysteresis 구간이 사라진 게 아니라,
    
    ```
    clear가 아니면 계속 AVOIDANCE 유지
    ```
    
    로 처리되는 거야.
    
    즉 수정된 AVOIDANCE 구조는 괜찮아. 오히려 지금 문제에는 더 안전한 구조야.
    
    ---
    
    ### 5. REJOIN의 `!obstacle_clear` 조건은 정확히 무엇인가?
    
    REJOIN 상태에서 이런 조건이 있다고 했지.
    
    ```cpp
    else if (!obstacle_clear) {
        if (free_space_ok) {
            publishAvoidancePath(...);
        } else {
            publishBlocked(...);
        }
    }
    ```
    
    여기서 `!obstacle_clear`는 아래 세 가지 중 하나야.
    
    ```
    1. front_clear == false
    2. side_clear == false
    3. front_clear와 side_clear 둘 다 false
    ```
    
    즉, 아직 전방 또는 측면이 완전히 clear하지 않다는 뜻이야.
    
    더 구체적으로 보면:
    
    ```
    front distance가 front_block보다 멀어져서 danger는 아니지만,
    front_clear_distance보다 아직 가까움
    
    또는
    
    side distance가 side_block보다 멀어져서 danger는 아니지만,
    side_clear_distance보다 아직 가까움
    ```
    
    예시:
    
    ```
    front_block_distance_m = 0.60
    front_clear_distance_m = 1.00
    
    front.nearest_distance_xy = 0.75
    ```
    
    이 경우:
    
    ```
    front_danger = false
    front_clear  = false
    ```
    
    상태 해석:
    
    ```
    전방 장애물이 아주 위험한 건 아님.
    하지만 1.00m 이상 멀어진 것도 아님.
    아직 global path로 복귀하기엔 애매함.
    ```
    
    측면도 똑같아.
    
    ```
    side_block_distance_m = 0.45
    side_clear_distance_m = 0.70
    
    left.nearest_distance_xy = 0.55
    ```
    
    이 경우:
    
    ```
    left_danger = false
    left_clear  = false
    side_clear  = false
    ```
    
    상태 해석:
    
    ```
    왼쪽 장애물이 즉시 위험할 정도는 아님.
    하지만 충분히 떨어진 것도 아님.
    global path로 바로 붙으면 장애물 옆면을 비빌 수 있음.
    ```
    
    그래서 REJOIN에서 `!obstacle_clear`이면:
    
    ```
    아직 복귀하지 말고,
    free-space가 가능하면 회피를 조금 더 유지한다.
    ```
    
    라는 의미가 된다.
    
    도형으로 보면:
    
    ```
    [ROBOT] ───── global path 방향
       |
       |  0.55m
       ● 장애물
    
    0.45m 이내는 아니므로 danger는 아님.
    하지만 0.70m 이상 떨어진 것도 아니므로 clear도 아님.
    → REJOIN하면 옆면을 비빌 수 있음.
    → AVOIDANCE 조금 더 유지.
    ```
    
    ---
    
    ### 6. `avoidance_min_clearance_m = 0.60`은 무엇인가?
    
    네 이해가 맞아.
    
    `avoidance_min_clearance_m`은 **FreeSpaceModel이 선택한 best gap을 실제 회피 경로로 써도 되는지 planner가 한 번 더 거르는 기준값**이야.
    
    FreeSpaceModel은 local occupancy grid를 angular bin으로 나누고, 연속된 candidate bin을 gap으로 묶은 뒤 가장 좋은 gap을 선택한다. 그리고 선택된 gap의 중심 heading, min clearance, score 등을 `FreeSpaceModel` 메시지로 publish한다. 선택된 gap의 `min_clearance`는 `best_clearance`로 메시지에 들어간다.
    
    즉 흐름은 이거야.
    
    ```
    FreeSpaceModelNode
      ↓
    candidate gaps 계산
      ↓
    selected_gap 선택
      ↓
    selected_gap.min_clearance
      ↓
    free_space_model.best_clearance
      ↓
    LocalPathPlannerNode
      ↓
    best_clearance >= avoidance_min_clearance_m ?
    ```
    
    의미는:
    
    ```
    FreeSpaceModel이 “이 방향으로 갈 수 있다”고 했더라도,
    그 방향의 최소 clearance가 0.60m보다 작으면
    planner는 실제 회피 path로 쓰지 않는다.
    ```
    
    예시:
    
    ```
    avoidance_min_clearance_m = 0.60
    
    free_space_model.path_available = true
    free_space_model.best_clearance = 0.45
    
    → free_space_ok = false
    → AVOIDANCE path 생성하지 않음
    → BLOCKED 또는 HOLD
    ```
    
    반대로:
    
    ```
    free_space_model.best_clearance = 0.85
    
    → free_space_ok = true
    → FreeSpaceModel heading을 사용해 AVOIDANCE path 생성
    ```
    
    즉 `avoidance_min_clearance_m`은 **전방/측면 obstacle 판단 거리와는 다른 값**이야.
    
    차이를 정리하면:
    
    ```
    front_block_distance_m / front_clear_distance_m
    side_block_distance_m / side_clear_distance_m
    → ObstacleModel 기반 상태 전환 기준
    → AVOIDANCE, REJOIN, BLOCKED를 결정하는 데 사용
    
    avoidance_min_clearance_m
    → FreeSpaceModel 기반 회피 heading 사용 가능 여부 기준
    → 이 gap 방향으로 local path를 만들어도 되는지 판단
    ```
    
    ---
    
    ### 한 줄 정리
    
    이번 수정의 핵심은 이거야.
    
    ```
    isLeftDanger / isRightDanger
    → 측면이 가까운지 판단
    
    isSideDanger
    → 왼쪽/오른쪽 중 하나라도 위험하면 AVOIDANCE 유지
    
    isLeftClear / isRightClear
    → 측면이 충분히 멀어졌는지 판단
    
    isSideClear
    → 양쪽 모두 충분히 멀어졌을 때만 REJOIN 허용
    
    side_block_distance_m
    → 측면 위험 진입 거리
    
    side_clear_distance_m
    → 측면 위험 해제 거리
    
    avoidance_min_clearance_m
    → FreeSpaceModel이 고른 gap을 회피 경로로 써도 되는 최소 여유 거리
    ```
    
    따라서 지금 수정 방향은 맞아.
    
    AVOIDANCE에서는 `obstacle_clear`가 아니면 계속 회피를 유지하고, REJOIN에서도 `!obstacle_clear`이면 다시 AVOIDANCE로 돌리는 구조가 현재 문제를 막는 데 가장 직접적이야.
    
- 검증 파라미터
    
    `local_path_planner.param.yaml` :
    
    ```bash
    side_block_distance_m: 0.45         # 위험 진입 기준
    side_clear_distance_m: 0.70         # 위험 해제 기준: 있어도 side_clear_distance_m보다 멀면 is*Clear() = True
    ```
    
    - 인규형 모델 기준 위험 진입 기준이 0.3m임 나는 어떻게 정의할 지는 실제 검증 후 확인
    - 검증 방향성:
        
        ```bash
        초기 시작 값:
        side_block_distance_m: 0.40
        side_clear_distance_m: 0.60
        
        보수적인 시작 값:
        side_block_distance_m: 0.45
        side_clear_distance_m: 0.70
        
        ```
        
        - 의미 :
            
            ```bash
            측면 장애물이 0.40m 이내
            → side danger
            
            0.40m ~ 0.65m
            → danger는 아니지만 clear도 아님
            → AVOIDANCE 유지 가능 구간
            
            0.65m 이상
            → side clear
            ```
            
            즉, `side_clear_distance_m`은 `side_block_distance_m`보다 크게 둬서 hysteresis를 만드는 값이고, 두 값의 차이는 보통 `0.20~0.30m` 정도면 충분해.
            
    
    ---
    
    ```bash
    avoidance_min_clearance_m: 0.60     # FreeSpaceModel's Gap을 실제 회피 path로 쓸 수 있는지의 Planner 기준
    ```
    
    완전히 없애기보다는, 지금 단계에서는 **낮춰서 보조 안전 필터로만 쓰는 것**을 추천해.
    
    이유는 아직 local planner에 “생성된 local path가 occupancy grid와 충돌하는지 검사하는 기능”이 없기 때문이야. 현재 planner는 FreeSpaceModel heading을 받아서 직선 local path를 만들지만, 그 path 중간이 occupied cell과 겹치는지 직접 검사하지 않는다. 그래서 planner 쪽에 약한 최종 필터가 있는 건 아직 의미가 있어.
    
    다만 현재 `0.60`은 FreeSpaceModel의 `min_clearance_block_threshold: 0.40`보다 꽤 높아서, 회피 가능 후보를 과하게 버릴 수 있어.
    
    내 추천은 이거야.
    
    ```
    avoidance_min_clearance_m: 0.45
    ```
    
    또는 조금 보수적으로:
    
    ```
    avoidance_min_clearance_m: 0.50
    ```
    
    즉, FreeSpaceModel 기준 `0.40m`에 planner가 `0.05~0.10m` 정도만 추가 safety margin을 주는 구조가 적당해.
    

---

### 2차 수정:  local planner FSM에서 AVOIDANCE latch 추가 - `local_path_planner_node`

> 
> 
> 
> AVOIDANCE에서 장애물이 잠깐 안 보이거나 clear처럼 보여도 바로 REJOIN으로 넘어가지 말고 일정 시간 또는 일정 거리만큼은 회피 상태를 계속 유지하는 것
> 

AVOIDANCE latch: FSM이 바로 상태 머신을 바꾸면 로봇이 흔들리거나 장애물에 붙는 현상이 발생하므로 **“회피를 시작했으면 최소한 이 정도는 회피 행동을 유지해라.”**의 **회피 행동의 지속성을 보장하는 장치**이다.

- `local_path_planner.param.yaml`
    
    ```bash
        # [추가] AVOIDANCE latch
        # AVOIDANCE 상태를 유지를 위한 Parameter
        # avoidance_min_travel_m ≈ avoidance_horizon_m의 50~100%
        avoidance_min_hold_sec: 1.0         # latch 최소 시간
        avoidance_min_travel_m: 0.50        # latch 최소 이동 거리
    ```
    
- `local_path_planner_node.hpp/cpp`
    
    #### 1. local_path_planner_node.hpp
    
    ```bash
    // ==========================================
    // [추가] AVOIDANCE Latch Helper 함수
    // - AVOIDANCE 진입 시점/위치를 저장한다.
    // - obstacle_clear가 true가 되더라도 최소 시간/거리 조건을 만족하기 전까지
    //   REJOIN으로 전환하지 않도록 한다.
    // - local path 자체를 고정하는 것이 아니라, AVOIDANCE 상태만 유지한다.
    // ==========================================
    void startAvoidanceLatch(
        const LocalizedPoseMsg &pose,
        const rclcpp::Time &now);
    
    void resetAvoidanceLatch();
    
    bool isAvoidanceLatchDone(
        const LocalizedPoseMsg &pose,
    const rclcpp::Time &now) const;
    
    // [추가] AVOIDANCE latch 파라미터
    // - AVOIDANCE에 진입한 뒤 최소 시간 또는 최소 이동거리 조건을 만족하기 전까지
    //   REJOIN 전환을 막기 위한 값이다.
    double avoidance_min_hold_sec_;         // AVOIDANCE 최소 유지 시간 [sec]
    double avoidance_min_travel_m_;         // AVOIDANCE 최소 이동 거리 [m]
    
    // [추가] AVOIDANCE latch state
    // - AVOIDANCE episode가 언제, 어디서 시작되었는지 저장한다.
    // - path를 고정하는 용도가 아니라 REJOIN 허용 조건을 판단하기 위한 상태값이다.
    rclcpp::Time avoidance_start_time_;         // latch 시작 시간
    bool avoidance_latch_active_{false};        // latch 활성화 flag
    double avoidance_start_x_m_{0.0};           // latch 시작 위치 x
    double avoidance_start_y_m_{0.0};           // latch 시작 위치 y
    ```
    
    #### 2. local_path_planner_node.cpp
    
    ```bash
    // AVOIDANCE Latch 시작 시점&위치 저장 함수
    void LocalPathPlannerNode::startAvoidanceLatch(
        const LocalizedPoseMsg &pose,
        const rclcpp::Time &now)
    {
        /*
        AVOIDANCE episode 시작 시점과 시작 위치를 저장한다.
    
        중요:
            - local path를 고정하는 기능이 아니다.
            - AVOIDANCE 상태에서 REJOIN으로 너무 빨리 넘어가지 않도록
            최소 시간/거리 조건을 판단하기 위한 기준값이다.
        */
        avoidance_latch_active_ = true;
        avoidance_start_time_   = now;
        avoidance_start_x_m_    = static_cast<double>(pose.x_m);
        avoidance_start_y_m_    = static_cast<double>(pose.y_m);
    }
    
    // AVOIDANCE Latch 상태 정보(시간, 위치) 초기화 함수
    void LocalPathPlannerNode::resetAvoidanceLatch()
    {
        /*
        AVOIDANCE episode가 끝났다고 판단될 때 latch 상태를 초기화한다.
    
        일반적으로:
            - REJOIN으로 전환될 때
            - GLOBAL_SUB_GOAL로 정상 복귀할 때
            - GLOBAL_GOAL_REACHED가 되었을 때
            - INVALID_INPUT으로 인해 정상 planning이 불가능할 때
        호출할 수 있다.
        */
        avoidance_latch_active_ = false;
        avoidance_start_x_m_    = 0.0;
        avoidance_start_y_m_    = 0.0;
    }
    
    // AVOIDANCE 상태 최소 시간 or 거리 이상 유지했는지 판단 함수
    bool LocalPathPlannerNode::isAvoidanceLatchDone(
        const LocalizedPoseMsg &pose,
        const rclcpp::Time &now) const
    {
        /*
        AVOIDANCE 상태를 최소 시간 또는 최소 거리 이상 유지했는지 판단한다.
    
        - true: obstacle_clear가 true일 때 REJOIN 전환을 허용할 수 있다.
    
        - false: obstacle_clear가 true여도 아직 AVOIDANCE를 유지해야 한다.
        */
        
        if (!avoidance_latch_active_) return true;
    
        // elapsed_sec: AVOIDANCE에 진입한 뒤 현재까지 몇 초가 지났는가?
        const double elapsed_sec = (now - avoidance_start_time_).seconds();
    
        // traveled_m: AVOIDANCE 시작 위치에서 현재 pose까지의 직선 거리
        const double dx = static_cast<double>(pose.x_m) - avoidance_start_x_m_;
        const double dy = static_cast<double>(pose.y_m) - avoidance_start_y_m_;
        const double traveled_m = std::hypot(dx, dy);
    
        return elapsed_sec >= avoidance_min_hold_sec_ ||
            traveled_m >= avoidance_min_travel_m_;
    }
    ```
    
- 각 상태에 대한 latch 구조 정의 및 선언

---

### 3차 수정: obstacle memory grid 추가

> 순간적인 lidar 정보(장애물 정보)를 짧은 시간 동안 기억하게 만드는 것
> 

FreeSpaceModel이 쓰게 만든다. 이 구조는 memory를 다시 현재 로봇 기준 local grid에 반영하는 방식이다.

```
local_occupancy_grid            base_link 기준 현재 LiDAR grid
+
obstacle_memory_grid            mission_map 기준 최근 장애물 memory
+
localization_pose               mission_map 기준 현재 로봇 pose
        ↓
local_occupancy_grid_with_memory base_link 기준 memory 반영 grid
        ↓
FreeSpaceModel
        ↓
LocalPathPlanner
```

이 방식의 장점: 

```
- FreeSpaceModel이 memory 장애물까지 고려해서 gap을 선택함
- LocalPathPlanner 구조를 크게 바꾸지 않아도 됨
- “최근 봤던 장애물 방향”을 free-space 후보에서 제외할 수 있음
```

즉, FreeSpaceModel이 현재 LiDAR만 보는 게 아니라:

```
현재 LiDAR + 최근 기억 장애물
```

을 같이 보고 주행 가능한 방향을 선택하게 된다.

- 설계 구조
    
    ```bash
    memory는 mission_map에 저장한다.
    하지만 FreeSpaceModel이 쓰기 전에는 다시 base_link local grid로 투영한다.
    ```
    
    ```bash
    /scan_3D
      ↓
    pointcloud_preprocess_node
      ↓
    /perception/lidar/points_filtered
      ↓
    local_occupancy_grid_node
      ↓
    /perception/lidar/local_occupancy_grid
      frame_id = base_link
    
    /localization/pose
      frame_id = mission_map
    
    /perception/lidar/local_occupancy_grid
    /localization/pose
      ↓
    obstacle_memory_grid_node
      ↓
    /perception/lidar/obstacle_memory_grid
      frame_id = mission_map
    
    /perception/lidar/local_occupancy_grid
    /perception/lidar/obstacle_memory_grid
    /localization/pose
      ↓
    local_occupancy_grid_memory_fusion_node
      ↓
    /perception/lidar/local_occupancy_grid_with_memory
      frame_id = base_link
    
    /perception/lidar/local_occupancy_grid_with_memory
      ↓
    free_space_model_node
      ↓
    /perception/lidar/free_space_model
      ↓
    local_path_planner_node
    ```
    
    #### 왜 memory를 다시 base_link로 투영해야 하나?
    
    FreeSpaceModel은 현재 base_link 기준 local grid를 angular bin으로 분석하는 구조이기 때문에 FreeSpaceModel 입장에서는 아래의 형식 데이터가 필요하다.
    
    ```
    로봇 기준 전방 몇 도에 occupied가 있는가?
    로봇 기준 왼쪽 몇 도가 free인가?
    로봇 기준 어느 heading이 가장 안전한가?
    ```
    
    그래서 FreeSpaceModel 입력은 `base_link` 기준이어야 자연스럽다. 하지만 obstacle memory는 mission_map 기준으로 저장되어 있기에  아래와 같은 중간 변환 과정이 필요하다.
    
    ```
    mission_map memory cell
    → 현재 robot pose 기준 base_link 좌표로 변환
    → 현재 local occupancy grid에 occupied로 반영
    ```
    
    수식은 반대 변환:
    
    ```
    dx =x_memory_map-robot_x_map;
    dy =y_memory_map-robot_y_map;
    
    x_base =cos(yaw)*dx+sin(yaw)*dy;
    y_base =-sin(yaw)*dx+cos(yaw)*dy;
    ```
    
    이렇게 하면 memory obstacle이 현재 로봇 기준 local grid 안에 다시 들어오게 된다.
    
- 설계 방향성
    
    ### 1단계: mission_map 기준 obstacle_memory_grid_node 구현
    
    ```
    local_occupancy_grid + localization_pose
    → obstacle_memory_grid
    ```
    
    출력:
    
    ```
    /perception/lidar/obstacle_memory_grid
    frame_id = mission_map
    ```
    
    이 단계는 memory를 생성하고 RViz에서 확인하는 단계야.
    
    ---
    
    ### 2단계: memory_fusion_node 구현
    
    ```
    local_occupancy_grid
    				+
    obstacle_memory_grid
    				+
    localization_pose
    → local_occupancy_grid_with_memory
    ```
    
    출력:
    
    ```
    /perception/lidar/local_occupancy_grid_with_memory
    frame_id = base_link
    ```
    
    이 단계에서 memory가 현재 로봇 기준 local grid에 제대로 다시 나타나는지 확인해.
    
    ---
    
    ### 3단계: FreeSpaceModel 입력 변경
    
    ```
    input_topic:"/perception/lidar/local_occupancy_grid_with_memory"
    ```
    
    이렇게 하면 FreeSpaceModel이 memory 장애물까지 고려해서 gap을 선택하게 돼.
    
    ---
    
    ### 4단계: LocalPathPlanner는 그대로 유지
    
    LocalPathPlanner는 기존처럼 FreeSpaceModel 결과를 사용하면 돼.
    
    ```
    FreeSpaceModel이 memory 반영 gap을 선택
    → LocalPathPlanner는 그 heading으로 AVOIDANCE path 생성
    ```
    
    ---
    
    ### mission_map vs odom 최종 선택
    
    현재 프로젝트 상황에서는 이렇게 추천해.
    
    ```
    MVP:
    mission_map 기준 obstacle_memory_grid
    
    이유:
    - 현재 /localization/pose가 mission_map 기준
    - global path도 mission_map 기준
    - 관제 UI와 연결하기 좋음
    - 구현이 단순함
    ```
    
    다만 장기적으로는 이렇게 생각하면 돼.
    
    ```
    짧은 시간의 로컬 회피 memory
    → odom이 더 정석적
    
    관제 지도/임무 지도에 표시되는 memory
    → mission_map이 더 직관적
    ```
    
    현재는 mission_map으로 가도 충분해.
    
- `obstacle_memory_grid_node`
    
    > 현재 local grid의 occupied cell을 mission_map 좌표로 변환해서 일정 시간 기억하는 노드 → mission_map 기준의 “장애물 기억 저장소”
    > 
    
    파일 구조:
    
    ```bash
    include/lidar_perception/obstacle_memory_grid_node.hpp
    src/obstacle_memory_grid_node.cpp
    config/obstacle_memory_grid.param.yaml
    ```
    
    입력:
    
    ```
    /perception/lidar/local_occupancy_grid  // base_link 기준 현재 grid
    /localization/pose                      // mission_map 기준 현재 로봇 pose
    ```
    
    출력:
    
    ```
    /perception/lidar/obstacle_memory_grid  // mission_map 기준 최근 장애물 기억 grid
    ```
    
    동작:
    
    ```
    1. local_occupancy_grid에서 occupied cell만 추출
    2. occupied cell center를 base_link 좌표로 복원
    3. localization pose를 이용해 mission_map 좌표로 변환
    4. memory grid에 occupied로 저장
    5. 각 memory cell에 timestamp/age 저장
    6. TTL 지난 cell 삭제
    7. obstacle_memory_grid publish
    ```
    
    중요한 점:
    
    ```
    occupied만 저장
    free/unknown은 memory에 오래 저장하지 않음
    ```
    
    왜냐하면 free를 오래 저장하면 동적 장애물이 새로 들어왔을 때 위험할 수 있다.
    
    ---
    
    알고리즘:
    
    ```bash
    1. /localization/pose 수신
       - robot_x, robot_y, robot_yaw 저장
    
    2. /perception/lidar/local_occupancy_grid 수신
       - base_link 기준 grid
    
    3. local grid에서 occupied cell만 순회
    
    4. occupied cell의 center 좌표를 base_link 좌표로 복원
    
    5. base_link 좌표를 mission_map 좌표로 변환
    
    6. mission_map 좌표를 memory cell key로 변환
    
    7. 해당 memory cell의 last_seen_time 갱신
    
    8. TTL이 지난 memory cell 삭제
    
    9. 현재 robot 주변 window만 OccupancyGrid로 publish
    ```
    
    - 변환 수식:
        
        ```bash
        x_map = robot_x + cos(yaw) * x_base - sin(yaw) * y_base;
        y_map = robot_y + sin(yaw) * x_base + cos(yaw) * y_base;
        ```
        
- `memory_fusion_node`
    
    > FreeSpaceModel이 base_link 기준의 obstacle memory grid를 볼 수 있게 해주는 노드
    > 
    
    파일 구조:
    
    ```bash
    ~/robot_ws/src/perception/lidar_perception/include/lidar_perception/memory_fusion_node.hpp
    ~/robot_ws/src/perception/lidar_perception/src/memory_fusion_node.cpp
    ~/robot_ws/src/perception/lidar_perception/config/memory_fusion.param.yaml
    ~/robot_ws/src/perception/lidar_perception/launch/lidar_memory_fusion.launch.py
    ```
    
    입력:
    
    ```
    /perception/lidar/local_occupancy_grid       // base_link 기준 현재 grid
    /perception/lidar/obstacle_memory_grid       // mission_map 기준 memory grid
    /localization/pose                           // mission_map 기준 현재 로봇 pose
    ```
    
    출력:
    
    ```
    /perception/lidar/local_occupancy_grid_with_memory  // base_link 기준 fused grid
    ```
    
    동작:
    
    ```
    1. 현재 local_occupancy_grid를 복사
    2. mission_map 기준 obstacle_memory_grid의 occupied cell을 순회
    3. 현재 robot pose 기준 base_link 좌표로 변환
    4. local grid 범위 안에 들어오는 memory obstacle만 occupied로 overlay
    5. local_occupancy_grid_with_memory publish
    ```
    
    이렇게 하면 FreeSpaceModel은 기존과 거의 똑같이 동작할 수 있다.
    
    ```
    free_space_model_node:
      ros__parameters:
        input_topic:"/perception/lidar/local_occupancy_grid_with_memory"
    ```
    

근데 지금 드는 생각이 obstacle memory grid는 local path planner node에서 하는 것이 아니라 lidar_perception 패키지의 local occupancy grid node에서 추가 구현하는 것인가? 그래서 드는 생각은 결국 실시간 slam과 유사한 거 같거든?