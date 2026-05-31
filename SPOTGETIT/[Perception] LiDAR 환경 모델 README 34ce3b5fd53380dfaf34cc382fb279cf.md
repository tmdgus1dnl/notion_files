# [Perception] LiDAR 환경 모델 README

상태: Perception

## 1. Obstacle Model

### 상세내용

# Obstacle Model Pipeline README

## 1. 개요

이 문서는 CygLiDAR D1의 `/scan_3D` PointCloud를 입력으로 받아, Spot 기준(`base_link`)에서 전처리 → 3D 클러스터링 → 대표 장애물 요약까지 수행하는 **Obstacle Model Pipeline**의 구조와 실행 방법을 정리한다.

본 파이프라인의 목표는 다음과 같다.

- LiDAR 3D PointCloud를 Spot 기준 좌표계로 해석
- 불필요한 점들을 제거하고 연산량을 줄이기 위한 전처리 수행
- 전처리된 점군을 물체 단위 클러스터로 분리
- 각 클러스터의 대표 거리 / 방향 / sector(front, left, right) 계산
- 최종적으로 planning / decision이 사용하기 쉬운 `ObstacleModel` 형태로 요약

---

## 2. 전체 파이프라인 구조

```
/scan_3D (sensor_msgs/msg/PointCloud2, frame_id=laser_frame)
    ↓
pointcloud_preprocess_node
    ↓
/perception/lidar/points_filtered
    ↓
obstacle_cluster_node
    ↓
/perception/lidar/obstacle_clusters
/perception/lidar/clustered_points_colored
    ↓
obstacle_model_node
    ↓
/perception/lidar/obstacle_model
```

### 좌표계 관계

현재 파이프라인은 아래 TF 관계를 기준으로 동작한다.

```
base_link -> laser_frame
```

- `base_link`: Spot 몸체 기준 좌표계
- `laser_frame`: LiDAR 센서 기준 좌표계

즉, 원본 `/scan_3D`는 `laser_frame` 기준으로 들어오고, 전처리 노드에서 `base_link` 기준으로 변환한 뒤 이후 단계가 모두 Spot 기준으로 해석된다.

---

## 3. 사용 메시지

### 3.1 `robot_interfaces/msg/ObstacleCluster.msg`

개별 3D 장애물 클러스터 1개의 정보를 표현한다.

주요 필드:

- `cluster_id`
- `point_count`
- `centroid_x/y/z`
- `nearest_x/y/z`
- `centroid_distance_xy`
- `nearest_distance_xy`
- `azimuth_angle_rad`
- `elevation_angle_rad`
- `bbox_size_x/y/z`
- `sector`

### 3.2 `robot_interfaces/msg/ObstacleClusters.msg`

현재 프레임의 전체 장애물 클러스터 목록을 표현한다.

주요 필드:

- `header`
- `ObstacleCluster[] clusters`

### 3.3 `robot_interfaces/msg/ObstacleModel.msg`

planning / decision이 바로 사용할 수 있도록 front / left / right 대표 장애물만 요약한 메시지이다.

주요 필드:

- `header`
- `obstacle_detected`
- `front`
- `left`
- `right`

---

## 4. 노드별 설명

## 4.1 `pointcloud_preprocess_node`

### 역할

원본 `/scan_3D` PointCloud를 받아 Spot 기준 전처리 결과를 생성한다.

### 입력

- `/scan_3D` (`sensor_msgs/msg/PointCloud2`)

### 출력

- `/perception/lidar/points_filtered` (`sensor_msgs/msg/PointCloud2`)

### 주요 처리

1. `laser_frame` → `base_link` TF 변환
2. ROI crop
    - x, y, z 범위 밖의 점 제거
3. voxel downsampling
    - 연산량 감소
4. 전처리된 PointCloud publish

### 핵심 파라미터

- `target_frame`
- `roi_min_x`, `roi_max_x`
- `roi_min_y`, `roi_max_y`
- `roi_min_z`, `roi_max_z`
- `voxel_leaf_size`

### 핵심 코드 포인트

- `tf2::doTransform()`을 사용해 `base_link` 기준으로 점군 변환
- `pcl::PassThrough`로 ROI crop
- `pcl::VoxelGrid`로 downsampling

---

## 4.2 `obstacle_cluster_node`

### 역할

전처리된 점군을 3D 클러스터로 분리하고, 각 클러스터의 descriptor와 시각화용 colored point cloud를 생성한다.

### 입력

- `/perception/lidar/points_filtered` (`sensor_msgs/msg/PointCloud2`)

### 출력

- `/perception/lidar/obstacle_clusters` (`robot_interfaces/msg/ObstacleClusters`)
- `/perception/lidar/clustered_points_colored` (`sensor_msgs/msg/PointCloud2`)

### 주요 처리

1. PointCloud2 → PCL 변환
2. Euclidean clustering 수행
3. 각 클러스터에 대해 descriptor 계산
    - centroid
    - nearest point
    - distance
    - azimuth / elevation
    - bbox size
    - sector
4. cluster id별 색상을 입힌 PointCloud2 생성
5. 결과 publish

### 핵심 파라미터

- `cluster_tolerance`
- `min_cluster_size`
- `max_cluster_size`
- `front_min_deg`, `front_max_deg`
- `left_min_deg`, `left_max_deg`
- `right_min_deg`, `right_max_deg`

### 핵심 코드 포인트

- `pcl::EuclideanClusterExtraction` 사용
- `nearest_distance_xy` 기준 정렬 후 색상 할당
- `pcl::PointXYZRGB`를 이용한 RViz 시각화용 colored point cloud 생성

### 시각화 의도

`/perception/lidar/clustered_points_colored`는 각 프레임의 클러스터들을 색상별로 구분해서 RViz에서 확인하기 위한 토픽이다.

> 주의: 현재 cluster id는 persistent object id가 아니라 **프레임별 정렬 결과에 따른 임시 id**이므로, 같은 물체라도 프레임 간 색상이 바뀔 수 있다.
> 

---

## 4.3 `obstacle_model_node`

### 역할

전체 클러스터 목록 중 front / left / right sector별 대표 클러스터를 선택해 `ObstacleModel`로 요약한다.

### 입력

- `/perception/lidar/obstacle_clusters` (`robot_interfaces/msg/ObstacleClusters`)

### 출력

- `/perception/lidar/obstacle_model` (`robot_interfaces/msg/ObstacleModel`)

### 주요 처리

1. 각 sector(front, left, right)별 클러스터 필터링
2. sector별 대표 클러스터 선택
    - 기준: `nearest_distance_xy`가 가장 작은 클러스터
3. 대표 클러스터를 `ObstacleModel`에 채움
4. `obstacle_detected` 계산

### 핵심 코드 포인트

- `selectRepresentativeCluster()` 함수로 sector별 대표 클러스터 선택
- azimuth는 rad와 deg 로그를 함께 출력해 디버깅 편의성 확보

### 현재 범위

- MVP 기준으로는 **긴급 정지 판단은 제외**
- `ObstacleModel`은 obstacle 상황 요약까지만 수행
- 이후 decision / control 노드가 실제 회피/정지 전략을 판단하는 구조를 권장

---

## 5. 토픽 정리

### 5.1 입력 토픽

- `/scan_3D`
    - 타입: `sensor_msgs/msg/PointCloud2`
    - 설명: CygLiDAR D1의 원본 3D point cloud
    - frame: `laser_frame`

### 5.2 전처리 결과 토픽

- `/perception/lidar/points_filtered`
    - 타입: `sensor_msgs/msg/PointCloud2`
    - 설명: `base_link` 기준 ROI + voxel 전처리 결과

### 5.3 클러스터 결과 토픽

- `/perception/lidar/obstacle_clusters`
    - 타입: `robot_interfaces/msg/ObstacleClusters`
    - 설명: 각 클러스터의 descriptor 정보
- `/perception/lidar/clustered_points_colored`
    - 타입: `sensor_msgs/msg/PointCloud2`
    - 설명: cluster id별 색상 입힌 시각화용 point cloud

### 5.4 최종 모델 토픽

- `/perception/lidar/obstacle_model`
    - 타입: `robot_interfaces/msg/ObstacleModel`
    - 설명: front / left / right 대표 장애물 요약 결과

### 5.5 TF 토픽

- `/tf_static`
    - 설명: `base_link -> laser_frame` static TF

---

## 6. 파라미터 정리

## 6.1 `pointcloud_preprocess.param.yaml`

```yaml
pointcloud_preprocess_node:
  ros__parameters:
    target_frame: "base_link"

    roi_min_x: 0.0
    roi_max_x: 3.0

    roi_min_y: -2.0
    roi_max_y: 2.0

    roi_min_z: -0.2
    roi_max_z: 1.5

    voxel_leaf_size: 0.05
```

## 6.2 `obstacle_cluster.param.yaml`

```yaml
obstacle_cluster_node:
  ros__parameters:
    cluster_tolerance: 0.12
    min_cluster_size: 20
    max_cluster_size: 5000

    front_min_deg: -20.0
    front_max_deg: 20.0

    left_min_deg: 20.0
    left_max_deg: 90.0

    right_min_deg: -90.0
    right_max_deg: -20.0
```

> `obstacle_model_node`는 현재 별도의 튜닝 파라미터가 없으므로 yaml 없이 실행한다.
> 

---

## 7. 실행 방법

## 7.1 개별 실행

### 1) CygLiDAR 드라이버 실행

```bash
ros2 launch cyglidar_d1_ros2 cyglidar.launch.py
```

### 2) 전처리 노드 실행

```bash
ros2 launch lidar_perception lidar_preprocess.launch.py
```

### 3) 클러스터 노드 실행

```bash
ros2 run lidar_perception obstacle_cluster_node --ros-args --params-file ~/robot_ws/src/perception/lidar_perception/config/obstacle_cluster.param.yaml
```

### 4) 모델 노드 실행

```bash
ros2 run lidar_perception obstacle_model_node
```

---

## 7.2 전체 파이프라인 실행

### 통합 launch 실행

```bash
ros2 launch lidar_perception lidar_pipeline.launch.py
```

### 통합 launch 구성 요소

- CygLiDAR 드라이버 포함
- `base_link -> laser_frame` static TF
- `pointcloud_preprocess_node`
- `obstacle_cluster_node`
- `obstacle_model_node`

---

## 8. 빌드 방법

### 8.1 `robot_interfaces` 빌드

메시지 수정 후에는 먼저 인터페이스 패키지를 빌드해야 한다.

```bash
cd ~/robot_ws
rm -rf build/robot_interfaces install/robot_interfaces log/latest_build/robot_interfaces 2>/dev/null
colcon build --symlink-install --packages-select robot_interfaces
source ~/robot_ws/install/setup.bash
```

### 8.2 `lidar_perception` 빌드

```bash
cd ~/robot_ws
rm -rf build/lidar_perception install/lidar_perception log/latest_build/lidar_perception 2>/dev/null
colcon build --symlink-install --packages-select lidar_perception
source ~/robot_ws/install/setup.bash
```

### 8.3 워크스페이스 전체 빌드

```bash
cd ~/robot_ws
rm -rf build install log
colcon build --symlink-install
source ~/robot_ws/install/setup.bash
```

---

## 9. 검증 방법

## 9.1 토픽 확인

```bash
ros2 topic list | grep perception
```

기대되는 토픽 예시:

- `/perception/lidar/points_filtered`
- `/perception/lidar/obstacle_clusters`
- `/perception/lidar/clustered_points_colored`
- `/perception/lidar/obstacle_model`

## 9.2 모델 출력 확인

```bash
ros2 topic echo /perception/lidar/obstacle_model --once
```

## 9.3 클러스터 출력 확인

```bash
ros2 topic echo /perception/lidar/obstacle_clusters --once
```

## 9.4 colored point cloud 확인

RViz에서 아래 토픽을 `PointCloud2`로 추가:

- `/perception/lidar/clustered_points_colored`

설정:

- Fixed Frame = `base_link`
- Color Transformer = `RGB8`

## 9.5 filtered point cloud 확인

RViz에서 아래 토픽을 `PointCloud2`로 추가:

- `/perception/lidar/points_filtered`

---

## 10. RViz 실행

### RViz 실행

```bash
rviz2
```

### 저장된 config로 실행

```bash
rviz2 -d /home/jetson/.rviz2/lidar_preprocess.rviz
```

### 권장 RViz 설정

- Fixed Frame: `base_link`
- PointCloud2 Topic 1: `/perception/lidar/points_filtered`
- PointCloud2 Topic 2: `/perception/lidar/clustered_points_colored`

---

## 11. 현재 MVP 범위와 한계

### 완료된 MVP 범위

- LiDAR 3D point cloud 수신
- Spot 기준 좌표계 변환
- ROI + voxel 기반 전처리
- 3D Euclidean clustering
- cluster descriptor 계산
- cluster별 colored point cloud 시각화
- sector(front/left/right) 기반 대표 장애물 요약

### 아직 제외된 항목

- persistent object id / cluster tracking
- ground removal 전용 알고리즘
- decision / control 연동
- marker 기반 bbox / centroid 시각화 고도화

### 해석 시 주의사항

현재 `cluster_id`와 RViz 색상은 **프레임 간 고정된 물체 identity를 의미하지 않는다.**

현재 구조에서는 프레임마다 다시 clustering이 수행되고, 대표 거리 기준 정렬에 따라 id와 색상이 다시 부여된다.

---

## 12. 향후 개선 방향

1. cluster tracking 추가
    - centroid nearest matching
    - persistent id 부여
2. ground removal 추가 여부 재검토
3. marker 기반 bbox / centroid 시각화
4. `ObstacleModel` 기반 decision / control 노드 연동
5. 공통좌표계(`map -> base_link -> laser_frame`) 기반 확장

---

## 13. 핵심 요약

본 Obstacle Model Pipeline은 CygLiDAR D1의 3D PointCloud를 Spot 기준으로 해석하고, 전처리된 점군을 물체 단위 클러스터로 분리한 뒤, front / left / right 대표 장애물로 요약해 planning / decision에 전달하기 위한 LiDAR 인지 파이프라인이다. 현재 버전은 cluster tracking 제외 기준의 MVP로, 실제 환경에서 장애물 군집 형상과 sector 요약 결과를 시각적으로 검증할 수 있는 수준까지 구현되었다.

## 2. Local_occupancy_grid & Free Space model

### 상세내용

## LiDAR Local Occupancy Grid & FreeSpaceModel Pipeline

### 1. 개요

> 본 문서는 `lidar_perception` 패키지 내에서 구현한 `local_occupancy_grid_node`와 `free_space_model_node`의 역할, 입력/출력 토픽, 실행 순서, 파라미터, RViz 시각화 방법을 정리한다.

해당 파이프라인의 목적은 CygLiDAR D1에서 취득한 3D LiDAR PointCloud를 기반으로, Spot 기준 주변 공간을 2D OccupancyGrid로 변환하고, 이를 다시 전방/좌측/우측 sector별 주행 가능성 판단 정보로 요약하는 것이다.

전체 흐름은 다음과 같다.
> 

```markdown
CygLiDAR D1
  ↓
/scan_3D
  ↓
pointcloud_preprocess_node
  ↓
/perception/lidar/points_filtered
  ↓
local_occupancy_grid_node
  ↓
/perception/lidar/local_occupancy_grid
  ↓
free_space_model_node
  ↓
/perception/lidar/free_space_model
  ↓
free_space_marker_node
  ↓
/perception/lidar/free_space_markers
```

---

## 2. 전체 파이프라인 구조

### 2.1 입력 계층

```
CygLiDAR driver
  ↓
/scan_3D
```

CygLiDAR D1 driver는 LiDAR raw point cloud를 `/scan_3D` 토픽으로 publish한다.

### 2.2 PointCloud 전처리 계층

```
/scan_3D
  ↓
pointcloud_preprocess_node
  ↓
/perception/lidar/points_filtered
```

`pointcloud_preprocess_node`는 raw PointCloud2를 입력받아 다음 작업을 수행한다.

- `laser_frame` 기준 point cloud 수신
- `base_link` 기준 좌표 변환
- ROI 필터링
- voxel downsampling
- 전처리된 point cloud publish

출력 토픽은 다음과 같다.

```
/perception/lidar/points_filtered
```

### 2.3 Local Occupancy Grid 계층

```
/perception/lidar/points_filtered
  ↓
local_occupancy_grid_node
  ↓
/perception/lidar/local_occupancy_grid
```

`local_occupancy_grid_node`는 `base_link` 기준 point cloud를 2D OccupancyGrid로 변환한다.

기본 동작 방식은 다음과 같다.

```
1. 전체 grid를 unknown(-1)으로 초기화
2. LiDAR ray가 지나간 cell을 free(0)로 표시
3. point endpoint가 위치한 cell을 occupied(100)로 표시
4. 필요 시 obstacle inflation 적용
5. nav_msgs/msg/OccupancyGrid publish
```

### 2.4 FreeSpaceModel 계층

```
/perception/lidar/local_occupancy_grid
  ↓
free_space_model_node
  ↓
/perception/lidar/free_space_model
```

`free_space_model_node`는 Local Occupancy Grid를 front/left/right sector별로 해석하여 주행 가능성을 요약한다.

계산하는 주요 정보는 다음과 같다.

- 각 sector의 free 여부
- 각 sector의 blocked 여부
- 각 sector의 nearest occupied clearance
- unknown ratio
- free ratio
- occupied ratio
- 전체 blocked state

### 2.5 RViz Marker 시각화 계층

```
/perception/lidar/free_space_model
  ↓
free_space_marker_node
  ↓
/perception/lidar/free_space_markers
```

`free_space_marker_node`는 FreeSpaceModel 판단 결과를 RViz에서 볼 수 있도록 MarkerArray로 변환한다.

현재 시각화 요소는 다음과 같다.

- front / left / right sector 영역
- sector별 상태 색상
- 방향명 텍스트
- 전체 상태 텍스트

---

## 3. 좌표계 기준

### 3.1 기본 frame

현재 파이프라인의 주요 기준 frame은 다음과 같다.

```
base_link
```

`base_link`는 Spot 몸체 기준 좌표계이다.

```
x+ : Spot 전방
y+ : Spot 좌측
y- : Spot 우측
z+ : 위쪽
```

### 3.2 LiDAR frame

LiDAR 센서는 `laser_frame` 기준으로 raw point cloud를 출력한다.

`pointcloud_preprocess_node`에서 `laser_frame`의 데이터를 `base_link` 기준으로 변환한 뒤 `/perception/lidar/points_filtered`를 publish한다.

### 3.3 static TF

현재 LiDAR 장착 위치는 다음 static transform으로 설정한다.

```bash
ros2 run tf2_ros static_transform_publisher \
  0.14 0.00 0.05 0 0 0 base_link laser_frame
```

의미는 다음과 같다.

```
parent frame : base_link
child frame  : laser_frame

x = 0.14 m
y = 0.00 m
z = 0.05 m
roll  = 0
pitch = 0
yaw   = 0
```

---

## 4. Local Occupancy Grid

## 4.1 노드 정보

```
Node name:
  local_occupancy_grid_node

Input:
  /perception/lidar/points_filtered
  type: sensor_msgs/msg/PointCloud2

Output:
  /perception/lidar/local_occupancy_grid
  type: nav_msgs/msg/OccupancyGrid
```

---

## 4.2 OccupancyGrid cell value

현재 cell value는 다음 기준을 사용한다.

```
unknown  : -1
free     : 0
occupied : 100
```

의미는 다음과 같다.

| 값 | 의미 |
| --- | --- |
| -1 | 아직 관측되지 않은 영역 |
| 0 | LiDAR ray가 통과한 빈 공간 |
| 100 | LiDAR point endpoint 또는 inflated obstacle 영역 |

---

## 4.3 Grid geometry

현재 Local Occupancy Grid는 `base_link` 기준의 2D grid이다.

예시 설정은 다음과 같다.

```yaml
origin_x: -0.50
origin_y: -2.50
size_x: 4.50
size_y: 5.00
resolution: 0.10
```

위 설정의 의미는 다음과 같다.

```
grid origin:
  base_link 기준 (-0.50, -2.50)

grid size:
  x 방향 4.50 m
  y 방향 5.00 m

resolution:
  0.10 m/cell

width:
  45 cells

height:
  50 cells
```

즉, OccupancyGrid는 Spot 주변의 2D 공간을 10cm 단위 cell로 나누어 표현한다.

---

## 4.4 Ray tracing

2차 개선 이후 Local Occupancy Grid는 ray tracing 기반으로 free space를 표시한다.

기본 방식은 다음과 같다.

```
sensor origin → point endpoint
```

이 경로상의 cell은 free로 표시하고, endpoint cell은 occupied로 표시한다.

```
LiDAR ray 경로:
  free(0)

LiDAR point endpoint:
  occupied(100)
```

이를 통해 단순히 obstacle point만 표시하는 것이 아니라, LiDAR가 실제로 통과한 빈 공간을 함께 표현할 수 있다.

---

## 4.5 현재 확인된 로그 예시

정상 동작 시 `local_occupancy_grid_node`는 다음과 같은 로그를 출력한다.

```
local occupancy grid
input points      : 134
valid points      : 134
free cells        : 34
occupied cells    : 64
grid frame        : base_link
grid origin       : (-0.50, -2.50)
grid size         : width=45 height=50 resolution=0.10
ray tracing       : true
```

해석은 다음과 같다.

```
input points:
  입력된 PointCloud2 point 수

valid points:
  height/range filtering 이후 유효한 point 수

free cells:
  ray tracing으로 free 처리된 cell 수

occupied cells:
  point endpoint 및 inflation으로 occupied 처리된 cell 수

grid frame:
  OccupancyGrid의 기준 frame

grid size:
  grid cell 수 및 resolution

ray tracing:
  free cell 생성을 위해 ray tracing 사용 여부
```

---

## 5. FreeSpaceModel

## 5.1 노드 정보

```
Node name:
  free_space_model_node

Input:
  /perception/lidar/local_occupancy_grid
  type: nav_msgs/msg/OccupancyGrid

Output:
  /perception/lidar/free_space_model
  type: robot_interfaces/msg/FreeSpaceModel
```

---

## 5.2 역할

`free_space_model_node`는 Local Occupancy Grid를 front/left/right sector로 나누어 각 방향의 주행 가능성을 요약한다.

이 노드는 최종 주행 명령을 내리는 노드가 아니다.

역할은 다음과 같다.

```
Local Occupancy Grid
  ↓
front / left / right sector 분석
  ↓
각 방향의 free / blocked / unknown 상태 요약
  ↓
planning 또는 decision 계층에 전달 가능한 perception summary 생성
```

---

## 5.3 Sector 기준

현재 sector는 `base_link` 기준 각도 범위로 정의한다.

```yaml
front_min_deg: -20.0
front_max_deg: 20.0

left_min_deg: 20.0
left_max_deg: 60.0

right_min_deg: -60.0
right_max_deg: -20.0
```

각도 기준은 다음과 같다.

```
x+ 방향 : 0도, 전방
y+ 방향 : +90도, 좌측
y- 방향 : -90도, 우측
```

따라서 현재 sector는 LiDAR 수평 FOV 약 -60도 ~ +60도 범위를 3개 영역으로 나누는 구조이다.

```
right : -60° ~ -20°
front : -20° ~ +20°
left  : +20° ~ +60°
```

---

## 5.4 계산 항목

FreeSpaceModel은 각 sector별로 다음 값을 계산한다.

### 5.4.1 free 여부

```
front_free
left_free
right_free
```

해당 방향이 주행 가능 후보인지 나타낸다.

단, `true`라고 해서 바로 주행 명령을 의미하지 않는다.

이는 perception 계층의 후보 판단값이다.

---

### 5.4.2 blocked 여부

```
front_blocked
left_blocked
right_blocked
```

해당 방향이 장애물 또는 clearance 기준에 의해 막혔다고 판단되는지 나타낸다.

현재 기본 판단식은 다음과 같다.

```
blocked =
  occupied_ratio >= occupied_ratio_threshold
  OR
  nearest_occupied_distance < min_clearance_threshold
```

---

### 5.4.3 clearance

```
front_clearance
left_clearance
right_clearance
```

각 sector에서 가장 가까운 occupied cell까지의 거리이다.

단위는 meter이다.

---

### 5.4.4 unknown ratio

```
front_unknown_ratio
left_unknown_ratio
right_unknown_ratio
```

sector 안에서 unknown cell이 차지하는 비율이다.

unknown ratio가 높다는 것은 해당 방향이 실제로 비어 있는지 확신하기 어렵다는 뜻이다.

---

### 5.4.5 free ratio

```
front_free_ratio
left_free_ratio
right_free_ratio
```

sector 안에서 free cell이 차지하는 비율이다.

free cell은 LiDAR ray가 실제로 통과한 영역이다.

---

### 5.4.6 occupied ratio

```
front_occupied_ratio
left_occupied_ratio
right_occupied_ratio
```

sector 안에서 occupied cell이 차지하는 비율이다.

occupied ratio가 높을수록 장애물로 막혀 있을 가능성이 커진다.

---

## 5.5 blocked_state

FreeSpaceModel은 front/left/right 상태를 종합해 `blocked_state`를 출력한다.

현재 정의는 다음과 같다.

```
STATE_CLEAR=0
STATE_FRONT_BLOCKED=1
STATE_LEFT_BLOCKED=2
STATE_RIGHT_BLOCKED=3
STATE_FRONT_LEFT_BLOCKED=4
STATE_FRONT_RIGHT_BLOCKED=5
STATE_LEFT_RIGHT_BLOCKED=6
STATE_SURROUNDED=7
STATE_UNKNOWN_DOMINANT=8
```

특히 `STATE_UNKNOWN_DOMINANT=8`은 다음 의미를 가진다.

```
unknown cell 비율이 높아 free / blocked 판단 신뢰도가 낮은 상태
```

즉, 이는 단순히 “장애물로 막힘”이 아니라 “관측 부족으로 판단 신뢰도가 낮음”에 가깝다.

---

## 5.6 현재 확인된 로그 예시

정상 동작 시 `free_space_model_node`는 다음과 같은 로그를 출력한다.

```
FreeSpaceModel callback received: frame=base_link width=45 height=50 data_size=2250
Publishing FreeSpaceModel: front_free=0 left_free=0 right_free=0 state=8

free space model
front: free=0 blocked=1 clearance=0.35 unknown=0.87 free_ratio=0.01 occupied=0.12
left : free=0 blocked=1 clearance=0.38 unknown=0.84 free_ratio=0.01 occupied=0.15
right: free=0 blocked=1 clearance=0.38 unknown=0.91 free_ratio=0.00 occupied=0.09
blocked_state=8
```

해석은 다음과 같다.

```
callback received:
  /perception/lidar/local_occupancy_grid를 정상 수신함

width=45 height=50 data_size=2250:
  OccupancyGrid 크기가 정상이며 data 배열도 채워져 있음
  45 * 50 = 2250

front_free=0:
  전방은 주행 가능 후보가 아님

front_blocked=1:
  전방은 blocked 조건에 걸림

clearance=0.35:
  전방 sector에서 가장 가까운 occupied cell까지 약 0.35m

unknown=0.87:
  전방 sector의 약 87%가 unknown cell

free_ratio=0.01:
  전방 sector에서 LiDAR ray가 통과한 free cell은 약 1%

occupied=0.12:
  전방 sector의 약 12%가 occupied cell

blocked_state=8:
  전체 상태는 UNKNOWN_DOMINANT
```

현재 관찰된 주요 특징은 다음과 같다.

```
1. front / left / right 모두 unknown ratio가 높게 나타남
2. free_ratio는 매우 낮게 나타남
3. clearance가 0.3~0.6m 수준으로 비교적 짧게 나타남
4. blocked_state가 STATE_UNKNOWN_DOMINANT(8)로 자주 출력됨
```

이는 현재 CygLiDAR FOV, ROI, grid 크기, ray tracing 방식, threshold 설정이 복합적으로 영향을 준 결과이다.

---

## 6. FreeSpaceMarker 시각화

## 6.1 노드 정보

```
Node name:
  free_space_marker_node

Input:
  /perception/lidar/free_space_model
  type: robot_interfaces/msg/FreeSpaceModel

Output:
  /perception/lidar/free_space_markers
  type: visualization_msgs/msg/MarkerArray
```

---

## 6.2 역할

`free_space_marker_node`는 FreeSpaceModel의 판단 결과를 RViz에서 직관적으로 확인하기 위한 시각화 노드이다.

정량값은 `free_space_model_node` 로그로 확인하고, RViz에서는 판단 sector와 핵심 상태만 확인하는 것을 목표로 한다.

---

## 6.3 현재 시각화 요소

현재 RViz에는 다음 정보가 표시된다.

```
1. front / left / right sector 부채꼴 영역
2. 각 sector의 상태 색상
3. FRONT / LEFT / RIGHT 방향명
4. 전체 상태 텍스트
```

---

## 6.4 색상 의미

현재 색상 정책은 다음과 같다.

```
초록:
  free 상태

빨강:
  blocked 상태

노랑/주황:
  UNKNOWN_DOMINANT 또는 unknown_ratio가 높은 상태

회색:
  free도 blocked도 아닌 애매한 상태
```

현재 관찰 상태에서는 unknown ratio가 높기 때문에 sector가 주로 노랑/주황으로 표시된다.

---

## 6.5 RViz에서의 의미

RViz에 보이는 부채꼴 sector는 LiDAR 원본 FOV 자체가 아니다.

정확한 의미는 다음과 같다.

```
FreeSpaceModel이 local_occupancy_grid 위에서 front / left / right 판단을 수행하는 base_link 기준 sector 영역
```

즉, 부채꼴은 다음과 다르다.

```
LiDAR 원본 스펙 FOV 자체 X
preprocess ROI 사각형 자체 X
```

다만 현재 sector 각도 범위가 CygLiDAR의 수평 FOV와 유사하게 설정되어 있기 때문에, LiDAR FOV와 비슷한 형태로 보일 수 있다.

---

## 6.6 Fixed Frame에 따른 회전 해석

FreeSpace sector는 `base_link` 기준으로 표시된다.

따라서 RViz의 Fixed Frame에 따라 보이는 방식이 달라진다.

### Fixed Frame = base_link

```
로봇 기준 시점으로 보기 때문에 sector가 항상 로봇 앞에 고정되어 보임
```

이는 정상이다.

### Fixed Frame = map 또는 odom

```
로봇이 회전하면 base_link도 회전하므로 sector도 월드 기준에서 함께 회전해 보임
```

단, 이를 위해서는 `map/odom -> base_link` TF가 필요하다.

현재 localization 또는 odometry가 완성되지 않은 상태에서는 보통 `base_link` 기준으로 확인한다.

---

## 7. 파라미터 관리

FreeSpaceModel과 FreeSpaceMarker는 같은 sector 기준을 공유해야 한다.

따라서 `free_space_marker.param.yaml`을 별도로 두기보다는, `free_space_model.param.yaml` 하나에서 함께 관리하는 것을 권장한다.

예시 구조는 다음과 같다.

```yaml
/**:
  ros__parameters:
    # Common sector range
    front_min_deg: -20.0
    front_max_deg: 20.0

    left_min_deg: 20.0
    left_max_deg: 60.0

    right_min_deg: -60.0
    right_max_deg: -20.0

    # Common unknown threshold
    unknown_ratio_threshold: 0.60

free_space_model_node:
  ros__parameters:
    input_topic: "/perception/lidar/local_occupancy_grid"
    output_topic: "/perception/lidar/free_space_model"
    frame_id: "base_link"

    unknown_value: -1
    free_value: 0
    occupied_value: 100

    min_check_range: 0.20
    max_check_range: 2.50

    occupied_ratio_threshold: 0.05
    min_clearance_threshold: 0.80
    free_ratio_threshold: 0.20

free_space_marker_node:
  ros__parameters:
    input_topic: "/perception/lidar/free_space_model"
    output_topic: "/perception/lidar/free_space_markers"
    frame_id: "base_link"

    front_angle_deg: 0.0
    left_angle_deg: 40.0
    right_angle_deg: -40.0

    marker_z: 0.12
    text_z: 0.45
    text_size: 0.10
    boundary_length: 2.00
    marker_lifetime_sec: 0.50
```

---

## 8. 실행 방법

## 8.1 실행 전 공통 환경 설정

각 터미널에서 다음 환경을 맞춘다.

```bash
cd ~/robot_ws
source /opt/ros/humble/setup.bash
source ~/robot_ws/install/setup.bash

export ROS_DOMAIN_ID=0
export ROS_LOCALHOST_ONLY=0
unset RMW_IMPLEMENTATION
unset FASTRTPS_DEFAULT_PROFILES_FILE
unset CYCLONEDDS_URI
```

특히 MobaXterm 사용 시 `FASTRTPS_DEFAULT_PROFILES_FILE`이 설정되어 있으면 DDS 통신 문제가 발생할 수 있으므로 반드시 해제한다.

확인:

```bash
echo $FASTRTPS_DEFAULT_PROFILES_FILE
```

아무것도 출력되지 않아야 한다.

---

## 8.2 노드별 실행 순서

### Terminal 1. CygLiDAR driver

```bash
ros2 launch cyglidar_d1_ros2 cyglidar.launch.py
```

확인:

```bash
ros2 topic hz /scan_3D
```

---

### Terminal 2. static TF

```bash
ros2 run tf2_ros static_transform_publisher \
  0.14 0.00 0.05 0 0 0 base_link laser_frame
```

확인:

```bash
ros2 run tf2_ros tf2_echo base_link laser_frame
```

---

### Terminal 3. PointCloud preprocess

```bash
ros2 run lidar_perception pointcloud_preprocess_node --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/pointcloud_preprocess.param.yaml
```

확인:

```bash
ros2 topic hz /perception/lidar/points_filtered
ros2 topic echo /perception/lidar/points_filtered --once | grep frame_id
```

---

### Terminal 4. Local Occupancy Grid

```bash
ros2 run lidar_perception local_occupancy_grid_node --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/local_occupancy_grid.param.yaml
```

확인:

```bash
ros2 topic hz /perception/lidar/local_occupancy_grid
ros2 topic echo /perception/lidar/local_occupancy_grid --once | grep -E "frame_id|resolution|width|height"
```

---

### Terminal 5. FreeSpaceModel

```bash
ros2 run lidar_perception free_space_model_node --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/free_space_model.param.yaml
```

확인:

```bash
ros2 topic hz /perception/lidar/free_space_model
ros2 topic echo /perception/lidar/free_space_model --once
```

---

### Terminal 6. FreeSpaceMarker

```bash
ros2 run lidar_perception free_space_marker_node --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/free_space_model.param.yaml
```

확인:

```bash
ros2 topic hz /perception/lidar/free_space_markers
```

---

### Terminal 7. RViz

```bash
rviz2
```

또는 기존 RViz 설정 파일 사용:

```bash
rviz2 -d /home/jetson/.rviz2/lidar_preprocess.rviz
```

RViz 설정:

```
Global Options
  Fixed Frame: base_link
```

추가 Display:

```
TF

PointCloud2
  Topic: /perception/lidar/points_filtered

Map
  Topic: /perception/lidar/local_occupancy_grid

MarkerArray
  Topic: /perception/lidar/free_space_markers
```

---

## 9. 주요 확인 명령어

### 토픽 목록 확인

```bash
ros2 topic list | grep /perception/lidar
```

기대 토픽:

```
/perception/lidar/points_filtered
/perception/lidar/local_occupancy_grid
/perception/lidar/free_space_model
/perception/lidar/free_space_markers
```

---

### 주기 확인

```bash
ros2 topic hz /perception/lidar/points_filtered
ros2 topic hz /perception/lidar/local_occupancy_grid
ros2 topic hz /perception/lidar/free_space_model
ros2 topic hz /perception/lidar/free_space_markers
```

---

### FreeSpaceModel 메시지 확인

```bash
ros2 topic echo /perception/lidar/free_space_model --once
```

---

### Local Occupancy Grid 확인

```bash
ros2 topic echo /perception/lidar/local_occupancy_grid --once | grep -E "frame_id|resolution|width|height"
```

기대 예시:

```
frame_id: base_link
resolution: 0.1
width: 45
height: 50
```

---

## 10. 현재 한계 및 향후 개선 사항

현재 FreeSpaceModel은 MVP 수준으로 정상 동작한다.

다만 다음 한계가 확인되었다.

```
1. unknown_ratio가 높게 나타나 UNKNOWN_DOMINANT 상태가 자주 발생함
2. free_ratio가 매우 낮게 나타남
3. occupied_ratio와 clearance 기준이 보수적으로 작동함
4. CygLiDAR FOV, ROI, grid 크기, ray tracing 방식에 따라 판단 결과가 민감하게 변함
```

향후 개선 방향은 다음과 같다.

```
1. free_space_model.param.yaml threshold 튜닝
2. occupied_ratio_threshold 완화
3. unknown_ratio_threshold 완화
4. min_clearance_threshold 조정
5. local_occupancy_grid size / ROI 조정
6. inflation radius 조정
7. 실제 장애물 위치별 테스트
8. decision/planning 계층과 연동 후 최종 정책 조정
```

---

## 11. 현재 개발 상태 요약

현재까지 완료된 내용은 다음과 같다.

```
local_occupancy_grid_node
  완료
  - PointCloud2 기반 2D OccupancyGrid 생성
  - unknown/free/occupied cell 표현
  - LiDAR ray tracing 적용
  - RViz Map 시각화 가능

free_space_model_node
  완료
  - OccupancyGrid 기반 front/left/right sector 분석
  - free/blocked/unknown ratio 계산
  - clearance 계산
  - blocked_state publish

free_space_marker_node
  완료
  - FreeSpaceModel 결과를 RViz MarkerArray로 시각화
  - sector 기반 부채꼴 표시
  - UNKNOWN / FREE / BLOCKED 색상 구분
```

현재 FreeSpaceModel은 MVP 기능 구현이 완료되었으며, 추후 상위 decision/planning 계층과 연동하면서 threshold 및 판단 정책을 튜닝할 예정이다.

```

---

이 README는 현재 개발 상태를 기준으로 하면 충분히 실무적으로 정리된 수준이야.
다음 단계로 넘어가기 전에 이 파일만 저장해두면, 나중에 다시 튜닝하거나 팀원이 봐도 **왜 LocalOccupancyGrid와 FreeSpaceModel을 만들었는지, 어떤 토픽을 쓰는지, 어디까지 구현됐는지** 바로 이해할 수 있어.
```