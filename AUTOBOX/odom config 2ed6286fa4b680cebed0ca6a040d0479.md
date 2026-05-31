# odom config

상태: Not started
생성자: 경근 배

```yaml
ekf_filter_node:
  ros__parameters:
    frequency: 30.0
    sensor_timeout: 0.2
    two_d_mode: true

    map_frame: map
    odom_frame: odom
    base_link_frame: base_link
    world_frame: odom

    publish_tf: true
    print_diagnostics: true

    # -------------------------
    # 1) Wheel Odometry (encoder)
    # -------------------------
    odom0: /odom_wheel
    odom0_config: [ true,  true,  false,
                    false, false, true,
                    true,  false, false,
                    false, false, true,
                    false, false, false ]
    odom0_differential: false
    odom0_relative: false
    odom0_queue_size: 10

    # -------------------------
    # 2) LiDAR Odometry (rf2o)
    # -------------------------
    odom1: /odom_rf2o
    odom1_config: [ true,  true,  false,
                    false, false, true,
                    false, false, false,
                    false, false, false,
                    false, false, false ]
    odom1_differential: false
    odom1_relative: false
    odom1_queue_size: 10

    # -------------------------
    # 3) IMU
    # -------------------------
    imu0: /imu/data
    imu0_config: [ false, false, false,
                   false, false, true,
                   false, false, false,
                   false, false, true,
                   false, false, false ]
    imu0_differential: false
    imu0_relative: true
    imu0_remove_gravitational_acceleration: true
    imu0_queue_size: 20

```

x, y, z,
roll, pitch, yaw,
vx, vy, vz,
vroll, vpitch, vyaw,
ax, ay, az