# [Perception] launch 정리

상태: Perception

## 1. Lidar(lidar_perception 패키지)

```bash
launch/
├── lidar_preprocess.launch.py
│   └── 공통 입력 계층
│       - CygLiDAR driver
│       - static TF
│       - pointcloud_preprocess_node
│       - obstacle_cluster_node
│
├── lidar_obstacle_model.launch.py
│   └── ObstacleModel만 실행
│       - obstacle_model_node
│
├── lidar_free_space_model.launch.py
│   └── FreeSpaceModel 계열만 실행
│       - local_occupancy_grid_node
│       - free_space_model_node
│
└── lidar_environment_model.launch.py
    └── 전체 통합 실행
        - CygLiDAR driver
        - static TF
        - pointcloud_preprocess_node
        - obstacle_cluster_node
        - obstacle_model_node
        - local_occupancy_grid_node
        - free_space_model_node
```

### `lidar_preprocess.launch.py`

> CygLiDAR D1 raw PointCloud2(/scan_3D)를 받아서 base_link 기준 전처리 point cloud(/perception/lidar/points_filtered)를 생성하는 전처리 단독 테스트용 launch 파일.
> 

명령어 : `ros2 launch lidar_perception lidar_preprocess.launch.py`

실행 노드 :

```bash
- cyglidar_d1_ros2 driver
- base_link -> laser_frame static TF
- pointcloud_preprocess_node
- obstacle_cluster_node.cpp
```

주요 출력 토픽 :

```bash
/scan_3D
/perception/lidar/points_filtered
/perception/lidar/obstacle_clusters
/perception/lidar/clustered_points_colored
```

구조 :

```bash
CygLiDAR driver
  ↓
/scan_3D
  ↓
pointcloud_preprocess_node
  ↓
/perception/lidar/points_filtered
  ↓
obstacle_cluster_node
  ├─ /perception/lidar/obstacle_clusters
  └─ /perception/lidar/clustered_points_colored
```

### `obstacle_model.launch.py`

> LiDAR 기반 장애물 클러스터링과 ObstacleModel 생성을 단독으로 테스트한다.
> 

명령어 : `ros2 launch lidar_perception obstacle_model.launch.py`

실행 노드 :

```bash
1. obstacle_model_node
```

주요 출력 토픽 :

```bash
/perception/lidar/obstacle_model
```

구조 :

```bash
CygLiDAR driver
  ↓
pointcloud_preprocess_node
  ↓
/points_filtered
  ↓
obstacle_cluster_node
  ├─ /perception/lidar/obstacle_clusters
  └─ /perception/lidar/clustered_points_colored
        ↓
obstacle_model_node
        ↓
/perception/lidar/obstacle_model
```

### `free_space_model.launch.py`

> Local OccupancyGrid와 FreeSpaceModel 생성을 단독으로 테스트한다.
> 

명령어 : `ros2 launch lidar_perception free_space_model.launch.py`

실행 노드 :

```bash
1. local_occupancy_grid_node
2. free_space_model_node
```

주요 출력 토픽 :

```
/perception/lidar/local_occupancy_grid
/perception/lidar/free_space_model
```

구조:

```
/points_filtered
  ↓
local_occupancy_grid_node
  ↓
/perception/lidar/local_occupancy_grid
  ↓
free_space_model_node
  ↓
/perception/lidar/free_space_model
```