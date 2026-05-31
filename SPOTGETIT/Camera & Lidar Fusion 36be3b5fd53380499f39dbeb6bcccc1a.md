# Camera & Lidar Fusion

## 1. 기본 필수 정보

### 1-1. base_link 기준 camera_link, camera_optical_frame

Camera TF 구조:

```bash
base_link
 ├── laser_frame                     // LiDAR
 └── camera_link                     // Camera
      └── camera_optical_frame
```

- 1️⃣ base_link → camera_link
    
    > 카메라 몸체가 로봇에 장착된 위치를 의미
    > 
    
    ```
    camera_link_tf_node = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="base_link_to_camera_link_tf",
        arguments=[
            "0.14", "0.00", "0.00",
            "0", "0", "0",
            "base_link", "camera_link"
        ],
    )
    ```
    
    - base_link 기반 LiDAR 위치 기준으로 Camera는 약 5cm 밑에 위치함
- 2️⃣ camera_link → camera_optical_frame
    
    > 이미지 projection용 frame
    > 
    
    ```
    camera_optical_frame:
    x = 이미지 오른쪽
    y = 이미지 아래
    z = 카메라 전방
    ```
    
    반면 `base_link`/`camera_link`는 보통:
    
    ```
    x = 전방
    y = 좌측
    z = 위
    ```
    
    그래서 projection을 하려면 `camera_optical_frame`을 따로 만들어주는 게 안전함.
    
    ```
    camera_optical_tf_node = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="camera_link_to_camera_optical_frame_tf",
        arguments=[
            "0", "0", "0",
            "-1.5708", "0", "-1.5708",
            "camera_link", "camera_optical_frame"
        ],
    )
    ```
    

---

### 1-2. **OAK-D-Lite의 480x270 해상도 기준 intrinsic matrix 값**

OAK-D-Lite RGB 카메라 스펙:

```
RGB camera HFOV ≈ 69°
RGB camera VFOV ≈ 54°
```

해당 스펙을 480×270 이미지에 적용하면 대략:

```
fx ≈ 349 px
fy ≈ 265 px
cx ≈ 240 px
cy ≈ 135 px
```

- 계산식
    
    ```
    fx = image_width  / (2 * tan(HFOV / 2))
    fy = image_height / (2 * tan(VFOV / 2))
    
    cx = image_width  / 2
    cy = image_height / 2
    ```
    
- 실제 장비에 저장된 calibration 값
    
    ![image.png](image%2078.png)
    
    OAK-D-Lite가 Jetson에 연결되어 있고, `depthai`가 설치되어 있어야 아래의 코드 활용해서 찾을 수 있다.
    
    ```bash
    import depthai as dai
    import numpy as np
    
    width = 480
    height = 270
    
    with dai.Device() as device:
        calib = device.readCalibration()
    
        K = np.array(
            calib.getCameraIntrinsics(
                dai.CameraBoardSocket.CAM_A,
                width,
                height
            )
        )
    
        D = np.array(
            calib.getDistortionCoefficients(
                dai.CameraBoardSocket.CAM_A
            )
        )
    
        print("RGB camera intrinsic K:")
        print(K)
    
        print("\nfx =", K[0, 0])
        print("fy =", K[1, 1])
        print("cx =", K[0, 2])
        print("cy =", K[1, 2])
    
        print("\nDistortion coefficients:")
        print(D)
    ```
    

**[480x270 기준 실제 intrinsic 값]**

```bash
// 480×270 기준 실제 intrinsic 값
fx = 369.671630859375
fy = 369.8414306640625
cx = 231.57676696777344
cy = 141.4792938232422

// Intrinsic matrix K
K =
[369.6716,   0.0000, 231.5768
   0.0000, 369.8414, 141.4793
   0.0000,   0.0000,   1.0000]
```

---

## 2. 설계 구조

> raw filtered pointcloud를 bbox에 투영하는 방식
> 

```bash
camera_lidar_person_fusion_node

입력:
  - /perception/lidar/points_filtered
    Type: sensor_msgs/msg/PointCloud2
    Frame: base_link

  - /perception/camera/person_detections
    Type: 카메라 담당자 bbox 메시지 또는 custom msg
    기준: 480 x 270 image pixel

  - TF:
    base_link -> camera_link
    camera_link -> camera_optical_frame

출력:
  - /perception/fusion/person_3d_tracks
  - /perception/fusion/person_pair_distances
```

```bash
robot_ws/src/perception/lidar_perception/
├── include/lidar_perception/
│   └── camera_lidar_person_fusion_node.hpp
├── src/
│   └── camera_lidar_person_fusion_node.cpp
├── config/
│   └── camera_lidar_person_fusion.param.yaml
└── launch/
    └── camera_lidar_person_fusion.launch.py
```

### 1️⃣ Camera static TF

LiDAR static TF가:

```
arguments=["0.14","0.00","0.05","0","0","0","base_link","laser_frame"]
```

이고, 카메라가 LiDAR보다 5cm 아래면:

```
arguments=["0.14","0.00","0.00","0","0","0","base_link","camera_link"]
```

그리고 projection용 optical frame:

```
arguments=["0","0","0","-1.5708","0","-1.5708","camera_link","camera_optical_frame"]
```

### 2️⃣ Camera intrinsic

fusion node YAML:

```
camera_lidar_person_fusion_node:
  ros__parameters:
    image_width: 480
    image_height: 270

    fx: 369.671630859375
    fy: 369.8414306640625
    cx: 231.57676696777344
    cy: 141.4792938232422

    lidar_frame:"base_link"
    camera_frame:"camera_optical_frame"

    lidar_points_topic:"/perception/lidar/points_filtered"
    detections_topic:"/perception/camera/person_detections"
    person_tracks_topic:"/perception/fusion/person_3d_tracks"
    pair_distances_topic:"/perception/fusion/person_pair_distances"

    min_points_per_bbox: 3
    max_depth_m: 5.0
    use_bbox_inner_region: true
```