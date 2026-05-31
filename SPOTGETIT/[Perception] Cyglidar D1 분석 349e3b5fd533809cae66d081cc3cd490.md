# [Perception] Cyglidar D1 분석

상태: Perception

## 1. Cyglidar Topic 종류

```bnf
/clicked_point
/goal_pose
/initialpose
/parameter_events
/scan
/scan_2D
/scan_3D
/scan_image
/tf
/tf_static
```

## 2. Topic별 데이터 구조 분석

#### 💡TIP. 데이터 구조 분석 명령어

**1️⃣ Topic의 Message Type 확인**

```bnf
ros2 topic type /{Topic Name}
```

예시 : 

`sensor_msgs/msg/LaserScan` 

- `/scan` : 토픽 이름
- `sensor_msgs` : 이 메시지가 들어있는 **ROS2 패키지 이름**
- `msg` : 메시지 폴더
- `LaserScan` : 실제 메시지 타입 이름

**2️⃣ 표준 메시지 원본 경로 확인**

```bnf
find $(ros2 pkg prefix sensor_msgs) -name {Message Type}
```

**3️⃣ 메시지 내용 확인**

```bnf
sed -n '1,200p' <Message Type의 경로>
```

#### 2-1. `/scan`

> 2D 평면 라이다 한 번 스캔한 결과
> 

**Message Type : `sensor_msgs/msg/LaserScan`**

```bnf
# Single scan from a planar laser range-finder

std_msgs/Header header      

**float32 angle_min**            # 스캔 시작 각도 [rad]
**float32 angle_max**            # 스캔 끝   각도 [rad]
**float32 angle_increment**      # 측정한 각도 간의 변화량 [rad]

float32 time_increment       # 각 측정점 사이의 시간 간격 [seconds]

float32 scan_time            # 스캔 한 번 전체에 걸린 시간 [seconds]

**float32 range_min**            # 유효 최소 거리 [m] - 0.1m
**float32 range_max**            # 유효 최대 거리 [m] - 10m

# (Note: values < range_min or > range_max should be discarded)
**float32[] ranges**             # 해당 각도 방향으로 측정한 거리 값 [m]
        
float32[] intensities        # 반사 세기 값

```

- 각도는 `frame_id` 좌표계 기준
- +Z축을 중심으로 회전하는 각도
- 각도는 반시계 방향으로 증가
    - **`0 rad` = 정면**
    - **`+ 각도` = 왼쪽**
    - **`- 각도`  = 오른쪽**
- `time_increment` : **각 측정점 사이의 시간 변화량** → 이동 중 **3D 위치 보정**에 용이

#### 2-2. `/scan_2D` , `/scand_3D`

> **N차원 점들의 집합 데이터(점군)**를 담은 메시지
> 

**Message Type : `sensor_msgs/msg/PointCloud2`**

```bnf
# Time of sensor data acquisition, and the coordinate frame ID (for 3d points).
std_msgs/Header header

# 2D structure of the point cloud. If the cloud is unordered, height is
# 1 and width is the length of the point cloud.
**uint32 height**        # 점군의 행 개수 1 or 1 이상 
**uint32 width**         # 점군의 열 개수 

# Describes the channels and their layout in the binary data blob.
**PointField[] fields**  # 각 점들은 어떤 값을 가지는지에 대한 데이터

bool    is_bigendian # Is this data bigendian?
**uint32  point_step**   # 점 하나가 차지하는 총 Byte 값
uint32  row_step     # 한 줄(row)이 차지하는 바이트 수
**uint8[] data**         # 실제 점 데이터 (row_step*height)

**bool is_dense**        # 유효하지 않은 점(NaN, invalid)이 없는지 여부

```

#### 2-3. `/scan_image`

> `Image`는 **압축되지 않은 이미지 데이터**를 담는 메시지
이미지 : 거리값을 이미지화한 결과
> 

**Message Type : `sensor_msgs/msg/Image`**

```bnf
# This message contains an uncompressed image
# (0, 0) is at top-left corner of image

std_msgs/Header header       # Header timestamp should be acquisition time of image
                             # Header frame_id should be optical frame of camera
                             # origin of frame should be optical center of cameara
                             # +x should point to the right in the image
                             # +y should point down in the image
                             # +z should point into to plane of the image
                             # If the frame_id here and the frame_id of the CameraInfo
                             # message associated with the image conflict
                             # the behavior is undefined

uint32 height                # image height, that is, number of rows
uint32 width                 # image width, that is, number of columns

# The legal values for encoding are in file src/image_encodings.cpp
# If you want to standardize a new string format, join
# ros-users@lists.ros.org and send an email proposing a new encoding.

string encoding       # Encoding of pixels -- channel meaning, ordering, size
                      # taken from the list of strings in include/sensor_msgs/image_encodings.hpp

uint8 is_bigendian    # is this data bigendian?
uint32 step           # Full row length in bytes
uint8[] data          # actual matrix data, size is (step * rows)

```