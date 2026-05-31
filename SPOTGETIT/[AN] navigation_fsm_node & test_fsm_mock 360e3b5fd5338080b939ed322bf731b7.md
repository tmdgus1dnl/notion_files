# [AN] navigation_fsm_node & test_fsm_mock

상태: Autonomous Navigation

## navigation_fsm_node 브리핑

### 노드 역할

4개 토픽을 subscribe해서 매 100ms마다 FSM state를 판단하고 `/navigation/state/spot_01`로 publish합니다.

### Subscribe

| 토픽 | 타입 | 용도 |
| --- | --- | --- |
| `/navigation/path_progress/spot_01` | `PathProgress` | path 유효성, goal 도달, 거리 정보 |
| `/localization/pose` | `LocalizedRobotPose` | pose stale 판단 |
| `/perception/lidar/obstacle_model` | `ObstacleModel` | 전방/좌/우 장애물 거리 |
| `/perception/lidar/free_space_model` | `FreeSpaceModel` | 회피 가능 여부 |

### Publish

| 토픽 | 타입 |
| --- | --- |
| `/navigation/state/spot_01` | `NavigationState` |

### State 판단 우선순위

```
1. NAV_LOCALIZATION_LOST    pose 수신 끊김
2. NAV_PERCEPTION_STALE     obstacle 수신 끊김
3. NAV_WAITING_FOR_PATH     path_valid = false
4. NAV_EMERGENCY_STOP       전방 < 0.30m
5. NAV_GOAL_REACHED         goal_reached = true
6. NAV_STOPPED_BY_OBSTACLE  전방 막힘 + free space 없음
7. NAV_PLANNING_LOCAL_PATH  전방 막힘 + free space 있음
8. NAV_REJOINING_GLOBAL_PATH 회피 후 path 복귀 거리 이내
9. NAV_TRACKING_GLOBAL_PATH 정상 주행
```

### hysteresis

```
front_block_distance_m = 0.70  → blocked 진입
front_clear_distance_m = 0.85  → clear 조건
clear_hold_time_sec    = 0.50  → clear 유지 후 해제
```

---

## test_fsm_mock.py 브리핑

### 역할

FSM 노드 단독 테스트를 위해 4개 토픽을 mock 데이터로 publish하는 Python 스크립트입니다.

### 실행

```bash
python3 test_fsm_mock.py <scenario>
```

- 코드
    
    ```jsx
    #!/usr/bin/env python3
    """
    navigation_fsm_node 테스트용 mock publisher
    
    사용법:
      python3 test_fsm_mock.py <scenario>
    
    시나리오 선택지:
      normal        → NAV_TRACKING_GLOBAL_PATH 예상
      front_blocked → NAV_PLANNING_LOCAL_PATH 예상
      emergency     → NAV_EMERGENCY_STOP 예상
      goal_reached  → NAV_GOAL_REACHED 예상
      pose_stale    → NAV_LOCALIZATION_LOST 예상 (pose publish 중단)
      no_path       → NAV_WAITING_FOR_PATH 예상
    """
    
    import argparse
    import rclpy
    from rclpy.node import Node
    
    from robot_interfaces.msg import PathProgress
    from robot_interfaces.msg import LocalizedRobotPose
    from robot_interfaces.msg import ObstacleModel
    from robot_interfaces.msg import ObstacleCluster
    from robot_interfaces.msg import FreeSpaceModel
    
    VALID_SCENARIOS = ["normal", "front_blocked", "emergency", "goal_reached", "pose_stale", "no_path"]
    
    parser = argparse.ArgumentParser(description='FSM mock publisher')
    parser.add_argument('scenario', choices=VALID_SCENARIOS, help='테스트 시나리오')
    args, _ = parser.parse_known_args()
    SCENARIO = args.scenario
    
    class FsmMockPublisher(Node):
        def __init__(self):
            super().__init__('fsm_mock_publisher')
    
            self.path_progress_pub = self.create_publisher(
                PathProgress, '/navigation/path_progress/spot_01', 10)
            self.pose_pub = self.create_publisher(
                LocalizedRobotPose, '/localization/pose', 10)
            self.obstacle_pub = self.create_publisher(
                ObstacleModel, '/perception/lidar/obstacle_model', 10)
            self.free_space_pub = self.create_publisher(
                FreeSpaceModel, '/perception/lidar/free_space_model', 10)
    
            self.create_timer(0.1, self.publish_all)
            self.get_logger().info(f'[mock] 시나리오: {SCENARIO}')
    
        def publish_all(self):
            now = self.get_clock().now().to_msg()
    
            # PathProgress
            pp = PathProgress()
            pp.header.stamp    = now
            pp.header.frame_id = 'mission_map'
            pp.robot_id        = 'spot_01'
            pp.path_valid      = SCENARIO != 'no_path'
            pp.path_received   = SCENARIO != 'no_path'
            pp.pose_valid      = True
            pp.goal_reached    = SCENARIO == 'goal_reached'
            pp.distance_to_goal_m    = 0.1 if SCENARIO == 'goal_reached' else 5.0
            pp.distance_to_nearest_m = 0.1
            pp.distance_to_target_m  = 0.5
            self.path_progress_pub.publish(pp)
    
            # LocalizedRobotPose (pose_stale 시나리오는 publish 중단)
            if SCENARIO != 'pose_stale':
                pose = LocalizedRobotPose()
                pose.header.stamp    = now
                pose.header.frame_id = 'mission_map'
                pose.robot_id        = 'spot_01'
                pose.x_m             = 1.0
                pose.y_m             = 1.0
                pose.yaw_rad         = 0.0
                self.pose_pub.publish(pose)
    
            # ObstacleModel
            obs = ObstacleModel()
            obs.header.stamp    = now
            obs.header.frame_id = 'base_link'
    
            front = ObstacleCluster()
            if SCENARIO == 'front_blocked':
                front.valid               = True
                front.nearest_distance_xy = 0.50   # front_block_distance_m(0.70) 이내
            elif SCENARIO == 'emergency':
                front.valid               = True
                front.nearest_distance_xy = 0.20   # emergency_stop_distance_m(0.30) 이내
            else:
                front.valid = False
    
            obs.front             = front
            obs.obstacle_detected = front.valid
            self.obstacle_pub.publish(obs)
    
            # FreeSpaceModel
            fs = FreeSpaceModel()
            fs.header.stamp           = now
            fs.header.frame_id        = 'base_link'
            fs.path_available         = True
            fs.best_heading_angle_rad = 0.0
            fs.best_clearance         = 1.5
            self.free_space_pub.publish(fs)
    
    def main():
        rclpy.init()
        node = FsmMockPublisher()
        try:
            rclpy.spin(node)
        except KeyboardInterrupt:
            pass
        finally:
            node.destroy_node()
            rclpy.shutdown()
    
    if __name__ == '__main__':
        main()
    ```
    

### 시나리오별 동작

| scenario | 조작 내용 | 예상 state |
| --- | --- | --- |
| `normal` | 장애물 없음, path 정상 | NAV_TRACKING_GLOBAL_PATH (1) |
| `front_blocked` | front.nearest = 0.50m | NAV_PLANNING_LOCAL_PATH (2) |
| `emergency` | front.nearest = 0.20m | NAV_EMERGENCY_STOP (8) |
| `goal_reached` | goal_reached = true | NAV_GOAL_REACHED (7) |
| `pose_stale` | pose publish 중단 | NAV_LOCALIZATION_LOST (9) |
| `no_path` | path_valid = false | NAV_WAITING_FOR_PATH (0) |

### 결과 확인

```bash
ros2 topic echo /navigation/state/spot_01
```