# [Perception] LiDAR 기능 정의서

상태: Perception

## 1. 역할 분리

```bnf
[드라이버 계층]
- cyglidar_d1_ros2
- 역할: LiDAR raw publish

[가공 계층]
- lidar_preprocess_node
- obstacle_extractor_node
- free_space_estimator_node
- local_costmap_node
- 역할 : raw를 시스템용 정보로 변환

[활용 계층]
- local_planner
- collision_avoidance
- localization_adapter
- digital_twin_bridge
- fleet_status_reporter
```

즉, **CygLiDAR 패키지**를 직접 시스템 핵심 로직으로 쓰는 것이 아니라, 그 위에 시스템 노드를 얹는 구조

- LiDAR 스펙
    
    ![image.png](image%2021.png)
    
- 모델별 연결 구조
    
    ```bnf
    [CygLiDAR D1 Driver]
      - /scan
      - /scan_3D
            │
            ▼
    [공통 전처리]
      - 유효 거리만 남기기
      - inf / nan 제거
      - 노이즈 제거
      - Spot 기준 좌표계 정리
            │
            ├───────────────────────────────┬───────────────────────────────┬───────────────────────────────┐
            ▼                               ▼                               ▼
    [ObstacleModel branch]          [FreeSpaceModel branch]        [LocalizationHint branch]
      "장애물이 있나?"                 "그래서 지나갈 수 있나?"            "주변 구조가 어떤가?"
            │                               │                               │
            │                               │                               │
            │                               │                               ├─ left_wall_distance
            │                               │                               ├─ right_wall_distance
            │                               │                               ├─ front_wall_distance
            │                               │                               ├─ wall_alignment_score
            │                               │                               └─ corner_detected
            │                               │
            │                               ├─ front_clearance
            │                               ├─ left_clearance
            │                               ├─ right_clearance
            │                               ├─ front_blocked
            │                               ├─ left_blocked
            │                               ├─ right_blocked
            │                               ├─ blocked_state
            │                               └─ free_direction_candidates
            │
            ├─ front_detected
            ├─ left_detected
            ├─ right_detected
            ├─ front_min_distance
            ├─ left_min_distance
            ├─ right_min_distance
            ├─ front_nearest_angle
            ├─ left_nearest_angle
            ├─ right_nearest_angle
            ├─ obstacle_detected
            └─ emergency_stop_required
    ```
    
    ```bnf
    [CygLiDAR D1 Driver]
      - /scan
      - /scan_3D
            │
            ▼
    [공통 전처리]
            │
            ▼
    [Local Space Builder]
      - local_occupancy_grid
      - local_obstacle_points
      - free_space_region(선택)
            │
            ├─ Planning / Local Path Generator가 사용
            ├─ Digital Twin 시각화가 사용
            └─ 필요하면 FreeSpaceModel 요약값 계산에 사용
    ```
    

---

## 2. 계층 상세화

#### 2-1. 드라이버 계층

> CygLiDAR D1의 raw 데이터를 ROS2로 꺼내오는 역할
> 

```bnf
**- 패키지명 : cyglidar_d1_ros2
- Publish Topic : /scan, /scan_2D, /scan_3D, /scan_image**
```

- `/scan`**: 2D 거리 배열**
- `/scan_2D`**: 2D 점 형태**
- `/scan_3D`**: 3D 포인트클라우드**
- `/scan_image`**: 거리값을 이미지화**

#### 2-2. 환경 모델 계층 - Perception 본질

> Raw 데이터를 **의미 있는 주행 정보**로 바꾸는 역할
> 
- **LiDAR 후처리 데이터 필요한 리스트**
    
    **A. 안전/근거리 인지 계열**
    
    - `front_min_range`
    - `left_min_range`
    - `right_min_range`
    - `sector_ranges`
    - `obstacle_detected`
    - `emergency_stop_required`
    - `collision_risk_level`
    
    **B. 장애물 기하 정보 계열**
    
    - `local_obstacle_points`
    - `obstacle_clusters`
    - `nearest_obstacle_pose`
    - `obstacle_density`
    - `obstacle_bearing_distribution`
    
    **C. 공간 표현 계열**
    
    - `local_occupancy_grid`
    - `local_costmap`
    - `free_space_region`
    - `free_space_polygon`
    - `inflated_obstacle_map`
    
    **D. 주행 가능성 / 통로 해석 계열**
    
    - `gap_candidates`
    - `corridor_width_estimate`
    - `blocked_state`
    - `traversable_state`
    - `forward_path_clearance`
    - `local_goal_direction_hint`
    
    **E. 3D 활용 계열**
    
    - `ground_filtered_cloud`
    - `elevated_obstacle_points`
    - `low_obstacle_points`
    - `structure_features`
    - `height_profile_map`
    
    **F. 상위 시스템 공유용 요약 상태**
    
    - `local_path_feasible_hint`
    - `path_blocked_reason`
    - `replan_required_hint`
    - `local_map_patch`
    - `robot_local_risk_state`
    

#### 1️⃣ **obstacle_model - 충돌 방지용**

> 
> 
> 
> **Spot이 현재 바라보는 방향 기준**으로 LiDAR FOV를 전방/좌측/우측 섹터로 나누고, 각 섹터에서 탐지된 장애물 후보 중 가장 가까운 장애물의 정보(거리, 각도)와 전체 장애물 탐지 여부, 정지 필요 여부를 제공하는 모델
> 
- 역할 : **“지금 당장 부딪힐 위험이 있는가?”**를 빠르게 알려주는 것
- 소비 주체:  Safety Supervisor / Planning
- **Input**: 전처리된 LiDAR 데이터(`local_obstacle_points`)
- **Output**: 섹터별 **장애물 탐지 여부/장애물 거리/장애물 각도**, **전체 장애물 탐지 여부, 긴급 정지 필요 여부**

```bnf
front_distance             float32   전방 장애물까지 거리 (m)
left_distance              float32   좌측 장애물까지 거리 (m)
right_distance             float32   우측 장애물까지 거리 (m)
obstacle_detected          bool      장애물 감지 여부
obstacle_direction         float32   장애물 방향 (rad)
```

- **ObstacleModel.msg**
    
    Topic : `/perception/lidar/obstacle_model` → `custom_msgs/ObstacleModel`
    
    ```bnf
    std_msgs/Header header
    
    bool front_detected             # 전방 장애물 감지 여부
    bool left_detected              # 좌측 장애물 감지 여부
    bool right_detected             # 우측 장애물 감지 여부
    
    float32 front_min_distance      # 전방 장애물까지 거리 (m)
    float32 left_min_distance       # 좌측 장애물까지 거리 (m)
    float32 right_min_distance      # 우측 장애물까지 거리 (m)
    
    float32 front_nearest_angle     # 전방 장애물 방향 (rad)
    float32 left_nearest_angle      # 좌측 장애물 방향 (rad)
    float32 right_nearest_angle     # 우측 장애물 방향 (rad)
    
    bool obstacle_detected          # 빠른 장애물 감지 판단 여부 for Decision
    bool emergency_stop_required    # 즉시 정지 필요 여부
    ```
    

#### 2️⃣ **free_space_model - local path planning 입력용**

> FreeSpaceModel은 Spot 현재 진행 방향 기준으로, **주변 공간 중 어디가 주행 가능**하고 **어디가 막혀 있는지**를 요약해서 Planning에 전달하는 모델
> 
- 역할 : **“어디가 비어 있고, 어디가 막혀 있는가?”**를 표현하는 모델
- 소비 주체 : Local Path Generator / Fleet status / Digital Twin 시각화 노드
- **Input** : 전처리된 LiDAR 데이터(`local_obstacle_points`, `local_occupancy_grid`)
- **Output** : 섹터별 **주행 가능 여유 공간(clearance), 막힘 여부(blocked), 주행 가능 방향 후보**

```bnf
front_clearance          float32   전방 주행 가능 여유 공간 (m)
left_clearance           float32   좌측 주행 가능 여유 공간 (m)
right_clearance          float32   우측 주행 가능 여유 공간 (m)
blocked_state            bool      전방/진행 방향 막힘 여부
local_obstacle_points              로봇 기준 좌표계의 장애물 점 집합
local_occupancy_grid               로컬 점유 맵
free_direction_candidates          주행 가능 방향 후보
```

- **FreeSpaceModel.msg**
    
    Topic : `/perception/lidar/free_space_model` → `custom_msgs/FreeSpaceModel`
    
    ```bnf
    std_msgs/Header header
    
    float32 front_clearance             # 전방 주행 가능 여유 공간 (m)
    float32 left_clearance              # 좌측 주행 가능 여유 공간 (m) 
    float32 right_clearance             # 우측 주행 가능 여유 공간 (m)
    
    bool front_blocked                  # 전방 방향 막힘 여부
    bool left_blocked                   # 좌측 방향 막힘 여부
    bool right_blocked                  # 우측 방향 막힘 여부
    
    bool blocked_state                  # 빠른 진행 방향 막힘 여부 for Decision
    
    uint8[] free_direction_candidates   # 주행 가능한 방향 후보들
    ```
    
    - *_clearance : “그 방향으로 얼마나 갈 수 있느냐”를 나타내는 **대표 거리값**
    - *_blocked : “로봇 폭 기준으로 지나갈 수 있는 상태인지”를 나타내는 **대표 상태값**
    - free_direction_candidates : 최적 방향 X, 갈 수 있는 방향 후보들 산출
        
        예시 :
        
        ```bnf
        [1] → 전방만 가능
        [0,1] → 좌측, 전방 가능
        [0,2] → 좌/우 가능, 전방 불가
        ```
        
    
    ---
    
- **`/perception/lidar/local_obstacle_points` - 별도 표준 topic**
    
    > 전처리된 LiDAR 데이터 중 “장애물로 판단된 점들”의 집합 (점 집합)
    > 
    
    메시지 타입:
    
    ```
    sensor_msgs/PointCloud2
    ```
    
    역할:
    
    - planner가 장애물 분포를 볼 때 사용
    - 디지털트윈 시각화에 사용
    
    ---
    
- **`/perception/lidar/local_occupancy_grid` - 별도 표준 topic**
    
    > Spot 주변 전체 공간을 작은 칸으로 나눠서, 각 칸이 비었는지/막혔는지/모르는지 나타낸 로컬 맵 (격자 지도) → **Spot 주변 전체 공간**을 표현
    > 
    
    메시지 타입:
    
    ```
    nav_msgs/OccupancyGrid
    ```
    
    역할:
    
    - local planner 입력
    - 주변 free/occupied 공간 표현
    - 관제/시각화 활용

**확장**

```bnf
traversable_state         bool       현재 방향 주행 가능 여부
gap_candidates                       통과 가능한 gap 후보
corridor_width_estimate   float32    통로 폭 추정 (m)
```

**Decision 계층에서 처리할 것**

```bnf
best_escape_direction      float32    최적 회피 방향 (rad)
escape_confidence          float32    회피 방향 신뢰도 (0~1)
```

#### 3️⃣ **localization_hint - Localization의 지도 기반 Pose 보정용**

> LocalizationHint는 LiDAR 기반 로컬 공간 표현으로부터 Spot 주변의 구조적 특징(좌/우/전방 벽 거리, 벽 정렬 정도, 코너 여부)을 추출해 Localization에 전달하는 보조 모델
> 
- 역할 : **“현재 주변 구조가 어떤 형태인지”**를 localization에 보조 정보를 제공하는 모델
- 소비 주체: Localization EKF [F]
- **Input**: 전처리된 LiDAR 데이터(`local_obstacle_points`, 필요 시 `local_occupancy_grid`)
- **Output**: 현재 Spot 주변 구조를 나타내는 **벽 거리, 벽 정렬 정도, 코너 감지 여부**

```bnf
left_wall_distance    float32   좌측 벽까지 거리 (m)
right_wall_distance   float32   우측 벽까지 거리 (m)
front_wall_distance   float32   전방 벽까지 거리 (m)
wall_alignment_score  float32   벽 정렬 신뢰도 (0~1)
corner_detected       bool      코너 감지 여부
```

- **LocalizationHint.msg**
    
    Topic : `/perception/lidar/localization_hint` → `custom_msgs/LocalizationHint`
    
    ```bnf
    std_msgs/Header header
    
    float32 left_wall_distance          # 좌측 벽까지 거리 (m)
    float32 right_wall_distance         # 우측 벽까지 거리 (m)
    float32 front_wall_distance         # 전방 벽까지 거리 (m)
    
    float32 wall_alignment_score       
    float32 left_wall_alignment_score   # 좌측 벽 정렬 신뢰도 (0~1)
    float32 right_wall_alignment_score  # 우측 벽 정렬 신뢰도 (0~1)
    
    bool corner_detected                # 코너 감지 여부
    ```
    

**확장**

```bnf
corridor_detected          bool      복도 구조 감지 여부
left_wall_angle            float32   좌측 벽 방향 (rad)
right_wall_angle           float32   우측 벽 방향 (rad)
```

**Localization 계층에서 처리할 것**

```bnf
map_match_confidence       float32   지도 매칭 신뢰도 (0~1)
```

#### 4️⃣ **victim_detection**

> 역할: 실종자 발견 이벤트 발행
> 
> 
> 소비 주체: Mission Executor [C]
> 

```bnf
victim_detected            bool      실종자 감지 여부
confidence                 float32   탐지 신뢰도 (0~1)
position_x                 float32   탐지 위치 x (mission_map 기준, m)
position_y                 float32   탐지 위치 y (mission_map 기준, m)
bbox_x                     float32   이미지 내 bounding box x
bbox_y                     float32   이미지 내 bounding box y
bbox_w                     float32   bounding box 너비
bbox_h                     float32   bounding box 높이
estimated_position_x       float32   추정 위치 x (map/frame 기준)
estimated_position_y       float32   추정 위치 y (map/frame 기준)
```

**데이터 소스 정리**

```bnf
obstacle_model      ← LiDAR (CygLiDAR D1)
free_space_model    ← LiDAR (CygLiDAR D1)
localization_hint   ← LiDAR (CygLiDAR D1)
victim_detection    ← Camera + YOLOv8 + 좌표변환
```