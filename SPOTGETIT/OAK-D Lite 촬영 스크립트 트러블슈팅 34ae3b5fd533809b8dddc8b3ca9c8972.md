# OAK-D Lite 촬영 스크립트 트러블슈팅

상태: VISION

# OAK-D Lite 촬영 스크립트 트러블슈팅 보고서

> 작성일 : 2026-04-22
프로젝트 : SPOT Get IT
> 

---

## 1. 개요

파인튜닝 데이터 수집을 위한 OAK-D Lite 촬영 스크립트(`capture.py`) 개발 및 초기 운용 과정에서 발생한 문제와 해결 과정을 기록합니다.

---

## 2. 트러블슈팅

### 2-1. 디바이스 연결 실패 (X_LINK_DEVICE_NOT_FOUND)

**증상**

```
RuntimeError: Failed to find device after booting, error message: X_LINK_DEVICE_NOT_FOUND
```

**원인**

초기 코드에서 `dai.Pipeline()`만으로 파이프라인을 구성하고 실행했으나 DepthAI v3에서는 디바이스를 명시적으로 탐색하지 않으면 연결에 실패하는 경우가 있음

**해결**

`dai.Device.getAllAvailableDevices()`로 연결된 디바이스를 먼저 탐색하고 존재 여부를 확인 후 파이프라인을 시작하는 방식으로 변경

```python
devices = dai.Device.getAllAvailableDevices()
if not devices:
    print("[ERROR] 연결된 OAK 디바이스 없음")
    return
```

| 상태 |  |
| --- | --- |
| 해결 여부 | ✅ 해결 |

---

### 2-2. getMxId() AttributeError

**증상**

```
AttributeError: 'depthai.DeviceInfo' object has no attribute 'getMxId'
```

**원인**

DepthAI v2 기준 API인 `getMxId()`가 v3에서 제거됨

**해결**

`getMxId()` → `getDeviceId()`로 변경

| 상태 |  |
| --- | --- |
| 해결 여부 | ✅ 해결 |

---

### 2-3. pipeline.start() 인자 오류

**증상**

```
TypeError: start(): incompatible function arguments.
Invoked with: <depthai.Pipeline object>, DeviceInfo(...)
```

**원인**

DepthAI v3에서 `pipeline.start()`는 인자를 받지 않음. 디바이스는 자동으로 탐색됨

**해결**

`pipeline.start(device_info)` → `pipeline.start()`로 변경

| 상태 |  |
| --- | --- |
| 해결 여부 | ✅ 해결 |

---

### 2-4. 원격 접속 끊김 (1080P 부하)

**증상**

`capture.py` 실행 시 MobaXterm 원격 접속이 끊김

**원인**

1080P(1920x1080) 해상도로 카메라 스트림을 받으면서 imshow까지 처리하면 Orin Nano에 과부하가 발생하여 시스템이 불안정해짐

**해결**

- 저장은 1080P 유지 (원본 품질 보존)
- imshow 미리보기는 854x480으로 다운스케일

```python
preview = cv2.resize(frame, (854, 480))
cv2.imshow("Preview", preview)
```

| 상태 |  |
| --- | --- |
| 해결 여부 | ✅ 해결 |

---

### 2-5. OAK-D Lite USB 2.0 포트 연결

**증상**

```
[ERROR] 연결된 OAK 디바이스 없음
```

**원인**

OAK-D Lite를 USB 2.0 포트(Bus 01, 480M)에 연결한 경우 디바이스 부팅 실패. OAK-D Lite는 USB 3.0 연결이 필요함

**Orin Nano USB 포트 스펙**

| 포트 | 규격 | 속도 | 비고 |
| --- | --- | --- | --- |
| Type-A × 4 | USB 3.2 Gen 2 | 최대 10Gbps | 스택당 VBUS 3A 제한 |
| Type-C × 1 | USB 3.2 | - | Host/Device/Recovery |

> Bus 02(10000M)에 연결된 포트가 USB 3.0입니다. OAK-D Lite는 반드시 Bus 02 포트에 연결하십시오.
> 

**해결**

USB 3.0 포트로 재연결

| 상태 |  |
| --- | --- |
| 해결 여부 | ✅ 해결 |

---

### 2-6. 파일명 날짜 오류 (시스템 날짜 미설정)

**증상**

실제 촬영일(2026-04-22)과 다른 날짜(20260324)로 파일명과 디렉토리명이 저장됨

**원인**

Orin Nano 시스템 날짜가 올바르게 설정되지 않은 상태에서 촬영 진행

**해결**

`fix_captures.py` 작성. 잘못된 디렉토리명과 올바른 첫 촬영 시간을 입력하면 파일명을 일괄 수정함

```bash
python fix_captures.py 20260324 20260422_091534
```

동작 방식:

1. 대상 디렉토리의 파일을 파일명 기준으로 정렬
2. 첫 파일 시간과 각 파일의 시간 차이 계산
3. 입력받은 올바른 첫 촬영 시간 + 차이를 적용하여 파일명 변경
4. 디렉토리명도 올바른 날짜로 변경

| 상태 |  |
| --- | --- |
| 해결 여부 | ✅ 해결 |

---

### 2-7. fix_captures.py에서 불필요한 suffix 생성

**증상**

원본에서는 다른 시간의 파일인데 변환 후 같은 시간으로 계산되어 `_1`, `_2` suffix가 붙음

**원인**

파일 수정 시간(mtime)을 기준으로 시간 차이를 계산했는데, mtime 정밀도가 초 단위라 연산 후 같은 초로 떨어지는 경우 발생

**해결**

mtime 대신 파일명에서 직접 시간을 파싱하는 방식으로 변경. 파일명이 이미 초 단위로 기록되어 있으므로 정밀도 문제가 없음

```python
def parse_stem(f):
    parts = f.stem.split("_")
    if len(parts) == 3 and parts[2].isdigit():  # YYYYMMDD_HHMMSS_N
        dt = datetime.strptime(f"{parts[0]}_{parts[1]}", "%Y%m%d_%H%M%S")
        return dt, f"_{parts[2]}"
    dt = datetime.strptime(f"{parts[0]}_{parts[1]}", "%Y%m%d_%H%M%S")
    return dt, ""
```

capture.py에서 같은 초에 촬영된 파일에 붙이는 `_1`, `_2` suffix는 변환 후에도 그대로 유지됨

| 상태 |  |
| --- | --- |
| 해결 여부 | ✅ 해결 |

---

## 3. 파일 저장 규격 (최종 확정)

| 항목 | 내용 |
| --- | --- |
| 저장 경로 | `./captures/YYYYMMDD/` |
| 파일명 | `YYYYMMDD_HHMMSS.jpg` |
| 동일 초 촬영 시 | `YYYYMMDD_HHMMSS_1.jpg`, `_2.jpg` ... |
| 저장 해상도 | 1920x1080 (1080P) |
| 미리보기 해상도 | 854x480 |
| 조작 | `c` : 캡쳐 / `q` : 종료 |