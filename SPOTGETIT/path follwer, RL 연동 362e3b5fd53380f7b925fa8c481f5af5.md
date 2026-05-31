# path follwer, RL 연동

source install/setup.bash
ros2 launch robot_bringup rl_motion_manager.launch.py

이 launch가 띄우는 핵심 노드는:

rl_locomotion_node
stand_motion_node
joint_target_mux_node
actuator_bridge_node

흐름은:

STAND:
stand_motion_node
-> /control/stand/joint_target
-> joint_target_mux_node
-> /control/selected/joint_target
-> actuator_bridge_node
-> STM32

RL:
/control/cmd_vel/spot_01
-> rl_locomotion_node
-> /control/rl/joint_target
-> joint_target_mux_node
-> /control/selected/joint_target
-> actuator_bridge_node
-> STM32

기본값은 STAND야. 그래서 launch만 하면 일단 stand pose 쪽으로 감.

명시적으로 STAND:

ros2 topic pub /control/behavior/mode std_msgs/msg/String "{data: 'STAND'}" --once

RL로 전환:

ros2 topic pub /control/behavior/mode std_msgs/msg/String "{data: 'RL'}" --once

RL로 걷게 하려면 /control/cmd_vel/spot_01에 geometry_msgs/msg/Twist가 들어와야 함. 수동 테스트는 이
렇게:

ros2 topic pub -r 10 /control/cmd_vel/spot_01 geometry_msgs/msg/Twist \
"{linear: {x: 0.05, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}"

회전 테스트:

ros2 topic pub -r 10 /control/cmd_vel/spot_01 geometry_msgs/msg/Twist \
"{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.1}}"

네 path follower까지 연결하려면, 최종적으로는 navigation 쪽에서:

path_follower_node
-> /control/cmd_vel_raw/spot_01  TwistStamped

safety_supervisor_node
-> /control/cmd_vel/spot_01      Twist

이렇게 나오게 한 뒤, 위 rl_motion_manager.launch.py를 같이 띄우고 mode를 RL로 바꾸면 된다.

확인 명령:

ros2 topic info -v /control/cmd_vel/spot_01
ros2 topic echo /control/rl/debug --once
ros2 topic echo /control/selected/joint_target --once

/control/rl/debug에서 cmd_vx, cmd_wz가 들어오면 RL 입력까지 연결된 거고, /control/selected/
joint_target이 나오면 mux를 지나 STM32 bridge로 갈 준비가 된 거야.