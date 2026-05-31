# Git Convention

# Git Flow 전략을 정석대로 따르면서 하자

---

## 브랜치 구조

| 브랜치 | 용도 | 분기 대상 | 머지 대상 |
| --- | --- | --- | --- |
| `master~/launch_isaac.sh --enable isaacsim.ros2.bridge` | 프로덕션 배포. 버전 태그 부여 | — | release, hotfix |
| `develop` | 다음 릴리즈 통합 | — | feature, release, hotfix |
| `feature/*` | 신규 기능 개발 | develop | develop |
| `release/*` | 릴리즈 준비 (버그 수정·문서만) | develop | master + develop |
| `hotfix/*` | 프로덕션 긴급 수정 | master | master + develop |

`master`와 `develop`은 직접 push 금지. **PR로만 수정.**

---

## 네이밍 규칙

```
feature/{짧은설명}
release/v{MAJOR}.{MINOR}.{PATCH}
hotfix/v{PATCH}-{짧은설명}
```

- 소문자, 하이픈 구분
- 짧은설명은 3~5단어
- 예: `feature/pid-tuning`, `feature/readme-update`

---

## 커밋 메시지

```
{type}({scope}): {제목}
```

### Type

`feat` · `fix` · `refactor` · `docs` · `test` · `chore` · `perf` · `style`

### Scope (향후 수정 바람)

**STM32 펌웨어**`hal`, `driver`, `control-fw`, `gait`, `firmware`

**ROS2 계층**`vendor`, `interfaces`, `common`, `perception`, `localization`, `planning`, `decision`, `control`, `network`, `bringup`, `tests`

**독립 영역**`rl`, `vision`, `dtwin`, `rpi`, `proto`, `config`, `hw`, `docs`, `infra`

### 예시

```
feat(control): PID anti-windup 구현
fix(driver): STS3215 응답 타임아웃 처리
docs(infra): Git Flow 컨벤션 추가
```

---

## 일상 워크플로우

### 새 기능 시작

```bash
git checkout develop

# 최신화
git pull origin develop

# 개인 브랜치 만들기
git checkout -b feature/{짧은설명}
```

- 초보용
    
    ```cpp
    git checkout -b {Type}/{설명}
    ```
    
    - 업로드할 파일을 담을 브랜치 생성 후 **내 로컬 github 저장소로 이동**

### 작업 후 푸시

```bash
git add <파일>
git commit -m "feat(scope): 설명"
git push -u origin feature/{짧은설명}
```

→ GitHub에서 `develop` 대상 PR 생성 → 리뷰 → 머지

- 초보용
    
    ```cpp
    1. **업로드할 파일 경로** 복사하기
    ex) jetson@ubuntu2204:~/robot_ws/src/perception/lidar_perception$ pwd
    		/home/jetson/robot_ws/src/perception/lidar_perception
    
    2. 업로드할 **로컬 github 경로**로 이동
    ex) jetson@ubuntu2204:~/bw522/SPOT_GET_IT/robot_ws/src/perception$
    
    3. 해당 로컬 github 경로에 업로드할 파일 복사하기
    ex) cp -r /home/jetson/robot_ws/src/perception/lidar_perception .
    
    4. 현재 위치한 폴더 아래의 변경 사항 및 파일들을 Git stage에 올리기
    ex) git add .
    
    5. stage에 올라간 변경사항들을 하나의 커밋으로 저장하는 명령어
    ex) git commit -m "feat(lidar_perception): add LiDAR perception pipeline"
    
    6. commit후 origin에 본인 브랜치 적용하기
    ex) git push -u origin feature/robot_interfaces
    
    ```
    

### 머지 후 정리 (반드시 Request 진행 후 머지 완료 하고나서 진행하기)

```bash
# 본인이 만든 branch에서는 해당 branch 삭제가 안되므로 develop으로 이동
git checkout develop

# 내가 넣었던 파일을 develop branch에 최신화한 것을 다시 최신화
git pull origin develop

# 내가 만든 브랜치 삭제
git branch -d feature/{짧은설명}

# git에 있는 실제 branch 환경을 내 로컬에도 적용
git fetch origin --prune
```

### 매일 아침 최신화

```bash
git checkout develop
git pull origin develop
```

---

## PR 규칙

- 타겟: `develop` (GitHub 페이지에서 Pull Request 생성)
- 머지 방식: Merge commit (`-no-ff`). Fast-forward · Squash 금지
- 리뷰어: 최소 1명 승인 후 머지
- CI 통과 필수

---

## 금지 사항

1. `master` / `develop`에 직접 push
2. 공유 브랜치에 `git push --force`
3. feature → feature 머지
4. release 브랜치에서 신규 기능 추가
5. hotfix 후 develop에 머지 빼먹기

# 폴더 구조

SPOT_GET_IT/
├── .gitlab/
├── .github/
├── docs/
├── firmware/                            
├── robot_ws/                         
├── ai_training/
│   ├── rl/
│   └── vision/
├── shared/
├── hardware/
├── d_twin/
├── rpi/                             
│      ├── qt/
│      ├── bridge_app/
└── tools/

- 세부 폴더 생성되면 해당 폴더 구조 업데이트 요망
- 모든 상위 폴더에 [README.md](http://README.md) 존재

- 구버전 폴더 구조
    
    SPOT_GET_IT/
    ├── README.md
    ├── codeowners
    ├── .gitlab/
    │   └── merge_request_templates/
    ├── docs/
    │   ├── architecture/
    │   ├── hardware/
    │   ├── protocols/
    │   │   ├── stm32_jetson_binary.md       # shared/proto 스키마 설명
    │   │   └── ros2_topics.md                       # 토픽 네이밍 규격
    │   ├── conventions/
    │   │   └── ros2_convention.md            # 팀 ROS2 컨벤션 (SSoT)
    │   └── meeting-notes/
    │
    ├── firmware/                             # STM32F446RE · CMake (독립 빌드)
    │   ├── core/                             # CubeMX 생성물
    │   ├── drivers/                          # HAL, CMSIS
    │   ├── middlewares/                      # FreeRTOS
    │   ├── app/                              # 팀이 실제로 작업하는 영역
    │   │   ├── hal/                          # 함수 포인터 vtable 추상화
    │   │   ├── drivers/                      # STS3215, BNO055, 74HC126
    │   │   ├── control/                      # PID, anti-windup, IK/FK
    │   │   ├── gait/                         # 보행 궤적 실행
    │   │   ├── comm/                         # Jetson 통신 (UART/USB)
    │   │   └── tasks/                        # FreeRTOS 태스크
    │   ├── proto_generated/                  # shared/proto에서 자동 생성 (gitignore)
    │   ├── tests/                            # Unity/CMock
    │   └── CMakeLists.txt
    │
    ├── robot_ws/                             # Jetson Orin Nano · colcon · ROS2 Humble
    │   └── src/
    │       ├── vendor/                       # 외부 드라이버 (원본 유지, 로직 주입 금지)
    │       │   ├── cyglidar_d1_ros2/
    │       │   └── oak_camera_driver/
    │       │
    │       ├── interfaces/                   # 🔒 모든 msg/srv/action 집중
    │       │   └── robot_interfaces/
    │       │       ├── msg/
    │       │       │   ├── ObstacleModel.msg
    │       │       │   ├── FreeSpaceModel.msg
    │       │       │   ├── LocalizationHint.msg
    │       │       │   └── VictimDetection.msg
    │       │       ├── srv/
    │       │       ├── action/
    │       │       ├── CMakeLists.txt
    │       │       └── package.xml
    │       │
    │       ├── common/                       # 공통 유틸, 상수, 좌표/enum
    │       │   ├── robot_common/
    │       │   └── robot_utils/
    │       │
    │       ├── perception/                   # 인식 계층
    │       │   ├── lidar_perception/         # LiDAR 전처리 + 모델 산출
    │       │   ├── camera_perception/        # YOLO, victim_detection
    │       │   └── sensor_fusion/            # camera-lidar 융합 (공동)
    │       │
    │       ├── localization/                 # 측위 계층
    │       │   ├── localization_fusion/      # odom+imu+hint 융합
    │       │   └── map_localization_adapter/ # 정밀지도 연결
    │       │
    │       ├── planning/                     # 경로 계획 계층
    │       │   ├── global_path_manager/
    │       │   ├── local_path_generator/
    │       │   └── path_validator/
    │       │
    │       ├── decision/                     # 행동 결정 계층
    │       │   ├── behavior_manager/
    │       │   ├── safety_supervisor/        # emergency stop, fail-safe
    │       │   └── mission_executor/         # 정찰 임무 상태 머신
    │       │
    │       ├── control/                      # 제어 계층
    │       │   ├── motion_controller/        # 속도/조향 명령 생성 (RL 추론)
    │       │   └── actuator_bridge/          # ← STM32 시리얼 브릿지 (통신 담당)
    │       │
    │       ├── network/                      # 로봇-관제 외부 통신
    │       │   ├── fleet_bridge/             # 관제 상태 송수신
    │       │   ├── digital_twin_bridge/      # 디지털트윈 반영
    │       │   ├── telemetry_reporter/       # 상태/로그 전송
    │       │   └── image_stream_sender/      # 전방 이미지 송출
    │       │
    │       ├── bringup/                      # 통합 실행
    │       │   ├── robot_bringup/            # launch, params
    │       │   ├── robot_description/        # URDF/Xacro/TF
    │       │   └── robot_simulation/         # Gazebo/Isaac/MORAI 연동
    │       │
    │       └── tests/
    │           ├── integration_tests/
    │           └── scenario_tests/
    │
    │
    ├── ai_training/
    │   ├── rl/
    │   └── vision/
    │
    ├── shared/
    │   ├── proto/                            # 🔒 STM32↔Jetson 바이너리 프로토콜
    │   │   ├── schema/                       # 단일 원본 (.yaml 또는 .proto)
    │   │   ├── [generate.py](http://generate.py/)                   # C/Python 바인딩 자동 생성
    │   │   ├── c/                            # firmware/proto_generated/로 복사
    │   │   └── python/                       # actuator_bridge 노드에서 import
    │   └── robot_config/
    │       ├── urdf/                         # Spot Micro URDF
    │       └── params.yaml                   # 링크 길이, 질량 등
    │
    ├── hardware/
    │   ├── schematics/                       # KiCad, 회로도 PDF
    │   ├── pcb/                              # PDB 설계
    │   ├── cad/                              # Spot Micro STL (LFS)
    │   └── wiring/                           # 전원 star topology
    │
    ├── d_twin/
    │
    ├── rpi/                             
    │      ├── qt/
    │      ├── bridge_app/
    │
    ├── tools/
    ├── servo_calibration/                # STS3215 ID 할당, 영점 조정
    ├── rosbag_analyzer/                  # rosbag2 분석
    └── benchmarks/                       # cyclictest, 제어 주기 측정