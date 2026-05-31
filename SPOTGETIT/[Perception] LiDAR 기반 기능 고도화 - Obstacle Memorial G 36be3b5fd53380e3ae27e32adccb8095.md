# [Perception] LiDAR 기반 기능 고도화 - Obstacle Memorial Grid

상태: Perception

[Camera & Lidar Fusion](Camera%20&%20Lidar%20Fusion%2036be3b5fd53380499f39dbeb6bcccc1a.md)

## 1. Obstacle Memory Grid

> “현재 프레임에서만 보이는 local occupancy grid를 일정 시간 동안 기억 가능한 mission_map 기준 obstacle memory로 바꾸는 노드
> 

### 1-1. 역할

현재 `local_occupancy_grid_node`는 매 순간 LiDAR point를 받아서 `base_link` 기준 local grid를 만든다.

```
/perception/lidar/points_filtered
        ↓
local_occupancy_grid_node
        ↓
/perception/lidar/local_occupancy_grid
```

그런데 이 grid는 **현재 LiDAR frame에서 보이는 정보만 표현**한다.

즉, 장애물이 지금 LiDAR에 보이면 occupied가 되고, **1) 다음 순간 LiDAR FOV 밖으로 빠지거나** **2) 순간 미검출**되면 local grid에서 사라지는 현상이 나타남. 

`obstacle_memory_grid_node`는 **일정 시간 동안의 장애물(obstacle)을 기억해** 문제를 보완하기 위한 노드이다.

```
/perception/lidar/local_occupancy_grid
/localization/pose
        ↓
obstacle_memory_grid_node
        ↓
/perception/lidar/obstacle_memory_grid
```

핵심 역할:

```
base_link 기준 현재 장애물 cell
→ mission_map 기준 좌표로 변환
→ 일정 시간 TTL 동안 기억
→ mission_map 기준 obstacle memory grid로 발행
```

*TTL

---

### 1-2. 파이프라인 시각화

#### 1️⃣ 전체 perception 흐름에서 위치

```bash
LiDAR raw data
    ↓
pointcloud_preprocess_node
    ↓
/perception/lidar/points_filtered
    ↓
local_occupancy_grid_node
    ↓
/perception/lidar/local_occupancy_grid
    frame_id = base_link
    현재 순간의 장애물 grid
    ↓
obstacle_memory_grid_node
    + /localization/pose
    ↓
/perception/lidar/obstacle_memory_grid
    frame_id = mission_map
    최근 장애물 기억 grid
```

#### 2️⃣ obstacle_memory_grid_node 내부 흐름

```bash
[1] /localization/pose 수신
    - robot_x, robot_y, robot_yaw 저장

[2] /perception/lidar/local_occupancy_grid 수신
    - base_link 기준 OccupancyGrid
    - occupied cell 추출

[3] occupied cell center 계산
    grid index gx, gy
	        ↓
    x_base, y_base

**[4] base_link → mission_map 변환**
    x_map = robot_x + cos(yaw) * x_base - sin(yaw) * y_base
    y_map = robot_y + sin(yaw) * x_base + cos(yaw) * y_base

**[5] mission_map 좌표를 memory cell key로 변환**
    key.x = floor(x_map / memory_resolution)
    key.y = floor(y_map / memory_resolution)

[6] memory_cells_에 저장
    key → last_seen_time

[7] TTL 지난 cell 삭제

[8] memory_cells_를 OccupancyGrid로 변환

[9] /perception/lidar/obstacle_memory_grid 발행
```

---

### 1-3. 핵심 기능

→