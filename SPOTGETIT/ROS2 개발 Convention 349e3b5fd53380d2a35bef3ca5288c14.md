# ROS2 개발 Convention

## 1. 기본 원칙

> 
> 
> 
> 하나의 팀 병합 워크스페이스(`robot_ws`)에서 개발한다.
> 
> 다만 워크스페이스 안에서는 **패키지를 계층 이름으로 선언 X, 책임 단위로 분리**한다.
> 

예시 :

- 본인이 맡은 계층 : **perception**
- 해당 계층에서 구현할 기능 : **LiDAR를 통한 Spot 주변 장애물 정보 추출 노드**

### ROS2 C++ 패키지 구조

```bnf
robot_ws/src/
├── vender/
├── interfaces/
├── **perception/ → 계층 디렉토리**
│    ├── camera_perception/
│    └── **lidar_perception/ → 패키지**
│				├── package.xml
│				├── CMakeLists.txt
│				├── launch/
│				└── src/
│					   └── **obstacle_model_node.cpp → 본인이 개발한 기능(Node)**
├── localization/
├── planning/
```

### ROS2 Python 패키지 구조

```jsx
oak_camera_driver/          ← 패키지 루트
├── package.xml
├── setup.py
├── setup.cfg
├── oak_camera_driver/      ← Python 모듈 디렉토리 (패키지명과 동일해야 함)
│   ├── __init__.py
│   └── oak_camera_driver_node.py
├── launch/
└── config/
```

- 디렉토리는 **계층별**로 묶음
- 패키지는 **기능 책임별**로 분리
- 공통 메시지와 외부 드라이버는 별도 패키지로 격리
- **패키지 내부 전용 메시지는 가급적 생성 금지**
    - 모든 메시지는 **`robot_ws/src/interfaces/robot_interfaces/msg/`** 에 저장
- 상위 계층이 하위 계층을 참조할 수는 있지만, 하위 계층이 상위 계층을 참조하면 안 됨

---

## 2. 워크스페이스 구조

> 계층은 디렉토리로 묶고, 패키지는 **기능 책임 분리**로 자르는 구조
> 

#### **2-1. 최상위 워크스페이스 구조**

```bnf
Workspace/
├── build/
├── install/
├── log/
└── src/
    ├── 계층별 Directory 1/
    ├── 계층별 Directory 2/
    ├── 계층별 Directory 3/
    ├── 계층별 Directory 4/
    ├── ... /
    ├── ... /
    ├── ... /
    ├── ... /
    └── 계층별 Directory N/
```

```bnf
robot_ws/
├── build/
├── install/
├── log/
└── src/
    ├── vender/
    ├── interfaces/
    ├── common/
    ├── perception/
    ├── localization/
    ├── planning/
    ├── decision/
    ├── network/
    └── tests/
```

- **상세 구조**
    
    ```bnf
    robot_ws/
    └── src/
        ├── **vendor/**
        │   ├── cyglidar_d1_ros2/              # 외부 드라이버 원본
        │   └── oak_camera_driver/             # OAK-D Lite 카메라 드라이버 패키지
        │
        ├── **interfaces/ → 메시지 관리**
        │   └── robot_interfaces/              # msg/srv/action 전용
        │       ├── msg/
    		│       │   ├── ObstacleModel.msg
    		│       │   ├── FreeSpaceModel.msg
    		│       │   ├── LocalizationHint.msg
    		│       │   └── VictimDetection.msg
    		│       ├── srv/
    		│       ├── action/
    		│       ├── CMakeLists.txt
    		│       └── package.xml
        │
        ├── **common/**
        │   ├── robot_common/                  # 공통 상수, 유틸, 좌표/enum 정의
        │   └── robot_utils/                   # 공통 helper
        │
        ├── **perception/ → LiDAR + Camera**
        │   ├── lidar_perception/              # LiDAR 전처리 + 모델 산출
        │   ├── camera_perception/             # YOLO, victim_detection
        │   └── sensor_fusion/                 # 필요 시 camera-lidar 융합
        │
        ├── **localization/**
        │   ├── localization_fusion/           # odom+imu+hint 융합
        │   └── map_localization_adapter/      # 정밀지도/공통좌표계 연결
        │
        ├── **planning/ → 경로 생성**
        │   ├── global_path_manager/           # global path 수신/관리
        │   ├── local_path_generator/          # local path 생성
        │   └── path_validator/                # 경로 유효성 검사
        │
        ├── **decision/ → 행동 결정**
        │   ├── behavior_manager/              # 상위 행동 결정
        │   ├── safety_supervisor/             # emergency stop, fail-safe
        │   └── mission_executor/              # 정찰 임무 상태 머신
        │
        ├── **control/**
        │   ├── motion_controller/             # 속도/조향 명령 생성
        │   └── actuator_bridge/               # Spot 또는 하위 제어 인터페이스
        │
        ├── **network/**
        │   ├── fleet_bridge/                  # 로봇-관제 상태 송수신
        │   ├── digital_twin_bridge/           # 디지털트윈 반영
        │   ├── telemetry_reporter/            # 상태/로그 전송
        │   └── image_stream_sender/           # spot 전방 이미지 송출
        │
        ├── **bringup/**
        │   ├── robot_bringup/                 # launch, params
        │   ├── robot_description/             # URDF/Xacro/TF
        │   └── robot_simulation/              # Gazebo/Isaac/MORAI 연동
        │
        └── **tests/**
            ├── integration_tests/
            └── scenario_tests/
    ```
    

#### 2-2. 패키지 내부 구조

```bnf
my_package/
├── package.xml
├── CMakeLists.txt
├── **include/my_package/**   **# 각 노드 헤더 선언부**
├── **src/**                  **# 실제 노드 파일 구현부**
├── **config/**               **# 각 노드에 사용되는 파라미터 설정부**
├── launch/
├── msg/         # 필요 시 왠만하면 interfaces 패키지에 생성
├── srv/         # 필요 시
├── action/      # 필요 시
├── test/
└── README.md
```

- **상세 구조 예시**
    
    ```bnf
    perception/lidar_perception/
    ├── package.xml
    ├── CMakeLists.txt
    ├── include/lidar_perception/
    │   ├── preprocess.hpp
    │   ├── obstacle_model_builder.hpp
    │   ├── free_space_model_builder.hpp
    │   ├── localization_hint_builder.hpp
    │   └── occupancy_grid_builder.hpp
    ├── src/
    │   ├── lidar_preprocess_node.cpp
    │   ├── local_space_builder_node.cpp
    │   ├── obstacle_model_node.cpp
    │   ├── free_space_model_node.cpp
    │   └── localization_hint_node.cpp
    ├── config/
    │   ├── lidar_perception.param.yaml
    │   └── sectors.param.yaml
    └── launch/
        └── lidar_perception.launch.py
    ```
    

#### **패키지 기본 원칙  :**

#### **1️⃣ 패키지는 “하나의 책임”만 가진다.**

예를 들어 `lidar_perception`은 다음까지만 담당한다.

- `/scan` subscribe
- 공통 전처리
- `local_obstacle_points` publish
- `local_occupancy_grid` publish
- `ObstacleModel`, `FreeSpaceModel`, `LocalizationHint` publish

여기서 local path 생성까지 하면 안 된다.

그건 `planning/local_path_generator` 책임이다.

LiDAR 설계서에서도 raw publish → 공통 전처리 → obstacle/free_space/localization_hint 브랜치 구조로 정리돼 있다.

#### **2️⃣ 외부 드라이버 패키지는 vendor에 저장한다.**

`cyglidar_d1_ros2`는 vendor 원본으로 취급한다.

시스템 로직은 절대 vendor 패키지 안에 넣지 않는다.

필요하면 `lidar_perception`이 vendor topic을 subscribe해서 가공한다.

현재 정의서도 드라이버 계층과 가공 계층을 분리하는 방향으로 정리되어 있다.

#### **3️⃣ 커스텀 메시지는 interfaces 패키지 하나로 모은다.**

예:

- `ObstacleModel.msg`
- `FreeSpaceModel.msg`
- `LocalizationHint.msg`
- `VictimDetection.msg`

그리고 큰 공간 데이터는 가능한 한 표준 메시지를 쓴다.

- `local_obstacle_points` → `sensor_msgs/msg/PointCloud2`
- `local_occupancy_grid` → `nav_msgs/msg/OccupancyGrid`

#### 2-3. 노드 네이밍

> 노드명은 **기능이 바로 드러나게** 짓는다.
> 

좋은 예:

```bnf
lidar_preprocess_node.cpp
free_space_model_node.cpp
localization_hint_node.cpp
```

나쁜 예:

```bnf
main_node.cpp
planner_node2.cpp
test_node_final.cpp
```

---

## 3.  Topic 네이밍

> 토픽은 역할이 보이도록 namespace를 고정한다.
> 

```bnf
/<domain>/<module>/<topic_name>
```

#### **Topic 네이밍 기본 원칙 :**

#### **1️⃣ Open Source 패키지의 Topic은 그대로 사용한다.**

#### 2️⃣ **계층/역할이 이름에 드러나야 한다.**

토픽 이름만 보고도 아래와 같이 바로 알 수 있어야함

- 센서 raw인지
- perception 결과인지
- localization 결과인지
- planning 결과인지
- decision/control/network인지

#### 3️⃣ Format에 맞춰서 작성한다.

```bnf
/<domain>/<module>/<topic_name>
```

예:

- `/perception/lidar/obstacle_model`
- `/planning/local/local_path`

#### 4️⃣ snake_case만 사용한다.

- 소문자
- `_` 사용
- CamelCase 금지

좋은 예: `/perception/lidar/free_space_model`

나쁜 예: `/Perception/LiDAR/FreeSpaceModel`

#### 예시

Perception 계층 - LiDAR

```
/perception/lidar/local_obstacle_points
/perception/lidar/local_occupancy_grid
/perception/lidar/obstacle_model
/perception/lidar/free_space_model
/perception/lidar/localization_hint
```

---

## 4. launch / config 컨벤션

#### 4-1. 패키지 내부 launch

각 패키지는 단독 실행용 launch를 가진다.

예:

- `lidar_perception.launch.py`
- `local_path_generator.launch.py`

#### 4-2. 시스템 전체 launch → 연동 테스트때마다 진행할 예정

전체 시스템 실행은 `bringup/robot_bringup`에서만 담당한다.

예:

- `robot_core.launch.py`
- `mission_recon.launch.py`
- `simulation_recon.launch.py`

#### 4-3. params

모든 threshold, sector angle, map range, safety distance 등의 파라미터 값들은 yaml로 분리

예:

```
config/
├── lidar_perception.param.yaml
├── sectors.param.yaml
├── localization.param.yaml
└── planner.param.yaml
```

코드에 **하드코딩 금지**.

---

## 5. 개발 진행 규칙

#### 5-1. 기능 추가 순서

- `interfaces`에 msg 정의
- `perception`에서 raw → intermediate → summary 구현
- `localization/planning`에서 해당 topic 소비
- `decision/control` 연결
- `bringup`에서 통합

#### 5-2. PR 규칙

한 PR은 가능하면 **한 패키지 또는 한 책임**만 건드린다.

좋은 예:

- `robot_interfaces + lidar_perception`
- `localization_fusion`
- `local_path_generator`

나쁜 예:

- perception, planning, decision, control를 한 PR에서 다 변경

#### 5-3. README 의무화

모든 패키지는 README에 최소한 아래를 적는다. 본인이 개발한 모든 코드 설명이 가능하다면 나중에 작업해도 됨(단, 누군가 물어볼 때 모르면 README 적기)

- 역할
- subscribe topic
- publish topic
- required params
- 실행 명령어
- 예시 launch