# [Perception] LiDAR 환경 모델 설계

상태: Perception

### 1단계. CygLiDAR raw topic 분석 먼저 고정

가장 먼저 `/scan`, `/scan_2D`, `/scan_3D`, `/scan_image`를 보고 **실제 입력으로 뭘 쓸지 확정**해야 해.

특히 MVP는 `/scan`이 핵심이야.

먼저 확인:

```
ros2 topic type /scan
ros2 topic info /scan
ros2 topicecho /scan--once
ros2 topic hz /scan
```

여기서 봐야 할 건:

- `frame_id`
- `angle_min`, `angle_max`
- `angle_increment`
- `range_min`, `range_max`
- `ranges[]`

즉, **전방/좌/우 sector 분할이 가능한지**부터 먼저 확인하는 거야.

---

### 2단계. `robot_interfaces` 패키지부터 만들기

이게 첫 구현 포인트야.

공용 메시지는 전부 `robot_ws/src/interfaces/robot_interfaces/msg/`에 둔다고 컨벤션에도 정리했잖아.

먼저 만들 것:

- `ObstacleModel.msg`
- `FreeSpaceModel.msg`
- `LocalizationHint.msg`

이건 나중에 perception/planning/localization이 다 공통으로 참조하니까, **제일 먼저 고정**하는 게 맞아.

---

### 3단계. `lidar_perception` 패키지 생성

그다음 바로 `perception/lidar_perception` 패키지를 만든다.

컨벤션 문서 기준으로 이 패키지는 아래까지만 책임진다.

- `/scan` subscribe
- 공통 전처리
- `local_obstacle_points` publish
- `local_occupancy_grid` publish
- `ObstacleModel`, `FreeSpaceModel`, `LocalizationHint` publish

즉, 여기서 **local path 생성은 절대 하지 말고**, perception까지만 한다.

---

### 4단계. 첫 구현은 `ObstacleModel` 하나만

처음부터 다 하지 말고, **제일 먼저 `/scan` → sector 분할 → `ObstacleModel` publish**만 하자.

추천 구현 순서:

1. `/scan` subscribe
2. `angle_min`, `angle_increment` 기반으로 index→angle 계산
3. 전방/좌/우 sector 범위 정의
4. sector별
    - `detected`
    - `min_distance`
    - `nearest_angle`
    계산
5. `emergency_stop_required` 계산
6. `/perception/lidar/obstacle_model` publish

이 단계가 끝나면:

- raw topic 구조 이해 완료
- sector 분할 로직 검증 완료
- custom msg 파이프라인 검증 완료

즉 이후 단계들이 훨씬 쉬워져.

---

### 5단계. 그다음 `local_obstacle_points`

`ObstacleModel`이 되면 그다음은 `local_obstacle_points`야.

이건:

- `/scan`의 polar 데이터
- `(distance, angle)`를 `x, y` 점으로 변환
- `sensor_msgs/msg/PointCloud2`로 publish

즉 **전처리된 장애물 점 집합**을 만드는 단계야.

너희 정의서에서도 이게 `ObstacleModel`, `FreeSpaceModel`, `LocalizationHint`의 공통 intermediate 입력으로 정리돼 있어.

---

### 6단계. 그다음 `local_occupancy_grid`

이건 `local_obstacle_points` 기반으로 만드는 게 자연스러워.

즉:

- Spot 주변 전체 공간을 grid로 나누고
- obstacle point가 찍힌 칸은 occupied
- 나머지는 free/unknown으로 관리

이렇게 해서 `/perception/lidar/local_occupancy_grid`를 만든다.

이건 `nav_msgs/msg/OccupancyGrid`로 가면 돼.

---

### 7단계. 이제 `FreeSpaceModel`

이건 `local_occupancy_grid`를 해석해서 요약값으로 만든다.

즉:

- 전방/좌/우 clearance
- 전방/좌/우 blocked
- `blocked_state`
- `free_direction_candidates`

를 계산해서 publish

중요한 건:

- `best_escape_direction` 같은 최종 판단은 넣지 않음
- “갈 수 있는 후보”까지만 perception이 계산

이건 정의서에 정리한 그대로 가져가면 돼.

---

### 8단계. 마지막으로 `LocalizationHint`

이건 제일 뒤에 붙이는 게 좋아.

이유:

- obstacle / free space보다 구현 난도가 높고
- 구조 해석(벽/코너)이 들어가니까
- raw topic 특성을 충분히 이해한 뒤 붙이는 게 맞음

즉 마지막에:

- `left_wall_distance`
- `right_wall_distance`
- `front_wall_distance`
- `left/right_wall_alignment_score`
- `corner_detected`

순으로 확장

## 기본 LiDAR 테스트

- 1️⃣ **센서 구동 + 토픽 확인** 테스트
    
    **[터미널 1] - CygLiDAR 노드 실행**
    
    ```bnf
    cd ~/cyglidar_test_ws
    source /opt/ros/humble/setup.bash
    source install/setup.bash
    ros2 launch cyglidar_d1_ros2 cyglidar.launch.py
    ```
    
    **[터미널 2] - 토픽 확인**
    
    ```bnf
    cd ~/cyglidar_test_ws
    source /opt/ros/humble/setup.bash
    source install/setup.bash
    
    ros2 topic list | grep scan
    ros2 topic echo /scan --once
    ros2 topic hz /scan
    ```
    
    이 방식은
    
    - 노드가 정상 실행되는지
    - `/scan`, `/scan_2D`, `/scan_3D`, `/scan_image` 가 뜨는지
    - `/scan` 주기가 나오는지
- **2️⃣ 시각화 포함 테스트 (RViz 활용)**
    
    **[터미널 1] - 드라이버 + RViz 같이 실행**
    
    ```bnf
    cd ~/cyglidar_test_ws
    source /opt/ros/humble/setup.bash
    source install/setup.bash
    ros2 launch cyglidar_d1_ros2 view_cyglidar.launch.py
    ```
    
    **[터미널 2] - 토픽 확인**
    
    ```
    cd ~/cyglidar_test_ws
    source /opt/ros/humble/setup.bash
    source install/setup.bash
    
    ros2 topic list |grep scan
    ros2 topic hz /scan
    ```
    
    **[터미널 3] - 필요하면 echo 확인**
    
    ```
    cd ~/cyglidar_test_ws
    source /opt/ros/humble/setup.bash
    source install/setup.bash
    ros2 topicecho /scan--once
    ```
    
    ---
    
- **3️⃣ ROS2 이미지 뷰어 실행 명령어**
    
    ```bnf
    ros2 run rqt_image_view rqt_image_view
    ```
    
    ![image.png](image%2022.png)
    

cyglidar 기본 실행 명령어

```python
ros2 launch cyglidar_d1_ros2 cylidar.launch.py
```

## 0. 최종 목표 구조

```python
[Raw LiDAR]
/scan_3D
   ↓

[Preprocess]
pointcloud_preprocess_node
   ↓
/perception/lidar/points_filtered

   ├────────────────────────────────────┐
   │                                    │
   ↓                                    ↓

[Obstacle branch]                 [Local environment branch]
obstacle_cluster_node              local_obstacle_points_node
   ↓                                    ↓
/obstacle_clusters                 /local_obstacle_points
   ↓                                    ↓
obstacle_model_node                local_occupancy_grid_node
   ↓                                    ↓
/obstacle_model                    /local_occupancy_grid
                                        ↓
                                  free_space_model_node
                                        ↓
                                  /free_space_model
                                        ↓
                                  gap_model_node
                                        ↓
                                  /gap_model
```

- `ObstacleModel` : “대표 장애물 요약”
- `OccupancyGrid` : “공간 점유 상태”
- `FreeSpaceModel` : “주행 가능 공간 요약”
- `GapModel` : “통과 가능한 빈 공간 후보”

## 1. Obstacle Model

> 3D 형상 기반 Spot 주변 장애물 탐지 모델
> 

```python
/scan_3D
  ↓
pointcloud_preprocess_node
  ↓
/points_filtered
  ↓
obstacle_cluster_node
  ↓
/obstacle_clusters
  ↓
obstacle_model_node
  ↓
/obstacle_model
```

- [핵심 동작]
    1. `/scan_3D`의 `PointCloud2`를 사용
    2. 점 하나가 아니라 **물체 클러스터 단위**로 본다
    3. 각 클러스터의 **대표 중심, 거리, 각도**를 계산한다
    4. 그 각도를 **Spot의 `base_link` 정면 기준**으로 해석해서 front/left/right 섹터로 장애물 방향 판단
    5. 출력 결과를 **상세 목록**(**`ObstacleClusters`)** 과 **요약 결과(`ObstacleModel`)**로 산출
- **ObstacleCluster.msg**
    - 역할 : 개별 3D 장애물 클러스터 클러스터 1개의 상세 정보
    - Topic :
    
    ```python
    # 개별 3D 장애물 클러스터 1개를 표현하는 메시지
    std_msgs/Header header
    
    # 이 슬롯에 실제 클러스터가 존재하는지 여부
    # - ObstacleModel 안의 front/left/right 대표 슬롯에서 사용
    bool valid
    
    uint32 cluster_id      # 현재 프레임 내 클러스터 식별자
    
    # 이 클러스터를 구성하는 포인트 개수
    uint32 point_count
    
    # --------------------------------------------------
    # Spot base_link 기준 클러스터 중심점 좌표 (m)
    # --------------------------------------------------
    float32 centroid_x
    float32 centroid_y
    float32 centroid_z
    
    # --------------------------------------------------
    # Spot base_link 기준 클러스터 내 최근접 포인트 좌표 (m)
    # - 충돌 위험 판단에는 이 점이 더 중요할 수 있음
    # --------------------------------------------------
    float32 nearest_x
    float32 nearest_y
    float32 nearest_z
    
    # --------------------------------------------------
    # 대표 거리 정보
    # --------------------------------------------------
    # 클러스터 중심점까지의 수평 거리
    float32 centroid_distance_xy
    
    # 클러스터 내 최근접 포인트까지의 수평 거리
    float32 nearest_distance_xy
    
    # --------------------------------------------------
    # 대표 방향 정보
    # --------------------------------------------------
    # Spot 정면(+x) 기준 수평 방위각
    # atan2(centroid_y, centroid_x) [rad]
    #   0     : 정면
    #   +값   : 왼쪽
    #   -값   : 오른쪽
    float32 azimuth_angle_rad
    
    # Spot 기준 수직 방향각
    # atan2(centroid_z, sqrt(x^2 + y^2)) [rad]
    float32 elevation_angle_rad
    
    # --------------------------------------------------
    # 클러스터 외접박스 크기 (m)
    # --------------------------------------------------
    float32 bbox_size_x
    float32 bbox_size_y
    float32 bbox_size_z
    
    # --------------------------------------------------
    # Spot 기준 섹터 분류 결과
    # --------------------------------------------------
    uint8 sector
    uint8 SECTOR_NONE=0
    uint8 SECTOR_FRONT=1
    uint8 SECTOR_LEFT=2
    uint8 SECTOR_RIGHT=3
    ```
    
- **ObstacleClusters.msg**
    - 역할 : 현재 라이다가 본 전체 3D 물체 목록
    - Topic :
    
    ```python
    # 현재 프레임에서 인식된 전체 3D 장애물 클러스터 목록
    
    std_msgs/Header header
    
    ObstacleCluster[] clusters    # 현재 프레임의 전체 클러스터 배열
    ```
    
    예를 들어:
    
    - 사람 1개, 박스 1개, 의자 1개이면 `clusters[]` 안에 3개가 들어감.
- **ObstacleModel.msg**
    
    **Topic : `/perception/lidar/obstacle_model`**
    
    ```python
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
    
    - 설계 초안
        
        응, 네가 말한 방향이 **더 맞다**고 봐.
        
        지금까지 만든 건 **2D LaserScan 기반의 임시 MVP**고, 네가 진짜 원했던 건 그게 아니라:
        
        - 물체를 **전반적인 3D 형상**으로 보고
        - 그 형상 단위로 **클러스터**를 만들고
        - 각 클러스터의 **대표 중심 / 거리 / 각도**를 구한 뒤
        - **Spot의 전방 기준**으로 그 물체가 front / left / right 어디에 있는지 판단하는 것
        
        이거잖아.
        
        그 관점이면 지금 `/scan` 기반 코드는 구조적으로 한계가 있어.
        
        즉 지금은 **라이다 수평 단면 한 장**만 보고 있는 거고,
        
        네가 원하는 건 **3D 점군 전체를 보고 물체 단위로 해석**하는 거야.
        
        ## 내 생각
        
        결론적으로는:
        
        **지금 로직을 조금 고치는 수준이 아니라, 입력 데이터와 처리 파이프라인을 3D PointCloud 중심으로 바꾸는 게 맞아.**
        
        즉:
        
        - 입력을 `/scan`(`LaserScan`)에서
        - `/scan_3D`(`PointCloud2`) 중심으로 옮기고
        - `ObstacleModel`도 “포인트/2D 단면” 기준이 아니라
        - **3D 클러스터 기준**으로 다시 설계하는 게 맞아.
        
        ---
        
        # 왜 네 생각이 맞냐
        
        ## 지금 `/scan` 기반 방식의 한계
        
        지금 방식은 결국 이런 거야.
        
        - 라이다 높이의 수평 평면에서만 본다
        - 그 평면을 지나가는 점들만 본다
        - 물체 전체가 아니라 **단면**을 본다
        
        그래서:
        
        - 낮은 장애물은 안 잡힐 수 있고
        - 높은 구조물은 일부만 잡히고
        - 사람/박스 같은 것도 “형상 전체”가 아니라 스캔 평면에 잘린 일부만 보게 돼
        
        즉 “물체가 오른쪽에 있다”를 말하고 싶어도
        
        실제로는 “오른쪽 평면 단면의 점이 있었다” 수준인 거야.
        
        ---
        
        ## 네가 원하는 방식
        
        네 설명은 정확히 이런 구조야.
        
        1. 3D point cloud를 받는다
        2. point cloud에서 **물체 단위 클러스터**를 만든다
        3. 각 클러스터의 대표값을 구한다
            - 중심점(center)
            - 대표 거리
            - 대표 각도
        4. 그 각도가 Spot 전방 기준으로
            - 정면이면 front
            - 오른쪽이면 right
            - 왼쪽이면 left
            로 분류한다
        5. 그 결과를 `obstacle_model` output으로 쓴다
        
        이건 훨씬 자연스럽고,
        
        실제로 “물체가 어디 있는가”라는 질문에 더 맞는 모델이야.
        
        ---
        
        # 그럼 어떻게 바꿔야 하냐
        
        ## 1. 입력을 `PointCloud2` 중심으로 바꾼다
        
        이제 메인 입력은 `/scan_3D`가 되어야 해.
        
        즉:
        
        - 현재: `/scan` (`LaserScan`)
        - 변경: `/scan_3D` (`PointCloud2`)
        
        이유는 3D 형상과 높이 정보를 보려면 `(x, y, z)` 점들이 필요하기 때문이야.
        
        ---
        
        ## 2. 좌표계 기준을 먼저 명확히 해야 한다
        
        여기서 중요한 건 “Spot의 헤더 방향”이라고 했는데, 정확히는 **Spot의 body frame 기준**으로 보는 게 맞아.
        
        보통은:
        
        - `base_link` 또는 `base_footprint`
        같은 로봇 본체 기준 좌표계를 쓴다.
        
        즉 point cloud를 먼저 **Spot 본체 기준 좌표계**로 변환해야 해.
        
        ### 이게 중요한 이유
        
        Spot이 세상 좌표에서 어느 방향을 보고 있든,
        
        `base_link` 기준으로 보면:
        
        - 앞 = `+x`
        - 왼쪽 = `+y`
        - 오른쪽 = `y`
        
        처럼 상대 위치를 일관되게 해석할 수 있어.
        
        즉 “Spot의 heading 기준으로 물체가 어디 있냐”는 질문은
        
        사실상 **point cloud를 `base_link` frame으로 변환한 뒤 판단하는 문제**야.
        
        ---
        
        ## 3. 3D point cloud 전처리
        
        바로 클러스터링하지 말고 보통 이 순서가 좋아.
        
        ### 추천 전처리
        
        1. **ROI 제한**
            - 너무 먼 점 제거
            - 예: 전방 3m, 좌우 2m, 높이 -0.2~1.5m
        2. **Voxel downsampling**
            - 점이 너무 많으면 줄이기
            - 예: 5cm voxel
        3. **바닥 제거**
            - 바닥면 제거 안 하면 바닥 점들이 큰 클러스터가 될 수 있음
        4. **필요하면 노이즈 제거**
            - isolated point 제거
        
        즉 point cloud를 바로 쓰지 말고
        
        **의미 있는 3D 장애물 후보 점군**으로 먼저 정제해야 해.
        
        ---
        
        ## 4. 그다음 3D 클러스터링
        
        그다음에는 point cloud에서 **Euclidean clustering** 같은 걸 하면 돼.
        
        쉽게 말하면:
        
        - 서로 가까운 3D 점들끼리 묶어서
        - 하나의 물체 클러스터로 보는 거야
        
        예:
        
        - 사람 한 명 = 클러스터 1개
        - 박스 = 클러스터 1개
        - 의자 = 클러스터 1개
        
        이렇게.
        
        ---
        
        ## 5. 각 클러스터의 대표값 계산
        
        클러스터마다 대표값이 필요해.
        
        ### 최소한 필요한 값
        
        - `centroid_x, centroid_y, centroid_z`
        - `min_distance`
        - `azimuth_angle`
        - `point_count`
        - `bbox size` 또는 extents
        
        ### 대표 거리
        
        여긴 선택지가 있어.
        
        ### 방법 A. centroid 거리
        
        클러스터 중심점까지의 거리
        
        ### 방법 B. nearest point 거리
        
        클러스터 중 가장 가까운 점까지의 거리
        
        ### 추천
        
        - **위치/방향 판단**은 centroid 기준
        - **충돌 위험 판단**은 nearest point 기준
        
        즉 둘 다 갖고 있는 게 좋아.
        
        ---
        
        ## 6. 각도 계산은 이렇게 한다
        
        클러스터 중심점이 `base_link` 기준으로 `(x, y, z)`라고 하면:
        
        ### 수평 각도(azimuth)
        
        ```
        angle =atan2(y,x)
        ```
        
        이 값이 바로
        
        - `0`이면 정면
        - `+`면 왼쪽
        - 면 오른쪽
        
        이야.
        
        즉 네가 말한
        
        “Spot의 정면 헤더 기준으로 오른쪽에 있으면 오른쪽 장애물로 인식”
        
        이걸 수학적으로 구현한 게 바로 `atan2(y, x)`야.
        
        ### 거리
        
        수평 거리만 볼 거면:
        
        ```
        distance_xy =sqrt(x*x+y*y)
        ```
        
        3D 거리까지 볼 거면:
        
        ```
        distance_3d =sqrt(x*x+y*y+z*z)
        ```
        
        보통 장애물 방향 분류는 `atan2(y, x)`,
        
        충돌/근접 판단은 `distance_xy`로 많이 본다.
        
        ---
        
        ## 7. 섹터 분류는 클러스터 중심 각도로
        
        이제 point가 아니라 **클러스터**를 분류하면 된다.
        
        예:
        
        - front: `20° ~ +20°`
        - left: `+20° ~ +90°`
        - right: `90° ~ -20°`
        
        이렇게 두고,
        
        클러스터 중심각 `azimuth_angle`이:
        
        - `+35°`면 left
        - `40°`면 right
        - `+5°`면 front
        
        처럼 분류하는 거야.
        
        이게 네가 원한 방식과 거의 동일해.
        
        ---
        
        # 그럼 `ObstacleModel` output은 어떻게 해야 하냐
        
        ## 지금처럼 front/left/right 요약 필드만 둘 수도 있음
        
        즉 각 섹터에 대해:
        
        - 대표 클러스터 1개만 남기고
        - 그 클러스터의 대표 거리/각도를 output으로 둔다
        
        예:
        
        - `front_detected`
        - `front_distance`
        - `front_angle`
        - `right_detected`
        - `right_distance`
        - `right_angle`
        
        이건 기존 구조와 호환성이 좋아.
        
        ---
        
        ## 하지만 더 좋은 건 “클러스터 리스트”도 따로 두는 것
        
        왜냐하면 실제론 장애물이 여러 개일 수 있잖아.
        
        그래서 추천은:
        
        ### A. 요약 토픽
        
        `/perception/lidar/obstacle_model`
        
        - front / left / right 요약
        
        ### B. 상세 토픽
        
        `/perception/lidar/obstacle_clusters`
        
        - 모든 3D 클러스터 리스트
        - centroid / distance / angle / size / sector
        
        이렇게 두는 거야.
        
        즉 planning은 요약 토픽을 빠르게 보고,
        
        디버깅/고도화 로직은 상세 토픽을 보는 구조.
        
        ---
        
        # 내가 보기엔 아키텍처를 이렇게 바꾸는 게 맞다
        
        ## 새 파이프라인
        
        `/scan_3D (PointCloud2)`
        
        → `point cloud preprocessing`
        
        → `3D clustering`
        
        → `cluster descriptors 생성`
        
        → `cluster angle 기준 sector 분류`
        
        → `ObstacleModel 요약 생성`
        
        ---
        
        ## 구체적으로 나누면
        
        ### 1단계
        
        `pointcloud_preprocess_node`
        
        - frame transform to `base_link`
        - ROI crop
        - voxel downsample
        - ground removal
        
        ### 2단계
        
        `obstacle_cluster_node`
        
        - Euclidean clustering
        - 각 cluster의 centroid, distance, angle 계산
        
        ### 3단계
        
        `obstacle_model_node`
        
        - cluster들을 front/left/right로 분류
        - 대표 cluster 선택
        - `ObstacleModel` publish
        
        이렇게 역할 분리하면 구조도 훨씬 좋다.
        
        ---
        
        # 그래서 지금 코드 전반적으로 수정해야 하냐
        
        **응, 맞아.**
        
        하지만 “지금까지 한 게 다 틀렸다”는 건 아니고,
        
        지금까지 한 건:
        
        - 2D LaserScan 기반 빠른 MVP
        - 섹터 해석 개념 검증
        - 클러스터 기반 사고방식 검증
        
        이 역할은 충분히 했어.
        
        즉 지금까지는 **문제 정의와 설계 감 잡기** 단계였고,
        
        이제는 **진짜 원하는 구조로 갈 때가 됐다**고 보면 돼.
        
        ---
        
        # 내 의견 정리
        
        네가 말한 방향은 맞고, 오히려 더 현업스럽다.
        
        특히 네가 말한:
        
        - “전반적인 3D 형상을 인식”
        - “그 형상에 대한 클러스터 생성”
        - “대표 중심 / 거리 / 각도 계산”
        - “Spot 전방 기준 상대 위치 판단”
        - “그 결과를 obstacle_model output으로 사용”
        
        이건 아주 자연스러운 구조야.
        
        ## 단, 한 가지 보완
        
        `ObstacleModel` 하나에 모든 걸 다 넣기보다,
        
        - **요약 모델**
        - **클러스터 상세 모델**
        
        둘로 나누는 걸 추천해.
        
        그게 유지보수도 쉽고 디버깅도 편하다.
        
        ---
        
        ## 한 줄 결론
        
        **네가 말한 방향이 더 맞다.** 지금의 2D LaserScan 기반 obstacle 로직은 단면 기반 임시 MVP로 보고, 앞으로는 `/scan_3D` PointCloud2를 `base_link` 기준으로 변환한 뒤 3D 클러스터링을 수행하고, 각 클러스터의 centroid / distance / azimuth를 계산해서 Spot 전방 기준 front/left/right로 분류한 결과를 `ObstacleModel` output으로 쓰는 구조로 가는 게 맞다.
        
        다음엔 이걸 기준으로
        
        **새 `ObstacleModel` 구조와 `obstacle_clusters.msg` 설계**부터 다시 잡아보자.
        
    - 구현 과정
        
        좋아. 네가 말한 방향이 더 맞고, 지금부터는 **2D LaserScan 기반 임시 로직**이 아니라 **3D PointCloud 기반 “물체 클러스터 → 대표값 → 섹터 요약” 구조**로 다시 설계하는 게 맞아.
        
        핵심 철학은 이거야.
        
        **점(point) 단위가 아니라 물체(cluster) 단위로 판단한다.**
        
        그리고 그 물체를 **Spot의 `base_link` 기준**으로 해석해서 front / left / right에 배정한다.
        
        ---
        
        # 1. 최종 목표 구조
        
        입력:
        
        - `/scan_3D` (`sensor_msgs/msg/PointCloud2`)
        
        출력:
        
        - `/perception/lidar/obstacle_clusters`
        → 현재 보이는 **모든 3D 장애물 클러스터 상세 정보**
        - `/perception/lidar/obstacle_model`
        → front / left / right별 **대표 장애물 요약 정보**
        
        즉,
        
        - `ObstacleClusters` = 상세 원본 결과
        - `ObstacleModel` = planning/decision이 바로 쓰는 요약본
        
        이렇게 역할을 나누는 게 제일 좋아.
        
        ---
        
        # 2. 좌표계 기준
        
        이 부분이 제일 중요해.
        
        네가 말한 “Spot의 헤더 방향 기준”은 결국 **Spot 본체 기준 좌표계**로 해석해야 해.
        
        즉 PointCloud를 먼저 **`base_link` 프레임으로 변환**해야 해.
        
        `base_link` 기준 해석:
        
        - `+x` = Spot 정면
        - `+y` = Spot 왼쪽
        - `y` = Spot 오른쪽
        - `+z` = 위
        
        그러면 클러스터 중심점 `(x, y, z)`가 있을 때:
        
        - 거리 = `sqrt(x*x + y*y)`
        - 수평 각도 = `atan2(y, x)`
        
        이 수평 각도가 바로
        
        - `0`이면 정면
        - `+`면 왼쪽
        - 면 오른쪽
        
        이야.
        
        즉 네가 원하는
        
        “Spot 기준 오른쪽에 있는 장애물인지 판단”
        
        은 결국 **`base_link` 기준 클러스터 중심각**으로 해결된다.
        
        ---
        
        # 3. 새 메시지 구조 설계
        
        ## 3-1. `ObstacleCluster.msg`
        
        이건 **클러스터 하나**를 표현하는 메시지야.
        
        ```
        std_msgs/Header header
        
        uint32 cluster_id
        uint32 point_count
        
        float32 centroid_x
        float32 centroid_y
        float32 centroid_z
        
        float32 nearest_x
        float32 nearest_y
        float32 nearest_z
        
        float32 centroid_distance_xy
        float32 nearest_distance_xy
        
        float32 azimuth_angle_rad
        float32 elevation_angle_rad
        
        float32 bbox_size_x
        float32 bbox_size_y
        float32 bbox_size_z
        
        uint8 sector
        uint8 SECTOR_NONE=0
        uint8 SECTOR_FRONT=1
        uint8 SECTOR_LEFT=2
        uint8 SECTOR_RIGHT=3
        ```
        
        ### 각 필드 의미
        
        - `cluster_id`
        클러스터 식별자
        - `point_count`
        이 물체를 이루는 포인트 수
        - `centroid_x/y/z`
        클러스터 중심점
        - `nearest_x/y/z`
        Spot 기준 가장 가까운 포인트 좌표
        - `centroid_distance_xy`
        중심점까지의 수평 거리
        - `nearest_distance_xy`
        가장 가까운 점까지의 수평 거리
        → 충돌 판단엔 이 값이 더 중요
        - `azimuth_angle_rad`
        Spot 정면 기준 좌우 방향각
        - `elevation_angle_rad`
        위아래 각도
        - `bbox_size_x/y/z`
        물체 크기 대략치
        - `sector`
        이 물체가 front/left/right 중 어디에 소속되는지
        
        ---
        
        ## 3-2. `ObstacleClusters.msg`
        
        이건 전체 클러스터 목록이야.
        
        ```
        std_msgs/Header header
        ObstacleCluster[] clusters
        ```
        
        이 토픽은 디버깅, 시각화, 고도화 로직에 좋다.
        
        ---
        
        ## 3-3. 새 `ObstacleModel.msg`
        
        이건 요약본이야.
        
        추천은 **간단한 summary + 대표 클러스터 id 연결** 구조야.
        
        ```
        std_msgs/Header header
        
        bool front_detected
        bool left_detected
        bool right_detected
        
        float32 front_distance
        float32 left_distance
        float32 right_distance
        
        float32 front_angle_rad
        float32 left_angle_rad
        float32 right_angle_rad
        
        uint32 front_cluster_id
        uint32 left_cluster_id
        uint32 right_cluster_id
        
        bool obstacle_detected
        bool emergency_stop_required
        ```
        
        ### 이 구조를 추천하는 이유
        
        - planning/decision에서 바로 쓰기 편함
        - 메시지가 너무 무겁지 않음
        - 상세 정보는 `ObstacleClusters`에서 다시 찾을 수 있음
        
        즉:
        
        - `ObstacleModel` = 빠른 요약
        - `ObstacleClusters` = 자세한 근거 데이터
        
        ---
        
        # 4. 노드 구조 설계
        
        추천은 3단계야.
        
        ## 4-1. `pointcloud_preprocess_node`
        
        입력:
        
        - `/scan_3D`
        
        출력:
        
        - `/perception/lidar/points_filtered`
        
        역할:
        
        - `base_link` frame으로 transform
        - ROI crop
        - voxel downsampling
        - ground 제거
        - 노이즈 제거
        
        ---
        
        ## 4-2. `obstacle_cluster_node`
        
        입력:
        
        - `/perception/lidar/points_filtered`
        
        출력:
        
        - `/perception/lidar/obstacle_clusters`
        
        역할:
        
        - 3D 점군 클러스터링
        - 각 클러스터의 centroid / nearest point / bbox / angle 계산
        - sector 배정
        
        ---
        
        ## 4-3. `obstacle_model_node`
        
        입력:
        
        - `/perception/lidar/obstacle_clusters`
        
        출력:
        
        - `/perception/lidar/obstacle_model`
        
        역할:
        
        - front / left / right별 대표 클러스터 선택
        - summary 메시지 생성
        - emergency stop 판단
        
        ---
        
        # 5. 구현 과정 단계별 상세
        
        ## 단계 1. 입력 토픽과 프레임 확인
        
        먼저 `/scan_3D`의 frame이 뭔지 확인해야 해.
        
        해야 할 일:
        
        - `/scan_3D` topic type 확인
        - frame_id 확인
        - `base_link`로 TF 변환 가능한지 확인
        
        목표:
        
        - 모든 3D 점을 Spot 기준으로 해석 가능한 상태 만들기
        
        ---
        
        ## 단계 2. PointCloud2 → PCL point cloud 변환
        
        ROS2 C++에선 보통 이렇게 감.
        
        - `sensor_msgs::msg::PointCloud2`
        - `pcl::PointCloud<pcl::PointXYZ>` 또는 `PointXYZI`
        
        즉 구현 핵심:
        
        - `pcl_conversions`
        - `pcl::fromROSMsg(...)`
        
        ---
        
        ## 단계 3. `base_link` 기준으로 변환
        
        TF를 사용해서 point cloud를 `base_link`로 변환해.
        
        왜 중요하냐면:
        
        - 클러스터 angle이 Spot 기준으로 계산돼야 하니까
        
        이 단계가 되면
        
        이후 모든 거리/각도/섹터 판단이 “Spot 기준”이 된다.
        
        ---
        
        ## 단계 4. ROI 제한
        
        전체 point cloud를 그대로 쓰면 너무 많고 불필요해.
        
        예시 ROI:
        
        - `x`: `0.0 ~ 3.0 m`
        - `y`: `2.0 ~ 2.0 m`
        - `z`: `0.2 ~ 1.5 m`
        
        의미:
        
        - Spot 앞쪽 3m
        - 좌우 2m
        - 바닥 아래/너무 높은 점 제거
        
        ---
        
        ## 단계 5. Voxel downsampling
        
        점이 너무 많으면 클러스터링 비용이 크다.
        
        예:
        
        - voxel leaf size `0.03 ~ 0.05 m`
        
        즉 3~5cm 단위로 점을 줄여서 계산량 절감.
        
        ---
        
        ## 단계 6. 바닥 제거
        
        3D 클러스터링에서 바닥을 안 빼면 큰 바닥 클러스터가 생길 수 있어.
        
        방법 예:
        
        - RANSAC plane segmentation
        - 혹은 단순 `z` 필터 + 높이 조건
        
        처음엔 단순하게:
        
        - `z > -0.1`
        - `z < 1.5`
        
        로만 가도 된다.
        
        ---
        
        ## 단계 7. 3D 클러스터링
        
        이제 장애물 후보 점군으로 클러스터링.
        
        추천:
        
        - PCL Euclidean Cluster Extraction
        
        대표 파라미터 예:
        
        - cluster tolerance: `0.10 ~ 0.20 m`
        - min cluster size: `20`
        - max cluster size: `5000`
        
        결과:
        
        - 각 물체 단위 point index 묶음 생성
        
        ---
        
        ## 단계 8. 각 클러스터 descriptor 계산
        
        클러스터마다 아래 계산.
        
        ### 8-1. centroid
        
        ```
        cx =mean(x),cy =mean(y),cz =mean(z)
        ```
        
        ### 8-2. nearest point
        
        클러스터 내 각 점에 대해
        
        ```
        distance_xy =sqrt(x*x+y*y)
        ```
        
        최솟값 찾기
        
        ### 8-3. 대표 거리
        
        - 위치 표현: `centroid_distance_xy`
        - 안전 판단: `nearest_distance_xy`
        
        둘 다 저장 추천
        
        ### 8-4. 대표 각도
        
        ```
        azimuth =atan2(cy,cx)
        ```
        
        ### 8-5. 높이 각도
        
        ```
        elevation =atan2(cz,sqrt(cx*cx+cy*cy))
        ```
        
        ### 8-6. bbox 크기
        
        클러스터의 min/max xyz로 extents 계산
        
        ---
        
        ## 단계 9. 클러스터 sector 소속
        
        여기서 point가 아니라 **클러스터 중심각** 기준으로 섹터를 배정.
        
        예:
        
        - front: `20° ~ +20°`
        - left: `+20° ~ +90°`
        - right: `90° ~ -20°`
        
        판단 기준:
        
        - `azimuth_angle_rad`
        
        즉:
        
        - `atan2(cy, cx)`가 `35°`면 right
        - `+12°`면 front
        - `+45°`면 left
        
        이게 네가 원한 핵심 로직이야.
        
        ---
        
        ## 단계 10. `ObstacleClusters` publish
        
        지금 보이는 모든 물체를 리스트로 publish.
        
        이 단계가 있으면:
        
        - RViz 시각화
        - 디버깅
        - 다중 장애물 추적
        이 쉬워짐.
        
        ---
        
        ## 단계 11. 대표 클러스터 선택
        
        각 섹터별로 대표 하나만 고른다.
        
        추천 기준:
        
        - **가장 가까운 클러스터** (`nearest_distance_xy` 최소)
        
        즉:
        
        - front sector에 클러스터 3개 있으면
        → nearest_distance가 가장 작은 것 선택
        
        ---
        
        ## 단계 12. `ObstacleModel` 생성
        
        각 섹터 대표 클러스터로 summary 채우기.
        
        예:
        
        - `front_detected = true`
        - `front_distance = front_rep.nearest_distance_xy`
        - `front_angle_rad = front_rep.azimuth_angle_rad`
        - `front_cluster_id = front_rep.cluster_id`
        
        ---
        
        ## 단계 13. stop 판단
        
        추천:
        
        - front 대표 클러스터 또는 front sector 내 모든 클러스터 중
        - `nearest_distance_xy <= stop_threshold`
        이면 stop
        
        조금 더 안전하게 하려면:
        
        - front 중에서도 **central front zone** 따로 둬도 됨
        - 예: `10° ~ +10°`
        
        ---
        
        # 6. 구현 순서 추천
        
        ## 1차
        
        `ObstacleCluster.msg`, `ObstacleClusters.msg`, 새 `ObstacleModel.msg` 정의
        
        ## 2차
        
        `/scan_3D`를 `base_link`로 변환하는 전처리 노드 구현
        
        ## 3차
        
        ROI + voxel + 간단한 z filter 구현
        
        ## 4차
        
        Euclidean clustering 구현
        
        ## 5차
        
        각 클러스터의 centroid / nearest / angle / bbox 계산
        
        ## 6차
        
        `ObstacleClusters` publish
        
        ## 7차
        
        클러스터를 front/left/right로 배정
        
        ## 8차
        
        대표 클러스터 선택 후 `ObstacleModel` publish
        
        ## 9차
        
        RViz 검증 + 로그 검증
        
        ---
        
        # 7. 네가 기대하는 최종 동작 예시
        
        Spot이 정면을 보고 있고,
        
        오른쪽 앞에 박스가 있다고 치자.
        
        3D point cloud 처리 후:
        
        - 박스 점군이 하나의 클러스터로 생성
        - centroid = `(x=1.2, y=-0.6, z=0.4)`
        - `distance_xy = sqrt(1.2² + (-0.6)²)`
        - `azimuth = atan2(-0.6, 1.2) ≈ -26.6°`
        
        그럼:
        
        - sector = `right`
        - `right_detected = true`
        - `right_distance ≈ 1.34m`
        - `right_angle ≈ -26.6°`
        
        이렇게 나오는 거야.
        
        이게 네가 원한 로직하고 거의 정확히 같다.
        
        ---
        
        # 8. 지금까지 코드와의 관계
        
        지금까지 만든 `/scan` 기반 코드는 버리는 게 아니라,
        
        의미는 있었어.
        
        그건:
        
        - front/left/right 해석 개념 검증
        - threshold, sector 개념 검증
        - 로그 설계 감 익히기
        
        용도로 충분히 값어치 있었어.
        
        하지만 이제부터는 **진짜 목표 구조는 3D point cloud 기반**이 맞다.
        
        ---
        
        # 9. 최종 추천
        
        내 추천은 이거야.
        
        ### 메시지
        
        - `ObstacleCluster.msg`
        - `ObstacleClusters.msg`
        - 새 `ObstacleModel.msg`
        
        ### 노드
        
        - `pointcloud_preprocess_node`
        - `obstacle_cluster_node`
        - `obstacle_model_node`
        
        ### 기준 frame
        
        - 반드시 `base_link`
        
        ### 섹터 기준
        
        - 클러스터 중심각(`atan2(y, x)`)
        
        ### 대표값
        
        - 위치/방향 = centroid
        - 안전 판단 = nearest point
        
        ---
        
        ## 한 줄 결론
        
        맞아. 네가 말한 모델이 더 맞고, 앞으로는 **`/scan_3D` PointCloud2를 `base_link` 기준으로 변환한 뒤 3D 클러스터링을 수행하고, 각 클러스터의 centroid / nearest point / distance / azimuth를 계산해서 front/left/right에 배정한 결과를 `ObstacleModel`로 요약하는 구조**로 가는 게 맞다.
        

---

### 1-1. Cluster 분류

**기준 좌표계**

> 반드시 **Spot의 `base_link` 기준**으로 본다. 
즉 PointCloud를 먼저 `base_link` frame으로 변환한 뒤 계산한다.
> 

`base_link` 기준:

- `+x` = Spot 정면
- `+y` = Spot 왼쪽
- `y` = Spot 오른쪽

---

**각 클러스터의 대표 중심**

> 클러스터 중심점 `(centroid_x, centroid_y, centroid_z)`를 구한 뒤, 아래와 같이 계산
> 

```
azimuth =atan2(centroid_y,centroid_x);
```

이 값으로:

- `0 rad` = 정면, `+rad` = 왼쪽,  `rad` = 오른쪽 판단

---

**sector 분류 기준 예시**

- `front`: `20° ~ +20°`
- `left`: `+20° ~ +90°`
- `right`: `90° ~ -20°`
- 그 외는 `SECTOR_NONE`

의미 :

- Spot 정면 가까운 범위 = front
- 왼쪽 넓은 범위 = left
- 오른쪽 넓은 범위 = right

---

**분류 규칙**

예를 들어 클러스터 중심각이:

- `+12°` → front
- `35°` → right
- `+48°` → left
- `+120°` → none
(혹은 ROI 밖이므로 애초에 안 보게 설정)

즉 **점이 아니라 클러스터 중심각으로 분류**하는 것

---

**대표 Cluster 선별**

> 각 섹터에 들어간 여러 클러스터 중 가장 근접한 클러스터 선택
> 

예:

- front에 사람, 박스 두 개
- right에 의자 하나

이때 대표 클러스터는 **해당 섹터에서 `nearest_distance_xy`가 가장 작은 클러스터**를 선택

즉, 가장 가까운 물체를 대표로 본다

### 1-2. 구현 과정

#### 1️⃣ PointCloud 전처리 노드 구현(lidar_perception/pointcloud_preprocess_node)

```python
- base_link : Spot의 무게 중심
- LiDAR(laser_frame) :
    - 위치 : (x, y, z)          = (0.141, 0, 0.053)
    - 방향 : (roll, pitch, yaw) = (0, 0, 0)
```

- **LiDAR 배치 실측 자료**
    
    ![image.png](image%2023.png)
    
- **static TF와 cyglidar 토픽의 연관성**
    
    **[cyglidar가 하는 일]**
    
    - 센서 데이터(`/scan` , `/scan_2D` , `/scan_3D`)를 publish함.
    - **laser_frame 기준**
    
    ---
    
    **[static TF가 하는 일]**
    
    ROS에게 알려줌:
    
    - `laser_frame`은 `base_link` 기준으로 어디에 있다
    
    ---
    
    **[perception 노드가 하는 일]**
    
    원본 점군을 받아서
    
    - `laser_frame → base_link` 변환
    - Spot 기준 장애물 해석
    
    ---
    
    **[정리]**
    
    ```python
    - **cyglidar 드라이버** = 데이터 생성
    - **static TF**         = 좌표 관계 제공
    - **perception 노드**   = 데이터 해석/변환/가공
    ```
    

[static TF 설정] - static TF 띄우기 (임시, 추후 launch로 자동화 예정)

```python
ros2 run tf2_ros static_transform_publisher 0.14 0.00 0.05 0 0 0 base_link laser_frame
```

- [TF Frame 관리]
    
    **지금 단계**
    
    ```
    base_link -> laser_frame
    ```
    
    - Spot 기준 로컬 인식 개발
    - obstacle cluster / obstacle model 개발
    
    **나중 localization 붙을 때, 추가**
    
    ```
    map -> base_link
    ```
    
    **최종 완성**
    
    ```
    map -> base_link -> laser_frame
    ```
    
- launch 예시
    
    ```python
    from launch import LaunchDescription
    from launch_ros.actions import Node
    
    def generate_launch_description():
        return LaunchDescription([
            Node(
                package="tf2_ros",
                executable="static_transform_publisher",
                arguments=["0.10", "0.00", "0.08", "0", "0", "0", "base_link", "laser_frame"],
                output="screen",
            ),
            Node(
                package="cyglidar_d1_ros2",
                executable="D1_Node",
                output="screen",
            ),
            Node(
                package="lidar_perception",
                executable="pointcloud_preprocess_node",
                output="screen",
            ),
        ])
    ```
    
- **실행 방법**
    
    **터미널 1: cyglidar 드라이버 실행**
    
    ```
    cd ~/robot_ws
    source ~/robot_ws/install/setup.bash
    ros2 launch cyglidar_d1_ros2 cyglidar.launch.py
    
    # 드라이버 + RViz 실행
    ros2 launch cyglidar_d1_ros2 view_cyglidar.launch.py
    
    # RViz만 실행
    ros2 launch view_cyglidar.launch.py
    ```
    
    **터미널 2: preprocessing launch 실행**
    
    ```
    cd ~/robot_ws
    source ~/robot_ws/install/setup.bash
    ros2 launch lidar_perception lidar_preprocess.launch.py
    ```
    
    **터미널 3: 검증 과정 진행**
    
    static TF 확인 :
    
    ```python
    ros2 topic echo /tf_static --once
    ```
    
    filtered point cloud 토픽 확인 :
    
    ```python
    ros2 topic type /perception/lidar/points_filtered
    ros2 topic hz /perception/lidar/points_filtered
    ```
    
    **터미널 4: RViz 확인**
    
    RViz에서 PointCloud2 디스플레이를 추가하고, 아래의 토픽을 둔다
    
    ```
    rviz2 -d /home/jetson/.rviz2/lidar_preprocess.rviz
    /perception/lidar/points_filtered
    ```
    
    기대 결과 :
    
    - 원본보다 점 수가 줄어든다
    - ROI 범위 안의 점만 남는다
    - `base_link` 기준으로 해석된 점군이 보인다

---

#### **2️⃣ Obstacle Cluster 노드 구현 (lidar_perception/obstacle_cluster_node)**

- **[Debugging] 클러스터별 PointCloud2 시각화**
    - cluster 0 점군, cluster 1 점군, cluster 2 점군
    
    을 각각 다른 PointCloud2로 publish하거나, 한 PointCloud2에 RGB를 입혀서 cluster별 색을 다르게 만들어서 시각화하는 과정
    
    - **구현 과정**
        - `/perception/lidar/points_filtered` 수신
        - Euclidean clustering 수행
        - 각 cluster의 index 목록 확보
        - 새 `pcl::PointCloud<pcl::PointXYZRGB>` 생성
        - cluster별로 색을 정해서 점에 입힘
        - `sensor_msgs/msg/PointCloud2`로 변환
        - `/perception/lidar/clustered_points_colored` publish
    
    **[실행 결과]**
    
    ![image.png](image%2024.png)
    

---

#### 3️⃣ Obstacle Model 노드 구현 (lidar_perception/obstacle_model_node)

- nearest : 해당 sector의 대표 클러스터에서 가장 가까운 점까지의 **XY 평면거리 [m]**
- azimuth : **Spot 기준 수평 방향각 [rad] ⇒ +는 왼쪽 / -는 오른쪽 / 0은 정면**
- **Obstacle Model 기능**
    
    1. 입력 클러스터 읽기 : `/perception/lidar/obstacle_clusters` 구독
    
    2. sector별로 묶기 
    
    각 클러스터는 이미 `sector`가 있기 때문에, 
    
    - `SECTOR_FRONT` , `SECTOR_LEFT` , `SECTOR_RIGHT` 의 기준으로 나눈다.
    
    3. 각 섹터에서 **대표 클러스터 선택**
    
    추천 기준:
    
    - `nearest_distance_xy`가 가장 작은 클러스터
    
    즉:
    
    - front에서 제일 가까운 것 1개
    - left에서 제일 가까운 것 1개
    - right에서 제일 가까운 것 1개
    
    4. `ObstacleModel.msg` 채우기
    
    예:
    
    - `front.valid = true`
    - `front.nearest_distance_xy = ...`
    - `front.azimuth_angle_rad = ...`
    
    없으면:
    
    - `front.valid = false`
    
    5. stop 판단
    
    예:
    
    - front 대표 클러스터가 있고, `nearest_distance_xy <= stop_threshold`면
    - `emergency_stop_required = true`
- **`nearest_distance_xy` 를 기준으로 쓰는 이유**
    
    > xy는 수평 거리로, Spot이 2D 평면에서 이동하기 때문이다.
    > 
    
    ```python
    3D 거리 = sqrt(x² + y² + z²)   → 높이(z)까지 포함
    수평 거리 = sqrt(x² + y²)       → 수평 거리만
    ```
    
    - 천장에 매달린 물체가 있다면 3D 거리는 가깝지만, Spot이 실제로 부딪힐 위험은 없다.
    - 반대로 바닥에 낮게 깔린 장애물은 z값이 작아서 3D 거리 기준으로는 멀어 보일 수 있지만 실제로는 충돌 위험이 있다.
    - Spot은 수평 방향으로만 이동하기 때문에 충돌 위험 판단은 **수평 거리(`xy`)**로 봐야된다.
    
    ```python
    centroid_distance_xy  → 클러스터 중심까지 수평 거리 (위치/방향 판단용)
    nearest_distance_xy   → 클러스터에서 가장 가까운 점까지 수평 거리 (충돌 판단용)
    ```
    
    대표 클러스터 선택 기준으로 `nearest_distance_xy`를 쓰는 이유도 같습니다. 클러스터 중심보다 실제로 Spot에 가장 가까운 점이 충돌 위험을 더 정확하게 나타내기 때문입니다.
    
- **확장 기능 - traking**
    
    **왜 id와 색이 계속 바뀌냐?**
    
    > 지금 `id`는 **물체 고유번호**가 아니라, **그 프레임에서 정렬된 클러스터 순번**이기 때문
    > 
    
    ### 추적 관점
    
    나중에 : 
    
    - “저 왼쪽 물체가 계속 같은 물체다”
    - “저 front 물체를 프레임 사이에서 추적하겠다”
    
    →  **tracking** 필요
    
    예를 들면:
    
    - 이전 프레임 centroid와 현재 centroid를 매칭
    - 가장 가까운 클러스터를 같은 물체로 간주
    - 고정된 persistent id 부여
    
    즉 지금 `id`는 **persistent id가 아니라 frame-local id**라고 이해하면 된다.
    
- **실행 과정**
    
    **터미널 1: 드라이버 실행**
    
    ```
    ros2 launch cyglidar_d1_ros2 cyglidar.launch.py
    ```
    
    **터미널 2: 전처리 노드 실행**
    
    ```
    ros2 launch lidar_perception lidar_preprocess.launch.py
    ```
    
    **터미널 3: 클러스터 노드 실행**
    
    ```
    ros2 run lidar_perception obstacle_cluster_node--ros-args--params-file ~/robot_ws/src/perception/lidar_perception/config/obstacle_cluster.param.yaml
    ```
    
    **터미널 4: 모델 노드 실행**
    
    ```
    ros2 run lidar_perception obstacle_model_node
    ```
    
- 
    
    ## 단계 2. PointCloud 전처리 노드 구현
    
    새 노드 추천 이름:
    
    - `pointcloud_preprocess_node`
    
    입력:
    
    - `/scan_3D`
    
    출력:
    
    - `/perception/lidar/points_filtered`
    
    역할:
    
    1. `PointCloud2` 수신
    2. `base_link` frame으로 transform
    3. ROI crop
    4. voxel downsample
    5. ground / noise 제거
    
    ### 이 단계에서 필요한 기술
    
    - `tf2_ros`
    - `tf2_sensor_msgs`
    - `pcl_conversions`
    - `pcl::VoxelGrid`
    - 필요 시 `pcl::PassThrough`
    
    ---
    
    ## 단계 3. 3D 클러스터 생성 노드 구현
    
    새 노드 추천 이름:
    
    - `obstacle_cluster_node`
    
    입력:
    
    - `/perception/lidar/points_filtered`
    
    출력:
    
    - `/perception/lidar/obstacle_clusters`
    
    역할:
    
    1. `PointCloud2` → PCL 변환
    2. Euclidean clustering
    3. 각 클러스터 descriptor 계산
    4. `ObstacleClusters` publish
    
    ---
    
    ## 단계 4. 클러스터 descriptor 계산
    
    각 클러스터마다 아래를 계산해야 해.
    
    ### 4-1. 중심점
    
    ```
    centroid_x =mean(x)
    centroid_y =mean(y)
    centroid_z =mean(z)
    ```
    
    ### 4-2. 최근접 포인트
    
    클러스터 안에서 `sqrt(x*x + y*y)`가 최소인 포인트를 찾는다.
    
    ### 4-3. 대표 거리
    
    - `centroid_distance_xy = sqrt(cx*cx + cy*cy)`
    - `nearest_distance_xy = sqrt(nx*nx + ny*ny)`
    
    ### 4-4. 각도
    
    ```
    azimuth_angle_rad   =atan2(centroid_y,centroid_x)
    elevation_angle_rad =atan2(centroid_z,sqrt(cx*cx+cy*cy))
    ```
    
    ### 4-5. bbox 크기
    
    클러스터 내 min/max xyz를 구해서:
    
    - `bbox_size_x = max_x - min_x`
    - `bbox_size_y = max_y - min_y`
    - `bbox_size_z = max_z - min_z`
    
    ---
    
    ## 단계 5. cluster를 섹터에 배정
    
    `azimuth_angle_rad`를 degree로 바꿔서 판단해도 되고,
    
    내부는 rad 그대로 써도 된다.
    
    예:
    
    ```
    if (-20deg<=azimuth<=+20deg) {
    sector =FRONT;
    }
    elseif (+20deg<azimuth<=+90deg) {
    sector =LEFT;
    }
    elseif (-90deg<=azimuth<-20deg) {
    sector =RIGHT;
    }
    else {
    sector =NONE;
    }
    ```
    
    ### 중요
    
    여기서 sector는 **클러스터 전체에 하나만 부여**한다.
    
    즉 예전처럼 point가 front/right 동시에 걸리는 문제가 줄어든다.
    
    ---
    
    ## 단계 6. `ObstacleClusters` publish
    
    각 클러스터에 대해:
    
    - `cluster_id`
    - `point_count`
    - `centroid_*`
    - `nearest_*`
    - `distance`
    - `azimuth`
    - `bbox`
    - `sector`
    
    를 채워서 배열로 publish.
    
    이 단계까지 가면
    
    **현재 주변 장애물 형상들을 3D 물체 단위로 볼 수 있게 된다.**
    
    ---
    
    ## 단계 7. `ObstacleModel` 생성 노드 구현
    
    새 노드 추천 이름:
    
    - `obstacle_model_node`
    
    입력:
    
    - `/perception/lidar/obstacle_clusters`
    
    출력:
    
    - `/perception/lidar/obstacle_model`
    
    역할:
    
    1. front / left / right별로 클러스터 필터링
    2. 각 섹터에서 대표 클러스터 선택
    3. `ObstacleModel` 요약 메시지 생성
    
    ---
    
    ## 단계 8. 대표 클러스터 선택 규칙
    
    각 섹터에 대해:
    
    - `nearest_distance_xy`가 가장 작은 클러스터를 대표로 선택
    
    예:
    
    - front 클러스터가 3개라면
    → 가장 가까운 것 1개를 `front`에 넣는다
    
    ---
    
    ## 단계 9. `ObstacleModel` 채우기
    
    예를 들어 front 대표 클러스터가 있으면:
    
    - `front.valid = true`
    - `front.cluster_id = ...`
    - `front.centroid_x/y/z = ...`
    - `front.nearest_distance_xy = ...`
    - `front.azimuth_angle_rad = ...`
    - `front.sector = FRONT`
    
    없으면:
    
    - `front.valid = false`
    
    left/right도 동일.
    
    그리고:
    
    - `obstacle_detected = front.valid || left.valid || right.valid`
    
    ---
    
    ## 단계 10. `emergency_stop_required`
    
    추천 기준:
    
    - `front.valid == true`
    - `front.nearest_distance_xy <= stop_threshold`
    
    이면 `true`
    
    예:
    
    - `stop_threshold = 0.30m`
    
    필요하면 더 엄격하게
    
    - front 안에서도 central front zone만 따로 두는 방식으로 확장 가능
    
    ---
    
    # 6. 코드/패키지 수정 범위 정리
    
    ## `robot_interfaces`
    
    수정 파일:
    
    - `msg/ObstacleCluster.msg`
    - `msg/ObstacleClusters.msg`
    - `msg/ObstacleModel.msg`
    - `package.xml`
    - `CMakeLists.txt`
    
    ---
    
    ## `perception/lidar_perception`
    
    앞으로 새로 생길 가능성이 높은 파일:
    
    - `src/pointcloud_preprocess_node.cpp`
    - `src/obstacle_cluster_node.cpp`
    - `src/obstacle_model_node.cpp`
    
    헤더:
    
    - `include/lidar_perception/pointcloud_preprocess_node.hpp`
    - `include/lidar_perception/obstacle_cluster_node.hpp`
    - `include/lidar_perception/obstacle_model_node.hpp`
    - 공통 유틸: `include/lidar_perception/pointcloud_utils.hpp`
    
    ---
    
    # 7. 내가 추천하는 구현 순서
    
    지금 당장 제일 좋은 순서는 이거야.
    
    ### 1
    
    `robot_interfaces`에 새 메시지 3개 정의
    
    ### 2
    
    메시지 패키지 빌드 및 확인
    
    ### 3
    
    `/scan_3D`를 `base_link`로 변환하는 전처리 노드 구현
    
    ### 4
    
    간단한 ROI 필터와 voxel downsample 구현
    
    ### 5
    
    Euclidean clustering 구현
    
    ### 6
    
    `ObstacleClusters` publish
    
    ### 7
    
    그 다음 `ObstacleModel` 요약 생성
    
    즉 **메시지 먼저 → 클러스터 노드 → 모델 노드** 순서가 가장 자연스럽다.
    

### 1-3. 파라미터 튜닝

#### 1️⃣ LiDAR ROI 튜닝

> Spot(base_link) 기준으로 어디 범위까지 인식할 것인가
> 

```python
 pointcloud_preprocess_node:
  ros__parameters:
    # 최종적으로 Spot 기준으로 해석할 것이므로 target_frame은 base_link
    target_frame: "base_link"

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

    # Voxel downsampling leaf size
    voxel_leaf_size: 0.05
```

- 결과
    
    #### **[X(0 ~ 3.0), Y(-2.0 ~ 2.0), Z(-0.2 ~ 1.5)]**
    
    ![image.png](image%2025.png)
    
    #### **[X(0.35 ~ 1.5), Y(-1.0 ~ 1.0), Z(0.05 ~ 1.0)]**
    
    ![image.png](image%2026.png)
    

---

## 2. local_obstacle_points_node

### 2-1. 이 노드가 해야 하는 일

`/points_filtered`는 이미 base_link 기준으로 변환된 PointCloud다.

그런데 이 안에는 아직 높이 방향 정보가 포함되어 있다.

`local_obstacle_points_node`에서는 다음만 한다.

```
1. /perception/lidar/points_filtered subscribe
2. 각 point의 x, y, z 확인
3. local 환경 모델에 필요한 점만 필터링
4. 필요 없는 z 영역 제거
5. 너무 가까운 노이즈 제거
6. 너무 먼 점 제거
7. /perception/lidar/local_obstacle_points publish
```

### 2-2. 파라미터 설계

파일:

```
robot_ws/src/perception/lidar_perception/config/local_obstacle_points.param.yaml
```

내용:

```
local_obstacle_points_node:
  ros__parameters:
    input_topic:"/perception/lidar/points_filtered"
    output_topic:"/perception/lidar/local_obstacle_points"

# base_link 기준 관심 영역
    min_x: -0.5
    max_x: 4.0
    min_y: -2.5
    max_y: 2.5

# 장애물로 볼 z 범위
# 바닥 노이즈를 줄이기 위해 너무 낮은 점은 제거
    min_z: -0.10
    max_z: 1.20

# 로봇 중심 너무 가까운 점 제거
    min_range: 0.10
    max_range: 4.50

# 2D 환경 모델로 사용할 것이므로 z를 0으로 평면화할지 여부
    flatten_z: true
```

주의할 점은 `min_z`다.

CygLiDAR D1에서 바닥점이 많이 들어오면 occupancy grid가 전부 막힌 것처럼 보일 수 있다.

따라서 처음에는 `min_z: -0.10` 정도로 두고 RViz에서 확인하면서 조정하면 된다.

### 2-3. 코드 추가 위치

새 파일 생성:

```
cd ~/robot_ws/src/perception/lidar_perception/src
touch local_obstacle_points_node.cpp
```

파일:

```
robot_ws/src/perception/lidar_perception/src/local_obstacle_points_node.cpp
```

- 코드:
    
    ```
    #include<memory>
    #include<string>
    #include<vector>
    #include<cmath>
    
    #include"rclcpp/rclcpp.hpp"
    #include"sensor_msgs/msg/point_cloud2.hpp"
    
    #include<pcl/point_types.h>
    #include<pcl/point_cloud.h>
    #include<pcl_conversions/pcl_conversions.h>
    
    classLocalObstaclePointsNode :public rclcpp::Node
    {
    public:
    LocalObstaclePointsNode()
      : Node("local_obstacle_points_node")
      {
    // -----------------------------
    // 1. Parameters
    // -----------------------------
    input_topic_ =this->declare_parameter<std::string>(
    "input_topic",
    "/perception/lidar/points_filtered"
        );
    
    output_topic_ =this->declare_parameter<std::string>(
    "output_topic",
    "/perception/lidar/local_obstacle_points"
        );
    
    min_x_ =this->declare_parameter<double>("min_x",-0.5);
    max_x_ =this->declare_parameter<double>("max_x",4.0);
    min_y_ =this->declare_parameter<double>("min_y",-2.5);
    max_y_ =this->declare_parameter<double>("max_y",2.5);
    min_z_ =this->declare_parameter<double>("min_z",-0.10);
    max_z_ =this->declare_parameter<double>("max_z",1.20);
    
    min_range_ =this->declare_parameter<double>("min_range",0.10);
    max_range_ =this->declare_parameter<double>("max_range",4.50);
    
    flatten_z_ =this->declare_parameter<bool>("flatten_z",true);
    
    // -----------------------------
    // 2. Publisher / Subscriber
    // -----------------------------
    pub_ =this->create_publisher<sensor_msgs::msg::PointCloud2>(
    output_topic_,
          rclcpp::SensorDataQoS()
        );
    
    sub_ =this->create_subscription<sensor_msgs::msg::PointCloud2>(
    input_topic_,
          rclcpp::SensorDataQoS(),
          std::bind(&LocalObstaclePointsNode::pointCloudCallback,this, std::placeholders::_1)
        );
    
    RCLCPP_INFO(this->get_logger(),"local_obstacle_points_node started.");
    RCLCPP_INFO(this->get_logger(),"Subscribe: %s",input_topic_.c_str());
    RCLCPP_INFO(this->get_logger(),"Publish  : %s",output_topic_.c_str());
      }
    
    private:
    void pointCloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtrmsg)
      {
    // -----------------------------
    // 1. ROS PointCloud2 -> PCL 변환
    // -----------------------------
        pcl::PointCloud<pcl::PointXYZ>::Ptrinput_cloud(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::fromROSMsg(*msg, *input_cloud);
    
        pcl::PointCloud<pcl::PointXYZ>::Ptroutput_cloud(new pcl::PointCloud<pcl::PointXYZ>());
    output_cloud->header =input_cloud->header;
    
    // -----------------------------
    // 2. local obstacle point filtering
    // -----------------------------
    for (constauto &pt :input_cloud->points)
        {
    // NaN / Inf 제거
    if (!std::isfinite(pt.x)||!std::isfinite(pt.y)||!std::isfinite(pt.z)) {
    continue;
          }
    
    // ROI 필터링
    if (pt.x<min_x_||pt.x>max_x_) {
    continue;
          }
    
    if (pt.y<min_y_||pt.y>max_y_) {
    continue;
          }
    
    if (pt.z<min_z_||pt.z>max_z_) {
    continue;
          }
    
    // 거리 기반 필터링
    constdoublerange_xy = std::sqrt(pt.x*pt.x+pt.y*pt.y);
    
    if (range_xy<min_range_||range_xy>max_range_) {
    continue;
          }
    
          pcl::PointXYZfiltered_pt;
    filtered_pt.x =pt.x;
    filtered_pt.y =pt.y;
    
    // local occupancy grid는 2D 기반이므로 필요하면 z를 0으로 평면화
    if (flatten_z_) {
    filtered_pt.z =0.0;
          }else {
    filtered_pt.z =pt.z;
          }
    
    output_cloud->points.push_back(filtered_pt);
        }
    
    output_cloud->width =static_cast<uint32_t>(output_cloud->points.size());
    output_cloud->height =1;
    output_cloud->is_dense =false;
    
    // -----------------------------
    // 3. PCL -> ROS PointCloud2 변환 후 publish
    // -----------------------------
        sensor_msgs::msg::PointCloud2output_msg;
        pcl::toROSMsg(*output_cloud,output_msg);
    
    // points_filtered가 이미 base_link 기준이면 frame_id도 그대로 유지
    output_msg.header =msg->header;
    
    pub_->publish(output_msg);
      }
    
    private:
      std::string input_topic_;
      std::string output_topic_;
    
    double min_x_;
    double max_x_;
    double min_y_;
    double max_y_;
    double min_z_;
    double max_z_;
    double min_range_;
    double max_range_;
    
    bool flatten_z_;
    
      rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub_;
      rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr sub_;
    };
    
    intmain(intargc,char **argv)
    {
      rclcpp::init(argc,argv);
      rclcpp::spin(std::make_shared<LocalObstaclePointsNode>());
      rclcpp::shutdown();
    return0;
    }
    ```
    

---

## 3. local_occupancy_grid_node

- 토픽 경로 : `/perception/lidar/local_occupancy_grid`
- 메시지 타입 : `nav_msgs/msg/OccupancyGrid`

![image.png](image%2027.png)

 `/perception/lidar/local_occupancy_grid`의 정의 :

```
base_link 기준 로컬 2D 장애물 점유 지도
```

의미:

```
Spot 기준 전방/좌우 일정 범위 안에서
장애물로 판단된 LiDAR point가 존재하는 cell을 occupied로 표시한 지도
```

즉, 이건 global map도 아니고, SLAM 지도도 아니고, localization 결과도 아니야.

```
X 공통좌표계 지도
X SLAM map
X GPS 대체 위치 추정 결과

O Spot 기준 주변 장애물 지도
O FreeSpaceModel/GapModel의 입력
O local planning/costmap으로 확장 가능한 중간 표현
```

- **OccupancyGrid란?**
    
    > **로봇 주변 공간을 격자 칸으로 나누고, 각 칸이 비어 있는지/막혀 있는지를 표현하는 2D 지도**
    > 
    
    LiDAR point cloud는 원래 이런 형태야.
    
    ```
    점 1: x=1.24, y=0.31, z=0.42
    점 2: x=1.31, y=0.35, z=0.38
    점 3: x=2.02, y=-0.80, z=0.55
    ...
    ```
    
    즉, “점들의 집합”이지.
    
    그런데 planning이나 free space 판단에서는 매번 point 하나하나를 직접 보기보다, 공간을 격자로 나눠서 보는 게 편하다.
    
    예를 들어 로봇 주변을 이렇게 나눈다고 해보자.
    
    ```
    x 방향: -1.0m ~ 4.0m
    y 방향: -2.5m ~ 2.5m
    해상도: 0.10m
    ```
    
    그러면 10cm × 10cm짜리 칸들이 만들어진다.
    
    ```
    [ ][ ][ ][ ][ ][ ][ ]
    [ ][ ][X][X][ ][ ][ ]
    [ ][ ][X][ ][ ][ ][ ]
    [ ][ ][ ][ ][ ][ ][ ]
    ```
    
    여기서 `X`는 장애물이 있는 칸이다. 이런 2D 격자 지도가 바로 `nav_msgs/msg/OccupancyGrid`다.
    
    ---
    
    **[OccupancyGrid의 값 의미]**
    
    | 값 | 의미 |
    | --- | --- |
    | `-1` | unknown, 모름 |
    | `0` | free, 비어 있음 |
    | `100` | occupied, 장애물 있음 |
    
    ```
    전체 grid를 free = 0으로 초기화
    LiDAR point가 찍힌 cell만 occupied = 100으로 표시
    ```
    
    즉, 아직 `unknown = -1`은 적극적으로 쓰지 않는다.
    
    이유는 현재 목적이 SLAM 지도 작성이 아니라, **로봇 주변 로컬 환경 모델 생성**이기 때문
    
    ---
    
    **[RViz 디버깅용]**
    
    PointCloud는 점으로 보이지만, OccupancyGrid는 “공간이 막혔는지”가 훨씬 직관적으로 보인다.
    
    ```
    PointCloud:
    장애물 점들이 흩어져 보임
    
    OccupancyGrid:
    어느 영역이 막혔는지 칸 단위로 보임
    ```
    
    RViz에서는 `Map` display로 확인할 수 있다
    

### 3-1. 이 노드가 해야 하는 일

입력:

```
/perception/lidar/local_obstacle_points
```

출력:

```
/perception/lidar/local_occupancy_grid
```

이 노드는 PointCloud를 2D grid로 바꾼다.

예를 들어 grid 설정을 이렇게 둔다.

```
x: -1.0m ~ 4.0m
y: -2.5m ~ 2.5m
resolution: 0.10m
```

그러면 grid 크기는:

```
width  = 5.0 / 0.10 = 50 cell
height = 5.0 / 0.10 = 50 cell
```

각 point에 대해:

```
grid_x = (point.x - origin_x) / resolution
grid_y = (point.y - origin_y) / resolution
```

으로 cell index를 계산하고, 해당 cell을 occupied로 표시한다.

### 3-2. OccupancyGrid 값 규칙

`nav_msgs/msg/OccupancyGrid`는 보통 이렇게 쓴다.

| 값 | 의미 |
| --- | --- |
| `-1` | unknown |
| `0` | free |
| `100` | occupied |

이번 MVP에서는 단순하게 이렇게 가면 된다.

```
처음 전체 cell = 0
장애물 점이 찍힌 cell = 100
```

즉, unknown은 아직 쓰지 않는다.

이유는 지금 목표가 SLAM 수준의 지도 작성이 아니라,

**로봇 주변의 로컬 장애물 분포를 보는 것**이기 때문이다.

### 3-3. 파라미터 파일

파일 생성:

```
cd ~/robot_ws/src/perception/lidar_perception/config
touch local_occupancy_grid.param.yaml
```

파일:

```
robot_ws/src/perception/lidar_perception/config/local_occupancy_grid.param.yaml
```

내용:

```
local_occupancy_grid_node:
  ros__parameters:
    input_topic:"/perception/lidar/local_obstacle_points"
    output_topic:"/perception/lidar/local_occupancy_grid"

    frame_id:"base_link"

# base_link 기준 local map 영역
    origin_x: -1.0
    origin_y: -2.5

    size_x: 5.0
    size_y: 5.0

    resolution: 0.10

    occupied_value: 100
    free_value: 0

# 장애물 cell 주변을 조금 부풀릴지 여부
# 로봇 크기/안전 여유 반영용
    inflate_obstacles: true
    inflation_radius: 0.15
```

### 3-4. 코드 추가 위치

새 파일 생성:

```
cd ~/robot_ws/src/perception/lidar_perception/src
touch local_occupancy_grid_node.cpp
```

파일:

```
robot_ws/src/perception/lidar_perception/src/local_occupancy_grid_node.cpp
```

- 코드:
    
    ```
    #include<memory>
    #include<string>
    #include<vector>
    #include<cmath>
    #include<algorithm>
    
    #include"rclcpp/rclcpp.hpp"
    #include"sensor_msgs/msg/point_cloud2.hpp"
    #include"nav_msgs/msg/occupancy_grid.hpp"
    
    #include<pcl/point_types.h>
    #include<pcl/point_cloud.h>
    #include<pcl_conversions/pcl_conversions.h>
    
    classLocalOccupancyGridNode :public rclcpp::Node
    {
    public:
    LocalOccupancyGridNode()
      : Node("local_occupancy_grid_node")
      {
    // -----------------------------
    // 1. Parameters
    // -----------------------------
    input_topic_ =this->declare_parameter<std::string>(
    "input_topic",
    "/perception/lidar/local_obstacle_points"
        );
    
    output_topic_ =this->declare_parameter<std::string>(
    "output_topic",
    "/perception/lidar/local_occupancy_grid"
        );
    
    frame_id_ =this->declare_parameter<std::string>("frame_id","base_link");
    
    origin_x_ =this->declare_parameter<double>("origin_x",-1.0);
    origin_y_ =this->declare_parameter<double>("origin_y",-2.5);
    
    size_x_ =this->declare_parameter<double>("size_x",5.0);
    size_y_ =this->declare_parameter<double>("size_y",5.0);
    
    resolution_ =this->declare_parameter<double>("resolution",0.10);
    
    occupied_value_ =this->declare_parameter<int>("occupied_value",100);
    free_value_ =this->declare_parameter<int>("free_value",0);
    
    inflate_obstacles_ =this->declare_parameter<bool>("inflate_obstacles",true);
    inflation_radius_ =this->declare_parameter<double>("inflation_radius",0.15);
    
    width_ =static_cast<int>(std::ceil(size_x_/resolution_));
    height_ =static_cast<int>(std::ceil(size_y_/resolution_));
    
    inflation_cells_ =static_cast<int>(std::ceil(inflation_radius_/resolution_));
    
    // -----------------------------
    // 2. Publisher / Subscriber
    // -----------------------------
    pub_ =this->create_publisher<nav_msgs::msg::OccupancyGrid>(
    output_topic_,
          rclcpp::QoS(10)
        );
    
    sub_ =this->create_subscription<sensor_msgs::msg::PointCloud2>(
    input_topic_,
          rclcpp::SensorDataQoS(),
          std::bind(&LocalOccupancyGridNode::pointCloudCallback,this, std::placeholders::_1)
        );
    
    RCLCPP_INFO(this->get_logger(),"local_occupancy_grid_node started.");
    RCLCPP_INFO(this->get_logger(),"Subscribe: %s",input_topic_.c_str());
    RCLCPP_INFO(this->get_logger(),"Publish  : %s",output_topic_.c_str());
    RCLCPP_INFO(this->get_logger(),"Grid size: width=%d, height=%d, resolution=%.3f",
    width_,height_,resolution_);
      }
    
    private:
    void pointCloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtrmsg)
      {
    // -----------------------------
    // 1. ROS PointCloud2 -> PCL 변환
    // -----------------------------
        pcl::PointCloud<pcl::PointXYZ>::Ptrcloud(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::fromROSMsg(*msg, *cloud);
    
    // -----------------------------
    // 2. OccupancyGrid 메시지 기본 설정
    // -----------------------------
        nav_msgs::msg::OccupancyGridgrid_msg;
    
    grid_msg.header.stamp =this->now();
    grid_msg.header.frame_id =frame_id_;
    
    grid_msg.info.resolution =resolution_;
    grid_msg.info.width =static_cast<uint32_t>(width_);
    grid_msg.info.height =static_cast<uint32_t>(height_);
    
    // base_link 기준 local grid의 왼쪽 아래 origin
    grid_msg.info.origin.position.x =origin_x_;
    grid_msg.info.origin.position.y =origin_y_;
    grid_msg.info.origin.position.z =0.0;
    
    grid_msg.info.origin.orientation.x =0.0;
    grid_msg.info.origin.orientation.y =0.0;
    grid_msg.info.origin.orientation.z =0.0;
    grid_msg.info.origin.orientation.w =1.0;
    
    // MVP에서는 전체를 free로 초기화
    grid_msg.data.assign(width_*height_,static_cast<int8_t>(free_value_));
    
    // -----------------------------
    // 3. PointCloud point를 grid cell로 변환
    // -----------------------------
    for (constauto &pt :cloud->points)
        {
    if (!std::isfinite(pt.x)||!std::isfinite(pt.y)) {
    continue;
          }
    
    intgrid_x =static_cast<int>((pt.x-origin_x_)/resolution_);
    intgrid_y =static_cast<int>((pt.y-origin_y_)/resolution_);
    
    if (!isInsideGrid(grid_x,grid_y)) {
    continue;
          }
    
    if (inflate_obstacles_) {
    markInflatedOccupied(grid_msg,grid_x,grid_y);
          }else {
    markOccupied(grid_msg,grid_x,grid_y);
          }
        }
    
    // -----------------------------
    // 4. publish
    // -----------------------------
    pub_->publish(grid_msg);
      }
    
    bool isInsideGrid(intgrid_x,intgrid_y)const
      {
    returngrid_x>=0&&grid_x<width_&&grid_y>=0&&grid_y<height_;
      }
    
    int toIndex(intgrid_x,intgrid_y)const
      {
    returngrid_y*width_+grid_x;
      }
    
    void markOccupied(nav_msgs::msg::OccupancyGrid &grid_msg,intgrid_x,intgrid_y)
      {
    if (!isInsideGrid(grid_x,grid_y)) {
    return;
        }
    
    constintindex =toIndex(grid_x,grid_y);
    grid_msg.data[index] =static_cast<int8_t>(occupied_value_);
      }
    
    void markInflatedOccupied(nav_msgs::msg::OccupancyGrid &grid_msg,intcenter_x,intcenter_y)
      {
    for (intdy =-inflation_cells_;dy<=inflation_cells_;++dy)
        {
    for (intdx =-inflation_cells_;dx<=inflation_cells_;++dx)
          {
    constintnx =center_x+dx;
    constintny =center_y+dy;
    
    if (!isInsideGrid(nx,ny)) {
    continue;
            }
    
    constdoubledist = std::sqrt(
              std::pow(dx*resolution_,2.0)+
              std::pow(dy*resolution_,2.0)
            );
    
    // 원형 inflation 적용
    if (dist<=inflation_radius_) {
    markOccupied(grid_msg,nx,ny);
            }
          }
        }
      }
    
    private:
      std::string input_topic_;
      std::string output_topic_;
      std::string frame_id_;
    
    double origin_x_;
    double origin_y_;
    double size_x_;
    double size_y_;
    double resolution_;
    
    int occupied_value_;
    int free_value_;
    
    bool inflate_obstacles_;
    double inflation_radius_;
    
    int width_;
    int height_;
    int inflation_cells_;
    
      rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr pub_;
      rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr sub_;
    };
    
    intmain(intargc,char **argv)
    {
      rclcpp::init(argc,argv);
      rclcpp::spin(std::make_shared<LocalOccupancyGridNode>());
      rclcpp::shutdown();
    return0;
    }
    ```
    

### 3-5. 결과

**[inflate_obstacles = true]**

![image.png](image%2028.png)

**[inflate_obstacles = false]**

![image.png](image%2029.png)

### 3-6. local_occupancy_grid의 활용도

#### 이걸 어디에 활용하나?

지금 만드는 `local_occupancy_grid`는 다음 단계들의 기반 데이터야.

#### **1) FreeSpaceModel**

가장 직접적인 활용은 `FreeSpaceModel`이야.

```
/perception/lidar/local_occupancy_grid
        ↓
free_space_model_node
        ↓
/perception/lidar/free_space_model
```

여기서 계산할 수 있는 것:

```
front_free
left_free
right_free

front_clearance
left_clearance
right_clearance

front_blocked
left_blocked
right_blocked
```

예를 들어 전방 영역의 grid cell을 검사해서:

```
전방 1.5m 이내 occupied cell 있음 → front_blocked = true
전방 가장 가까운 장애물까지 1.2m → front_clearance = 1.2
```

이런 식으로 요약할 수 있다.

---

#### 2) GapModel

두 번째 활용은 `GapModel`이야.

```
/perception/lidar/local_occupancy_grid
        ↓
gap_model_node
        ↓
/perception/lidar/gap_model
```

여기서는 이런 걸 본다.

```
장애물 사이에 빈 공간이 있는가?
그 빈 공간의 폭이 Spot이 지나갈 만큼 넓은가?
왼쪽 gap이 나은가, 오른쪽 gap이 나은가?
```

PointCloud에서 바로 gap을 찾으면 점 밀도나 노이즈에 민감한데, OccupancyGrid로 바꾸면 cell 단위로 판단할 수 있어서 더 안정적이다.

예:

```
전방 sector를 각도/열 단위로 스캔
occupied cell 사이의 free cell 연속 구간 탐색
free 구간 폭 계산
로봇 폭 + safety margin보다 크면 gap 후보
```

---

#### 3) Local Planning / 회피 후보 평가

나중에 planning으로 넘어가면, local path 후보를 평가할 수 있다.

예를 들어 후보 경로가 여러 개 있다고 해보자.

```
후보 A: 직진
후보 B: 좌측 20도 회피
후보 C: 우측 20도 회피
```

각 후보 경로가 지나가는 grid cell을 검사해서:

```
occupied cell과 충돌하는가?
장애물과 얼마나 떨어져 있는가?
free cell이 충분히 이어지는가?
```

를 계산할 수 있다.

즉, OccupancyGrid는 나중에 **local costmap** 비슷한 역할로 확장될 수 있다.

---

#### 4) RViz 디버깅

PointCloud만 보면 이런 문제가 있어.

```
점이 흩어져 보여서 실제로 어느 공간이 막혔는지 직관적으로 보기 어려움
```

OccupancyGrid는 이렇게 보인다.

```
이 영역은 막힘
이 영역은 비어 있음
이 영역은 통과 가능해 보임
```

그래서 디버깅할 때도 좋다.

지금 RViz에서 Map으로 본 화면은:

```
회색 영역 = free로 초기화된 grid
검은 영역 = occupied cell
빨간 점 = PointCloud overlay
```

이렇게 해석하면 된다.

---

#### 기존 ObstacleModel과 뭐가 다른가?

이 부분이 중요해.

현재 `ObstacleModel`은 대표 장애물 요약이야.

```
front 대표 장애물 1개
left 대표 장애물 1개
right 대표 장애물 1개
```

즉, 이런 질문에 답한다.

```
전방에 가장 가까운 장애물이 있는가?
왼쪽에 대표 장애물이 있는가?
오른쪽에 대표 장애물이 있는가?
```

반면 `local_occupancy_grid`는 공간 전체를 cell 단위로 표현한다.

```
로봇 주변 x-y 공간에서 어느 cell이 막혔는가?
```

비교하면:

| 항목 | ObstacleModel | local_occupancy_grid |
| --- | --- | --- |
| 표현 방식 | front/left/right 대표값 | 2D grid 전체 |
| 정보량 | 적음 | 많음 |
| 목적 | 장애물 요약 | 공간 점유 상태 표현 |
| 후속 활용 | 간단한 위험/상태 판단 | FreeSpace, Gap, Planning |
| 예시 | front distance = 0.8m | x-y cell별 occupied/free |

즉, `ObstacleModel`은 “요약 리포트”이고, `OccupancyGrid`는 “공간 지도”야.

---

### 왜 PointCloud를 그대로 쓰지 않나?

PointCloud를 그대로 써도 가능은 해.

하지만 후속 판단 로직이 복잡해진다.

예를 들어 “전방 1m 안이 막혔는가?”를 PointCloud로 판단하려면 매번 모든 점을 돌면서 계산해야 해.

```
모든 point에 대해:
  x 범위 확인
  y 범위 확인
  거리 계산
  threshold 비교
```

하지만 OccupancyGrid로 바꾸면:

```
전방 영역에 해당하는 cell index만 검사
occupied cell이 있는지 확인
```

으로 훨씬 구조화된다.

장점은 다음과 같아.

```
1. FreeSpaceModel 계산이 쉬워짐
2. GapModel 계산이 쉬워짐
3. 로봇 폭/safety margin 반영이 쉬워짐
4. RViz 디버깅이 직관적임
5. 나중에 local costmap으로 확장하기 쉬움
```

---

#### 그럼 3D 정보는 아예 의미 없나?

아니야. 3D 정보는 **필터링 단계에서 중요하게 사용된다.**

예를 들어 z 값을 보고:

```
바닥점은 제거
너무 높은 점은 제거
로봇이 충돌할 가능성이 있는 높이의 점만 사용
```

할 수 있다.

현재 yaml에 있는 이 값들이 그 역할을 한다.

```
min_z: -0.10
max_z: 1.20
```

즉, 3D LiDAR의 z 정보는 “장애물 후보를 선별하는 데” 쓰고, 최종 이동 판단은 x-y 2D grid에서 하는 구조야.

나중에 필요하면 3D 정보를 더 살려서 이런 확장도 가능하다.

```
낮은 장애물
높은 장애물
머리 위 장애물
바닥 단차
경사/계단 후보
```

하지만 지금 MVP에서는 우선 2D 로컬 장애물 grid가 맞다.

### 3-7. 2차 개선도 : ray tracking

현재 방식:

```
전체 cell = free
LiDAR point가 있는 cell = occupied
```

개선 방식:

```
전체 cell = unknown
LiDAR ray가 지나간 cell = free
LiDAR point가 찍힌 cell = occupied
```

즉, 이제부터는 이런 의미가 생긴다.

| 값 | 의미 |
| --- | --- |
| `-1` | unknown, 아직 관측하지 못함 |
| `0` | free, LiDAR ray가 지나가서 비어 있다고 판단 |
| `100` | occupied, LiDAR point가 존재하는 장애물 cell |

이 방식이 FreeSpaceModel에 더 적합하다. 왜냐하면 `free`와 `unknown`을 구분할 수 있기 때문이야.

---

![image.png](image%2030.png)

- 분석 내용
    
    ```bash
    청록/회녹색 넓은 영역
    = unknown 영역일 가능성이 큼
    = 아직 LiDAR ray가 지나가지 않은 cell
    
    흰색 또는 밝은 회색 영역
    = free 영역
    = LiDAR ray가 지나간 cell
    
    검은색 영역
    = occupied 영역
    = LiDAR point가 찍힌 endpoint cell + inflation 영역
    
    빨간/초록 점
    = PointCloud2 또는 clustered point cloud 시각화 결과
    ```
    
- 로그 분석 (`ros2 launch lidar_perception local_occupancy_grid.launch.py`)
    
    ```bash
    input points      : 227
    valid points      : 227
    free cells        : 50
    occupied cells    : 75
    grid frame        : base_link
    grid origin       : (-0.50, -2.50)
    grid size         : width=45 height=50 resolution=0.10
    ray tracing       : true
    ```
    
    | 항목 | 의미 | 현재 상태 |
    | --- | --- | --- |
    | `input points` | `/points_filtered`에서 들어온 point 수 | 약 220~240개 |
    | `valid points` | z/range/grid 필터를 통과한 point 수 | input과 동일 |
    | `free cells` | ray tracing으로 free 처리된 cell 수 | 45~57개 |
    | `occupied cells` | endpoint + inflation으로 occupied 처리된 cell 수 | 71~83개 |
    | `grid frame` | grid 기준 좌표계 | `base_link` |
    | `grid origin` | grid 시작점 | `x=-0.5`, `y=-2.5` |
    | `grid size` | grid cell 크기 | 45 x 50, 10cm |
    | `ray tracing` | ray tracing 사용 여부 | true |
    
    즉, 지금은:
    
    ```
    전체 grid = unknown
    LiDAR ray가 지나간 cell = free
    LiDAR point endpoint 및 주변 = occupied
    ```
    
    이 구조가 실제로 돌고 있는 상태야.
    
    ---
    
    #### 현재 단계에서 가장 정확한 표현 :
    
    ```
    흰색 영역 = LiDAR가 관측한 free space
    ```
    
    ```
    이 cell에 장애물 point가 있었는가?
    또는 LiDAR ray가 지나갔는가?
    ```
    
    그래서 현재 흰색 영역은 **“주행 가능 영역 후보”**에 가깝고, 최종 주행 가능 영역은 아직 아니다.
    
    그 다음 단계인 `FreeSpaceModel`에서 이걸 해석해서:
    
    ```
    front_free
    left_free
    right_free
    front_clearance
    left_clearance
    right_clearance
    ```
    
    같은 값으로 바꾼다.
    
    그다음 `GapModel`이나 planning에서:
    
    ```
    로봇 폭 + safety margin 기준으로 실제 통과 가능한가?
    ```
    
    를 판단해야 한다.
    
    ---
    
    #### 색상 해석
    
    | 색상 | 현재 의미 | 실제 주행 판단 |
    | --- | --- | --- |
    | 흰색 | LiDAR ray가 지나간 free cell | 주행 가능 후보 |
    | 검은색 | 장애물 point 또는 inflation 영역 | 주행 불가 |
    | 청록/회색 | unknown, 관측 안 됨 | 아직 모름. 보수적으로 보면 주행 불가 후보 |

---

## 4. Free Space Model

> `local_occupancy_grid` 를 해석해서 전방/좌측/우측이 비어있는지, clearance는 거리는 몇인지 계산하는 모델
> 

최종 구조 :

```bash
/perception/lidar/points_filtered
        ├─ obstacle_cluster_node
        │     ↓
        │   /perception/lidar/obstacle_clusters
        │     ↓
        │   obstacle_model_node
        │     ↓
        │   /perception/lidar/obstacle_model
        │
        └─ local_occupancy_grid_node
              ↓
            /perception/lidar/local_occupancy_grid
              ↓
            free_space_model_node
              ↓
            /perception/lidar/free_space_model
```

기존 `ObstacleModel`과 새 `FreeSpaceModel`의 차이 :

| 모델 | 역할 |
| --- | --- |
| `ObstacleModel` | front/left/right 대표 장애물 요약 |
| `LocalOccupancyGrid` | base_link 기준 cell 단위 공간 지도 |
| `FreeSpaceModel` | grid를 해석해 전방/좌/우 free 여부와 clearance 요약 |
| `GapModel` | 장애물 사이 통과 가능한 gap 후보 계산 |
- **FreeSpaceModel의 MVP 데이터**
    
    ```bash
    front_free
    left_free
    right_free
    
    front_blocked
    left_blocked
    right_blocked
    
    front_clearance
    left_clearance
    right_clearance
    
    front_unknown_ratio
    left_unknown_ratio
    right_unknown_ratio
    ```
    
    - `front_clearance`: 전방 영역에서 가장 가까운 occupied cell까지의 거리.
    
    ```
    전방에 장애물이 1.2m 앞에 있음
    → front_clearance = 1.2
    ```
    
    - `front_free` : 전방 영역이 주행 가능 후보인지.
    
    판단 예시:
    
    ```
    occupied cell 없음 또는 충분히 멀리 있음
    unknown 비율이 너무 높지 않음
    free cell이 충분히 있음
    → front_free = true
    ```
    
    - `front_unknown_ratio`: 전방 sector 안에서 unknown cell 비율.
    
    ```
    unknown cell이 너무 많음
    → 실제로 빈 공간인지 확신하기 어려움
    ```
    
    이제 `free`와 `unknown`을 구분할 수 있으니까, FreeSpaceModel이 더 안전한 판단 가능
    
- **FreeSpaceModel의 sector 정의**
    
    ```bash
    front 영역:
      x = 0.3m ~ 2.0m
      y = -0.5m ~ 0.5m
    
    left 영역:
      x = 0.0m ~ 1.5m
      y = 0.3m ~ 1.5m
    
    right 영역:
      x = 0.0m ~ 1.5m
      y = -1.5m ~ -0.3m
    ```
    
    즉, grid 전체를 다 쓰지 말고, **로봇 주변에서 의미 있는 전방/좌/우 검사 박스**를 잡는 것.
    
    초기 Sector 값 :
    
    ```bash
    front_min_x: 0.3
    front_max_x: 2.0
    front_min_y: -0.5
    front_max_y: 0.5
    
    left_min_x: 0.0
    left_max_x: 1.5
    left_min_y: 0.3
    left_max_y: 1.5
    
    right_min_x: 0.0
    right_max_x: 1.5
    right_min_y: -1.5
    right_max_y: -0.3
    
    ```
    
- **FreeSpaceModel의 소스 코드 정리**
    - **1. 왜 grid index를 base_link 기준 실제 좌표 x, y로 변환하나?**
        
        `OccupancyGrid`의 `data[]`는 그냥 cell 배열이다. 예를 들어 어떤 cell이 `occupied = 100`이라고 해도, 그 cell이 **로봇 기준 앞쪽인지, 왼쪽인지, 몇 m 떨어져 있는지**는 grid index만으로 바로 알 수 없음.
        
        예를 들어 grid index가 이렇게 있다고 하자.
        
        ```
        grid_x =17;
        grid_y =28;
        ```
        
        이건 단순히 “격자 배열에서 x 방향 17번째, y 방향 28번째 cell”이라는 뜻이야. 그런데 FreeSpaceModel에서는 **이 cell이 어느 sector에 들어가는지 판단**해야 함.
        
        ```
        front sector인가?
        left sector인가?
        right sector인가?
        로봇에서 몇 m 떨어져 있는가?
        ```
        
        이걸 계산하려면 `grid_x`, `grid_y`를 실제 좌표로 바꿔야 해.
        
        현재 local occupancy grid 설정이 예를 들어 이렇지.
        
        ```
        origin_x: -0.5
        origin_y: -2.5
        resolution: 0.10
        ```
        
        그러면 `grid_x = 17`, `grid_y = 28`인 cell의 위치는 대략 아래와 같이 계산 :
        
        ```
        x =origin_x+ (grid_x+0.5)*resolution;
        y =origin_y+ (grid_y+0.5)*resolution;
        ```
        
        여기서 `+ 0.5`를 하는 이유는 **cell의 왼쪽 아래 모서리 좌표가 아니라 cell 중심 좌표를 사용하기 위해서**야.
        
        예를 들어 `resolution = 0.10`이면 cell 하나는 10cm야.
        
        ```
        grid_x = 0번 cell의 범위:
        x = -0.5 ~ -0.4
        ```
        
        이 cell을 대표하는 좌표를 왼쪽 경계인 `-0.5`로 잡으면 실제 cell 중심보다 5cm 왼쪽으로 치우쳐. 그래서 cell 중심인 `-0.45`를 쓰는 게 더 자연스럽다.
        
        ```
        x = origin_x + grid_x * resolution;
        // grid_x = 0이면 x = -0.5, cell 시작점
        
        x = origin_x + (grid_x+0.5) * resolution;
        // grid_x = 0이면 x = -0.45, cell 중심점
        ```
        
        정리하면:
        
        ```
        grid_x, grid_y
        = 배열 index
        
        x, y
        = base_link 기준 실제 위치 [m]
        
        grid_x + 0.5
        = cell의 시작점이 아니라 중심점을 대표 좌표로 쓰기 위한 보정
        ```
        
        이 변환이 있어야 각도와 거리를 계산할 수 있고, 그래야 front/left/right sector 분류가 가능
        
        ---
        
    - **2. 왜 “x+ : 0도, y+ : +90도, y- : -90도”라고 하고 x-는 말하지 않았나?**
        
        `base_link` 기준 좌표계는 보통 이렇게 본다.
        
        ```
        x+ : 로봇 전방
        x- : 로봇 후방
        y+ : 로봇 좌측
        y- : 로봇 우측
        ```
        
        각도 기준으로 보면:
        
        ```
        x+ : 0도
        y+ : +90도
        y- : -90도
        x- : ±180도
        ```
        
        그런데 우리가 지금 FreeSpaceModel에서 검사하는 **LiDAR FOV가 대략 `-60도 ~ +60도`**야. 즉, 로봇 전방 중심의 영역만 보고 있어.
        
        현재 sector도 이렇게 정의했지.
        
        ```
        right: -60 ~ -20
        front: -20 ~ +20
        left : +20 ~ +60
        ```
        
        이 범위 안에서는 `x-`, 즉 후방 `±180도`가 아예 검사 대상에 들어오지 않아. 그래서 설명에서 `x+`, `y+`, `y-`만 강조한 거야.
        
        정확히 쓰면 이렇게야.
        
        ```
        base_link 기준 각도:
        x+ = 0도, 전방
        y+ = +90도, 좌측
        y- = -90도, 우측
        x- = ±180도, 후방
        ```
        
        하지만 현재 CygLiDAR FOV와 sector 기준에서는:
        
        ```
        검사 대상: -60도 ~ +60도
        x- 후방 영역: 검사 대상 아님
        ```
        
        그래서 **LiDAR cyglidar D1의 스펙 상 후방영역(x-)는 확인할 수 없기에 x는 `x+`만 언급된 것**
        
    - **3. unknown dominant가 정확히 뭐고, “세 sector 중 하나라도 unknown 비율이 너무 높으면 전체 blocked_state를 UNKNOWN_DOMINANT로 둘 수 있다”는 뜻은?**
        
        `unknown dominant`는 말 그대로 **unknown cell이 지배적이다**, 즉 특정 sector에서 관측되지 않은 영역이 너무 많다는 뜻이야.
        
        `local_occupancy_grid`는 이제 cell 값을 이렇게 나누고 있지.
        
        ```
        -1  = unknown, 아직 관측하지 못함
         0  = free, LiDAR ray가 지나간 공간
        100 = occupied, 장애물 point가 찍힌 공간
        ```
        
        예를 들어 front sector 안의 cell이 100개라고 해보자.
        
        ```
        unknown cell = 80개
        free cell = 15개
        occupied cell = 5개
        ```
        
        그러면:
        
        ```
        front_unknown_ratio = 80 / 100 = 0.8
        ```
        
        이 상황은 “장애물이 별로 없으니 전방이 안전하다”라고 말하면 안 돼. 왜냐하면 전방 cell 대부분을 **아직 본 적이 없기 때문**이야.
        
        즉 `unknown_dominant`는 이런 의미야.
        
        ```
        해당 방향은 장애물이 없는 게 아니라,
        아직 관측이 부족해서 안전하다고 확신할 수 없다.
        ```
        
        그럼 “세 sector 중 하나라도 unknown 비율이 너무 높으면 전체 blocked_state를 UNKNOWN_DOMINANT로 둘 수 있다”는 말은 뭐냐면, 전체 요약 상태인 `blocked_state`를 하나의 enum으로 표현할 때, 전방/좌측/우측 중 어느 하나라도 너무 불확실하면 전체 상태를 다음처럼 표시하겠다는 뜻이야.
        
        ```
        STATE_UNKNOWN_DOMINANT
        ```
        
        예를 들어:
        
        ```
        front_unknown_ratio = 0.75
        left_unknown_ratio = 0.20
        right_unknown_ratio = 0.10
        ```
        
        이면 전방은 확실하지 않아. 그러면 blocked_state를 그냥 `STATE_CLEAR`로 두면 위험해.
        
        그래서 보수적으로:
        
        ```
        blocked_state = STATE_UNKNOWN_DOMINANT
        ```
        
        로 둬서 후속 decision/planning에게 이렇게 알려주는 거야.
        
        ```
        "지금은 free/blocked 판단보다 관측 부족 상태가 더 중요하다."
        ```
        
        다만 이 정책은 나중에 바꿀 수 있어. “세 sector 중 하나라도 unknown이 높으면 unknown dominant”는 매우 보수적인 정책이고, 나중에는 이렇게 바꿀 수도 있어.
        
        ```
        front sector만 unknown이 높을 때만 UNKNOWN_DOMINANT
        또는
        front/left/right 평균 unknown ratio가 높을 때만 UNKNOWN_DOMINANT
        또는
        front_free 판단에만 unknown을 강하게 반영
        ```
        
        초기에는 안전하게 보수적으로 가는 게 좋아.
        
    - **4. blocked_state enum 결정에서 enum 결정이 뭐지?**
        
        `enum`은 쉽게 말하면 **상태를 숫자 코드로 정의한 것**이야.
        
        네 `FreeSpaceModel.msg`에 이렇게 정의했지.
        
        ```
        uint8 STATE_CLEAR=0
        uint8 STATE_FRONT_BLOCKED=1
        uint8 STATE_LEFT_BLOCKED=2
        uint8 STATE_RIGHT_BLOCKED=3
        uint8 STATE_FRONT_LEFT_BLOCKED=4
        uint8 STATE_FRONT_RIGHT_BLOCKED=5
        uint8 STATE_LEFT_RIGHT_BLOCKED=6
        uint8 STATE_SURROUNDED=7
        uint8 STATE_UNKNOWN_DOMINANT=8
        
        uint8 blocked_state
        ```
        
        즉, `blocked_state`는 그냥 숫자 하나야.
        
        ```
        0이면 CLEAR
        1이면 FRONT_BLOCKED
        7이면 SURROUNDED
        8이면 UNKNOWN_DOMINANT
        ```
        
        `determineBlockedState()` 함수는 front/left/right의 blocked 여부를 보고 이 숫자 중 하나를 골라주는 함수야.
        
        예를 들어:
        
        ```
        front_blocked = true
        left_blocked = false
        right_blocked = false
        ```
        
        이면:
        
        ```
        blocked_state =STATE_FRONT_BLOCKED;// 값은 1
        ```
        
        이 되는 거야.
        
        왜 문자열 대신 enum을 쓰냐면, 후속 노드에서 안정적으로 비교하기 위해서야.
        
        문자열이면:
        
        ```
        if (msg->blocked_state=="front_blocked")
        ```
        
        처럼 비교해야 하고, 오타에 약해.
        
        enum이면:
        
        ```
        if (msg->blocked_state==
            robot_interfaces::msg::FreeSpaceModel::STATE_FRONT_BLOCKED)
        ```
        
        처럼 비교할 수 있어. 더 안전하고 빠르고 명확해.
        
        정리하면:
        
        ```
        enum 결정
        = front/left/right 상태를 종합해서 blocked_state 숫자 코드를 하나 선택하는 것
        ```
        
    - **5. nearest clearance 후보가 뭐지? 가까운 거리 후보들인가?**
        
        **nearest clearance 후보 : 각 sector 안에서 occupied cell까지의 거리 후보들 중 가장 가까운 값**
        
        예를 들어 front sector에 occupied cell이 3개 있다고 해보자.
        
        ```
        occupied cell A: distance = 1.5m
        occupied cell B: distance = 0.9m
        occupied cell C: distance = 2.1m
        ```
        
        이때 front clearance는:
        
        ```
        front_clearance = 0.9m
        ```
        
        가 돼.
        
        왜냐하면 FreeSpaceModel에서 clearance는 “가장 가까운 장애물까지의 여유 거리”로 해석하기 때문이야.
        
        `updateSectorStats()`에서 occupied cell을 만날 때마다:
        
        ```
        if (distance<stats.nearest_occupied_distance) {
        stats.nearest_occupied_distance = distance;
        }
        ```
        
        이렇게 갱신하지.
        
        즉, `nearest clearance 후보`라는 말은:
        
        ```
        occupied cell이 발견될 때마다
        “이 cell까지의 거리도 최소 장애물 거리 후보가 될 수 있다” 라고 보고,
        그중 가장 작은 값을 선택한다.
        ```
        
        라는 뜻이야.
        
        용어를 더 정확히 쓰면:
        
        ```
        nearest_occupied_distance
        = sector 안에서 가장 가까운 occupied cell까지의 거리
        
        clearance
        = 그 값을 장애물 여유 거리로 해석한 것
        ```
        
        후보라는 표현은 “여러 occupied cell 거리 중 최소값 후보”라는 의미였어.
        
- **FreeSpaceModel 실행 과정**
    
    [Data Flow]
    
    ```bash
    CygLiDAR driver
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
    ```
    
    **터미널 1: CygLiDAR 드라이버 실행**
    
    먼저 LiDAR 원본 토픽이 나와야 해.
    
    ```
    ros2 launch cyglidar_d1_ros2 cyglidar.launch.py
    ```
    
    ---
    
    **터미널 2: pointcloud_preprocess_node 실행**
    
    `/scan_3D`를 받아서 `/perception/lidar/points_filtered`를 만들어야 한다.
    
    네가 기존에 만든 launch가 있다면:
    
    ```
    ros2 launch lidar_perception pointcloud_preprocess.launch.py
    ```
    
    만약 기존 전체 LiDAR pipeline launch가 `pointcloud_preprocess_node`까지 포함한다면 그걸 실행해도 돼.
    
    ```
    ros2 launch lidar_perception lidar_pipeline.launch.py
    ```
    
    확인:
    
    ```
    ros2 topic list |grep points_filtered
    ros2 topicecho /perception/lidar/points_filtered--once |grep frame_id
    ros2 topic hz /perception/lidar/points_filtered
    ```
    
    기대:
    
    ```
    frame_id: base_link
    ```
    
    ---
    
    **터미널 3: local_occupancy_grid_node 실행**
    
    `/points_filtered`를 받아서 `/local_occupancy_grid`를 만든다.
    
    ```
    ros2 launch lidar_perception local_occupancy_grid.launch.py
    ```
    
    ---
    
    **터미널 4: free_space_model_node 실행**
    
    `/local_occupancy_grid`를 받아서 `/free_space_model`을 만든다.
    
    ```
    ros2 launch lidar_perception free_space_model.launch.py
    ```
    
    로그는 이런 식으로 나와야 한다.
    
    ```
    free space model
    front: free=... blocked=... clearance=... unknown=... free_ratio=... occupied=...
    left : free=... blocked=... clearance=... unknown=... free_ratio=... occupied=...
    right: free=... blocked=... clearance=... unknown=... free_ratio=... occupied=...
    blocked_state=...
    ```
    
    ---
    
    **터미널 5: RViz 확인**
    
    새 터미널에서:
    
    ```
    rviz2 -d /home/jetson/.rviz2/lidar_preprocess.rviz
    ```
    
    설정:
    
    ```
    Fixed Frame = base_link
    ```
    
    추가할 Display:
    
    ```
    TF
    PointCloud2 → /perception/lidar/points_filtered
    Map         → /perception/lidar/local_occupancy_grid
    ```
    
    `FreeSpaceModel`은 RViz에 바로 시각화되는 타입은 아니니까 `ros2 topic echo`로 확인하면 된다.
    
    ```
    ros2 topicecho /perception/lidar/free_space_model
    ```
    
- **FreeSpaceModel Rviz 시각화**
    
    ### RViz 확인
    
    기존 설정 파일:
    
    ```
    rviz2 -d /home/jetson/.rviz2/lidar_preprocess.rviz
    ```
    
    RViz 설정:
    
    ```
    Global Options
      Fixed Frame: base_link
    ```
    
    Display 추가:
    
    ```
    Add → By display type → MarkerArray
    Topic → /perception/lidar/free_space_markers
    ```
    
    같이 켜면 좋은 Display:
    
    ```
    TF
    PointCloud2 → /perception/lidar/points_filtered
    Map         → /perception/lidar/local_occupancy_grid
    MarkerArray → /perception/lidar/free_space_markers
    ```
    
    [결과 사진]
    
    ![image.png](image%2031.png)
    
    ---
    
    ### 시각화 결과 해석
    
    색상은 이렇게 보면 돼.
    
    ```
    노랑/주황:
      unknown_ratio가 threshold 이상
      지금처럼 관측 부족이 지배적인 상태
    
    초록:
      free=true
      해당 방향이 주행 가능 후보
    
    빨강:
      blocked=true
      해당 방향이 장애물/clearance 기준으로 막힘
    
    회색:
      free도 blocked도 아닌 애매한 상태
    ```
    
    현재 로그 기준으로는 세 방향 모두 unknown이 높기 때문에, 처음에는 거의 노랑/주황으로 보일 가능성이 높아.
    
    그게 정상이다.
    
    이 시각화의 목적은 **왜 전부 unknown dominant로 빨려 들어가는지 sector와 grid 위에서 확인하는 것**이야.
    
    이 다음 단계에서 확인할 포인트는 이거야.
    
    ```
    1. front/left/right boundary가 실제 LiDAR FOV와 맞는가?
    2. free cell이 너무 좁게 찍히는가?
    3. occupied inflation이 과하게 넓은가?
    4. grid 범위가 너무 넓어서 unknown이 과하게 많은가?
    5. threshold가 너무 보수적인가?
    ```
    
    이제 숫자 튜닝 전에, 이 MarkerArray를 RViz에 띄워서 sector 판단이 실제 공간과 맞는지 먼저 보면 된다.
    

## 3. Localization Hint Model