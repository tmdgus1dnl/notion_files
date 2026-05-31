# sim 연동 전까지 ubuntu ros2 연동

상태: Simulation

# Ubuntu PC ROS2 환경 구축 및 브릿지 연결 보고서

> 작성일 : 2026-04-29
프로젝트 : SPOT Get IT
담당 파트 : SIM / APP
> 

---

## 1. 개요

기존 Windows 11 환경에서의 ROS2 연결 실패(FastDDS 버전 문제, Zenoh DLL 미지원 등)를 해결하기 위해 Ubuntu 22.04 LTS 듀얼부팅 환경으로 전환하여 Jetson ↔ Ubuntu PC ROS2 통신을 구축한 과정을 기록합니다.

---

## 2. 환경

| 항목 | 내용 |
| --- | --- |
| Jetson | Jetson Orin Nano, Ubuntu 22.04, ROS2 Humble |
| Ubuntu PC | Ubuntu 22.04 LTS (듀얼부팅), ROS2 Humble |
| Jetson IP | 70.12.245.113 (Wi-Fi) |
| Ubuntu PC IP | 70.12.246.222 |
| DDS | FastDDS XML 유니캐스트 |

---

## 3. FastDDS XML 유니캐스트 설정

Windows에서 겪었던 FastDDS 2.6.7 버전 문제가 없으므로 동일한 유니캐스트 XML 방식을 적용합니다.

### Jetson `~/fastdds.xml`

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<profiles xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles">
    <participant profile_name="unicast_participant" is_default_profile="true">
        <rtps>
            <builtin>
                <metatrafficUnicastLocatorList>
                    <locator>
                        <udpv4>
                            <address>70.12.245.113</address>
                        </udpv4>
                    </locator>
                </metatrafficUnicastLocatorList>
                <initialPeersList>
                    <locator>
                        <udpv4>
                            <address>127.0.0.1</address>
                        </udpv4>
                    </locator>
                    <locator>
                        <udpv4>
                            <address>70.12.246.222</address>
                        </udpv4>
                    </locator>
                </initialPeersList>
            </builtin>
        </rtps>
    </participant>
</profiles>
```

### Ubuntu PC `~/fastdds.xml`

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<profiles xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles">
    <participant profile_name="unicast_participant" is_default_profile="true">
        <rtps>
            <builtin>
                <metatrafficUnicastLocatorList>
                    <locator>
                        <udpv4>
                            <address>70.12.246.222</address>
                        </udpv4>
                    </locator>
                </metatrafficUnicastLocatorList>
                <initialPeersList>
                    <locator>
                        <udpv4>
                            <address>127.0.0.1</address>
                        </udpv4>
                    </locator>
                    <locator>
                        <udpv4>
                            <address>70.12.245.113</address>
                        </udpv4>
                    </locator>
                </initialPeersList>
            </builtin>
        </rtps>
    </participant>
</profiles>
```

### Jetson `~/.bashrc` 추가 항목

```bash
export ROS_DOMAIN_ID=0
export ROS_LOCALHOST_ONLY=0
export FASTRTPS_DEFAULT_PROFILES_FILE=/home/jetson/fastdds.xml
```

### Ubuntu PC `~/.bashrc` 추가 항목

```bash
export ROS_DOMAIN_ID=0
export ROS_LOCALHOST_ONLY=0
export FASTRTPS_DEFAULT_PROFILES_FILE=/home/ubuntu/fastdds.xml
```

---

## 4. 통신 검증

### 4-1. 기본 토픽 통신 테스트

```bash
# Jetson
ros2 topic pub /test std_msgs/msg/String "data: 'hello from jetson'" --rate 1

# Ubuntu PC
ros2 topic echo /test
```

첫 메시지 1개 손실(FastDDS 디스커버리 완료 전 유실)은 정상이며 이후 정상 수신 확인. ✅

### 4-2. Jetson 로컬 노드 간 통신 테스트

Jetson 터미널 두 개에서 pub/sub 정상 동작 확인. ✅

### 4-3. 샘플 노드 4케이스 동시 검증

**Jetson `~/test_pub.py`**

```python
import rclpy
from rclpy.node import Node
from std_msgs.msg import String

class TestPub(Node):
    def __init__(self):
        super().__init__('jetson_pub_node')
        self.pub = self.create_publisher(String, '/test_bridge', 10)
        self.timer = self.create_timer(1.0, self.publish)
        self.count = 0

    def publish(self):
        msg = String()
        msg.data = f'from jetson #{self.count}'
        self.pub.publish(msg)
        self.get_logger().info(f'publish: {msg.data}')
        self.count += 1

def main():
    rclpy.init()
    rclpy.spin(TestPub())
    rclpy.shutdown()

if __name__ == '__main__':
    main()
```

**Jetson `~/test_sub.py`**

```python
import rclpy
from rclpy.node import Node
from std_msgs.msg import String

class TestSub(Node):
    def __init__(self):
        super().__init__('jetson_sub_node')
        self.create_subscription(String, '/test_bridge', self.callback, 10)
        self.get_logger().info('waiting for messages...')

    def callback(self, msg):
        self.get_logger().info(f'received: {msg.data}')

def main():
    rclpy.init()
    rclpy.spin(TestSub())
    rclpy.shutdown()

if __name__ == '__main__':
    main()
```

> Ubuntu PC용은 node명만 `pc_pub_node`, `pc_sub_node`로 다르고 코드는 동일합니다.
> 

**결과**

| 케이스 | Publisher | Subscriber | 결과 |
| --- | --- | --- | --- |
| 1 | Jetson | Jetson | ✅ |
| 2 | Jetson | Ubuntu PC | ✅ |
| 3 | Ubuntu PC | Jetson | ✅ |
| 4 | Ubuntu PC | Ubuntu PC | ✅ |

4개 터미널을 동시에 실행하여 Jetson sub, Ubuntu PC sub 모두 `from jetson #N`과 `from pc #N`을 섞어서 수신. 4케이스 동시 검증 완료.

---

## 5. robot_interfaces 빌드

Jetson에서 Ubuntu PC로 소스 복사 후 빌드합니다.

```bash
# Jetson에서 실행
scp -r ~/robot_ws/src/interfaces ubuntu@70.12.246.222:~/robot_ws/src/
```

```bash
# Ubuntu PC에서 실행
cd ~/robot_ws
colcon build --packages-select robot_interfaces
source install/setup.bash
```

**동작 확인**

```bash
python3 -c "from robot_interfaces.msg import RobotLocalization; print(RobotLocalization)"
# <class 'robot_interfaces.msg._robot_localization.RobotLocalization'>
```

✅ 빌드 성공

---

## 6. 트러블슈팅

| 증상 | 원인 | 해결 방법 | 상태 |
| --- | --- | --- | --- |
| `export FASTRTPS_DEFAULT_PROFILES_FILE`이 적용 안 됨 | 기존 `~/.bashrc`에 `unset FASTRTPS_DEFAULT_PROFILES_FILE`이 남아있어 무효화 | 해당 unset 3줄 제거 후 `~/.bashrc` 재작성 | ✅ 해결 |
| Ubuntu PC에서 `python` 명령어 없음 | Ubuntu 22.04 기본 설치에 python 심볼릭 링크 미포함 | `sudo apt install python-is-python3 -y` | ✅ 해결 |
| Jetson SSH 접속에서 `cv2.imshow` GTK 오류 | 디스플레이 없는 SSH 환경에서 GTK 백엔드 초기화 실패 | SSH 접속 시 `-X` 옵션으로 X11 포워딩 활성화 (`ssh -X jetson@70.12.245.113`) | ✅ 해결 |
| `robot_interfaces` colcon 빌드 실패 | `empy` 모듈 없음 | `pip install empy==3.3.4` | ✅ 해결 |