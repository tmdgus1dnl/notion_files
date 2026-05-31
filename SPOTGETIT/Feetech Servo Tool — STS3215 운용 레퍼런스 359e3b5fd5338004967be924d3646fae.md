# Feetech Servo Tool — STS3215 운용 레퍼런스

> 출처 저장소: [`dgmz/feetech-servo-tool`](https://github.com/dgmz/feetech-servo-tool)
라이선스: GPL-3.0 / 버전: v0.1.0 (2025-08)
본 문서는 저장소의 `servo.py`, `mainwindow.py` 코드를 직접 분석해 작성됨.
> 

---

## 1. 저장소 개요

**Feetech Servo Tool**은 공식 `FT_SCServo_Debug_Qt` (Linux 전용 C++)와 `FT_SCServo_Debug` (Windows 전용)의 기능을 **PyQt6로 포팅한 크로스플랫폼 디버깅/구성 도구**다.

- LeRobot 프로젝트의 SO-101 로봇팔(Feetech ST3215 사용) 운용을 위해 시작됨
- macOS (Intel/Apple Silicon), Windows, Linux(AppImage) 바이너리 제공
- 지원 시리즈: **STS, SCS, SMCL, SMBL** (그중 STS/SC가 검증됨)
- 미구현: 설정 파일 저장/로드

### 주요 파일 구조

```
feetech-servo-tool/
├── main.py                # 진입점
├── mainwindow.py          # UI 로직 (Debug 탭, Programming 탭)
├── servo.py               # 레지스터 맵 + 통신 래퍼
├── servobus.py            # 시리얼 버스 추상화
├── ui_mainwindow.py       # PyQt6 UI 정의 (자동 생성)
├── simplegraphwidget.py   # 실시간 그래프
└── doc/, icons/, build-*.sh
```

### UI 구성

- **Debug 탭**: 실시간 위치/토크/속도/전류/온도/전압/이동상태 표시 + 그래프 + Goal 슬라이더
- **Programming 탭**: 전체 메모리(EEPROM/SRAM) 테이블 뷰, 직접 읽기/쓰기

---

## 2. 지원 서보 모델 식별

`servo.py`의 `ServoModels` 딕셔너리가 모델번호 → 이름 매핑을 담고 있다. STS3215의 모델 ID는:

```python
SERVO_MODEL(9, 3): "STS3215"   # 즉 워드 = (3 << 8) | 9 = 0x0309
```

`getModelSeries()`가 이름 prefix를 보고 시리즈를 결정:

- `STS*` → STS
- `SC*` → SCS
- `SM*BL*` → SMBL
- 그 외 SM* → SMCL

각 시리즈마다 메모리 맵이 다르며, **이 도구는 시리즈별로 다른 레지스터 테이블을 참조**한다.

---

## 3. STS 시리즈(STS3215 포함) 레지스터 맵

`servo.py`의 `MemConfig["STS"]`. 형식: `(address, name, size, default, direction, is_eprom, is_readonly, min, max)`

### 3.1 EEPROM 영역 (전원 차단 시에도 유지)

| Addr | 이름 | Size | Default | Range | 단위/의미 |
| --- | --- | --- | --- | --- | --- |
| 0 | Firmware Main Version | 1 | 0 | R/O | — |
| 1 | Firmware Sub Version | 1 | 0 | R/O | — |
| 3 | Servo Main Version | 1 | 0 | R/O | — |
| 4 | Servo Sub Version | 1 | 0 | R/O | — |
| 5 | **ID** | 1 | 0 | 0–253 | 서보 식별자 |
| 6 | **Baud Rate** | 1 | 4 | 0–7 | 0=1M, 1=500k, 2=250k, 3=128k, 4=115200, 5=76800, 6=57600, 7=38400 |
| 7 | Return Delay Time | 1 | 250 | 0–254 | 2 µs 단위 |
| 8 | Status Return Level | 1 | 1 | 0–1 | 0=무응답, 1=응답 |
| 9 | **Min Position Limit** | 2 | 0 | step | 위치 하한 (0–4095 ≈ 360°, 1 step ≈ 0.088°) |
| 11 | **Max Position Limit** | 2 | 0 | step | 위치 상한 |
| 13 | **Max Temperature Limit** | 1 | 80 | 0–100 | °C |
| 14 | **Max Input Voltage** | 1 | 140 | 0–254 | 0.1 V (140 = 14.0 V) |
| 15 | **Min Input Voltage** | 1 | 80 | 0–254 | 0.1 V (80 = 8.0 V) |
| 16 | **Max Torque Limit** | 2 | 1000 | 0–1000 | **0.1 % (1000 = 100.0 %)** |
| 18 | Setting Byte | 1 | 0 | 0–254 | 비트 플래그 |
| 19 | Protection Switch | 1 | 37 | 0–254 | 보호 활성 플래그 |
| 20 | LED Alarm Condition | 1 | 37 | 0–254 | LED 알람 플래그 |
| 21 | **Position P Gain** | 1 | 32 | 0–254 | 게인 |
| 22 | **Position D Gain** | 1 | 0 | 0–254 | 게인 |
| 23 | **Position I Gain** | 1 | 0 | 0–254 | 게인 |
| 24 | **Punch** | 2 | 0 | 0–1000 | 0.1 %, 정지마찰 극복용 최소 출력 |
| 26 | CW Dead Band | 1 | 0 | 0–32 | step (불감대) |
| 27 | CCW Dead Band | 1 | 0 | 0–32 | step |
| 28 | Overload Current | 2 | 0 | 0–511 | ≈ 6.5 mA 단위 |
| 30 | Angular Resolution | 1 | 1 | 1–100 | 분해능 배율 |
| 31 | Position Offset Value | 2 | 0 | -2047–2047 | step (영점 보정) |
| 33 | **Work Mode** | 1 | 0 | 0–3 | 0=위치, 1=휠/연속회전, 2=PWM 오픈루프, 3=Step |
| 34 | **Protect Torque** | 1 | 40 | 0–254 | %, 보호 발동 후 유지 토크 |
| 35 | **Overload Protection Time** | 1 | 80 | 0–254 | 10 ms 단위 |
| 36 | **Overload Torque** | 1 | 80 | 0–254 | %, 과부하 임계치 |
| 37 | Velocity P Gain | 1 | 32 | 0–254 | (Work Mode 1에서만 의미) |
| 38 | Overcurrent Protection Time | 1 | 100 | 0–254 | 10 ms |
| 39 | Velocity I Gain | 1 | 0 | 0–254 | (Work Mode 1에서만 의미) |

### 3.2 SRAM 영역 (런타임)

| Addr | 이름 | Size | Default | Range | 단위/의미 |
| --- | --- | --- | --- | --- | --- |
| 40 | **Torque Enable** | 1 | 0 | 0–254 | 0=OFF, 1=ON, 128=현재 위치 영점 보정 |
| 41 | **Goal Acceleration** | 1 | 0 | 0–254 | 100 step/s² |
| 42 | **Goal Position** | 2 | 0 | -32766–32766 | step |
| 46 | **Goal Velocity** | 2 | 0 | -1000–1000 | step/s |
| 48 | **Torque Limit** | 2 | 1000 | 0–1000 | **0.1 %, 런타임 토크 상한** |
| 55 | **Lock** | 1 | 1 | 0–1 | EEPROM 쓰기 잠금 |
| 56 | Current Position | 2 | R/O | step (부호 16-bit) | — |
| 58 | Instantaneous Velocity | 2 | R/O | step/s | — |
| 60 | **Current PWM (= UI의 Torque)** | 2 | R/O | 10-bit + 부호 | **0.1 % 단위 부하율** |
| 62 | Instantaneous Input Voltage | 1 | R/O | 0.1 V | — |
| 63 | Current Temperature | 1 | R/O | °C | — |
| 64 | Sync Write Flag | 1 | R/O | flag | — |
| 65 | **Hardware Error Status** | 1 | R/O | bit flags | 과전압/저전압/과온/과부하/각도 |
| 66 | Moving Status | 1 | R/O | 0/1 | 0=정지, 1=이동 중 |
| 69 | Instantaneous Current | 2 | R/O | ≈ 6.5 mA | — |

---

## 4. 핵심: 토크 단위 체계

### 4.1 모든 토크 관련 값은 0.1 % 단위, 풀스케일 1000

| 자리 | 단위 | 100 % 값 |
| --- | --- | --- |
| Max Torque Limit (16) | 0.1 % | 1000 |
| Torque Limit (48) | 0.1 % | 1000 |
| Punch (24) | 0.1 % | 1000 |
| Current PWM = UI Torque (60) | 0.1 % | ±1000 (인코딩상 ±1023까지 가능) |

```
실제 % = 레지스터 값 / 10
예) 48 → 4.8 %, 500 → 50 %, 1000 → 100 %
```

### 4.2 Current PWM(주소 60)의 인코딩 함정

`servo.py`의 `read_load()`:

```python
def read_load(self, id):
    val, res, error = handler.read2ByteTxRx(id, 60)
    if 0 == res:
        return handler.scs_tohost(val, 10)   # 10-bit 크기 + 부호 비트
    return 0
```

- raw 16-bit 워드: 비트 10(=0x400)이 **부호 비트**, 비트 0–9가 크기(0–1023)
- 부호 디코딩 전 raw가 1072로 보이면 → 1072 - 1024 = 48 → 부호 적용 후 **48**
- **메인 화면의 "Torque" 라벨은 디코딩 후 값**, "Current PWM" raw는 디코딩 전 값
- 보통 펌웨어는 ±1000 범위에서 동작, 1001–1023은 인코딩상 헤드룸

### 4.3 토크 한계 적용 흐름

```
[명령 토크] ─→ min(Torque Limit[48], Max Torque Limit[16]) ─→ [실제 출력]
                                                                  │
                                                                  ▼
                                                         Current PWM[60] 측정
                                                                  │
                            Overload Torque[36] 초과가 Time[35] 지속이면
                                                                  ▼
                                       Protect Torque[34] 수준으로 강제 강하
                                       + Hardware Error[65] 비트 set
```

---

## 5. UI ↔ 레지스터 매핑 (Debug 탭)

`mainwindow.py`의 `onServoReadTimerTimeout()` / `onGraphTimerTimeout()` 분석 결과:

| UI 라벨 | 내부 변수 | 호출 함수 | 실제 레지스터 |
| --- | --- | --- | --- |
| Position | `latest_pos_` | `read_position` | 56 |
| **Torque** | `latest_torque_` | **`read_load`** | **60 (10-bit + 부호 디코딩)** |
| Speed | `latest_speed_` | `read_speed` | 58 |
| Current | `latest_current_` | `read_current` | 60 (raw word, 비변환) |
| Temperature | `latest_temp_` | `read_temperature` | 63 |
| Voltage | `latest_voltage_` × 0.1 → "X.X V" | `read_voltage` | 62 |
| Moving | `latest_move_` | `read_move` | 66 |
| Goal | `latest_goal_` | `read_goal` | 42 |

> 주의: UI에서 "Current"라는 별도 라벨이 있지만, 코드를 보면 그것 역시 주소 60을 read_word한 값(부호 미디코딩)이다. 즉 Debug 탭의 "Current"와 "Torque"는 같은 워드의 두 표현인 셈. **사용자가 봐야 할 것은 "Torque"** (부호 적용된 값).
> 

---

## 6. 4족 보행 로봇 운용 가이드

### 6.1 안전·보호 파라미터 권장값

| 항목 (주소) | 기본값 | 권장 시작값 | 의미 |
| --- | --- | --- | --- |
| Max Torque Limit (16) | 1000 | **800** | 절대 토크 상한 80 %로 시작, 안정 후 상향 |
| Torque Limit (48) | 1000 | 800 (보행) / 250 (캘리브) | 런타임 동적 변경 |
| Protect Torque (34) | 40 (4 %) | **30–50** | 보호 발동 후에도 자세 유지 |
| Overload Torque (36) | 80 (80 %) | **70** | 보행 평균 부하보다 약간 위 |
| Overload Protection Time (35) | 80 (800 ms) | **50 (500 ms)** | 빠른 셧다운 |
| Overload Current (28) | 0 | **300–460 (≈ 2–3 A)** | BEC/배터리 보호 |
| Max Temperature Limit (13) | 80 °C | **70 °C** | 수명 보호 |
| Min Input Voltage (15) | 80 (8.0 V) | **90 (9.0 V)** | 저전압 시 토크 부족으로 인한 다리 꺾임 방지 |
| Punch (24) | 0 | **20–50 (2–5 %)** | 정지마찰 극복 |

### 6.2 시스템 구성

- **ID (5)**: 12개 모터 모두 다른 ID(1–12)로 미리 부여
- **Baud Rate (6)**: 12개 서보 50–100 Hz 제어 시 **1 Mbps(인덱스 0) 권장**
- **Min/Max Position Limit (9/11)**: 기구 간섭 각도를 펌웨어 수준에서 차단
- **Position Offset (31)**: 조립 후 영점 캘리브레이션 필수
- **Work Mode (33)**: 0(위치 서보) 고정
- **Lock (55)**: 셋업 후 1로 두어 EEPROM 실수 쓰기 방지

### 6.3 Torque 안정선 (절댓값 기준, 0.1 % 단위)

| |Torque| | % | 평가 | 권장 행동 |
| --- | --- | --- | --- |
| 0–100 | 0–10 | 🟢 매우 안정 | 정지 자세 이상적 |
| 100–300 | 10–30 | 🟢 정상 | 일반 보행 |
| 300–500 | 30–50 | 🟡 주의 | 동적 동작은 OK, 정지 시 지속이면 자세 재분배 |
| 500–700 | 50–70 | 🟠 경고 | 발열 빠름, 5–10분 이상 지속 금지 |
| 700–850 | 70–85 | 🔴 위험 | 즉시 자세 변경 |
| 850–1000+ | 85–100 | ⛔ 스톨 임박 | 즉시 정지 |

### 6.4 모드별 안전 한계

| 상황 | |Torque| 안정선 | 일시 허용 (수 초) |
| --- | --- | --- |
| 정지 자세 | < 200 (20 %) | 350 |
| 일반 보행 | < 400 (40 %) | 600 |
| 빠른 보행/방향 전환 | < 500 (50 %) | 750 |
| 점프/낙하 충격 (순간) | — | < 850 |
| 캘리브레이션/티칭 | < 100 (10 %) | — |

### 6.5 실시간 모니터링 우선순위

| 우선순위 | 값 | 주소 | 정상 | 위험 신호 |
| --- | --- | --- | --- | --- |
| ★★★ | **Torque** (UI 라벨) | 60(디코딩 후) | |값| < 500 | 자주 ±1000 도달 |
| ★★★ | Hardware Error Status | 65 | 0x00 | 비트가 서면 즉시 안전자세 |
| ★★ | Instantaneous Current | 69 | 단일 < 2 A | 12개 합산 BEC 한계 초과 |
| ★★ | Current Temperature | 63 | < 60 °C | 70 °C+ 지속 → 듀티 감소 |
| ★ | Moving Status | 66 | 1 (이동 중) | 명령 후 0 → 토크 부족 |

---

## 7. 통신/SDK 관련 코드 패턴

### 7.1 위치 명령

```python
# 단일 서보 (가속도 포함, STS 권장)
sms_sts_proto_.write_pos_ex(id, goal, speed, acc)

# 다중 서보 동기 명령 (4족 보행 필수)
sms_sts_proto_.sync_write_pos_ex(ids, goals, speeds, accels)

# 트리거 대기 명령 (Action 명령으로 일괄 실행)
sms_sts_proto_.reg_write_pos_ex(id, goal, speed, acc)
servo_bus_.reg_write_action(id)
```

### 7.2 토크 ON/OFF

```python
sms_sts_proto_.enable_torque(id, True)   # 주소 40에 1 쓰기
sms_sts_proto_.enable_torque(id, False)  # 주소 40에 0 쓰기
```

### 7.3 ID 변경 절차 (Lock 해제 → 변경 → Lock)

```python
servo_bus_.write_byte(id, 55, 0)   # unlock
servo_bus_.write_byte(id, 5, new_id)
servo_bus_.write_byte(new_id, 55, 1)  # lock
```

---

## 8. 흔한 실수와 주의점

1. **Max Torque Limit를 1000(100 %)으로 두고 무거운 보행 시작** → 끼임 시 코일 소손
2. **Torque Enable=0 상태로 보행 명령** → 다리 풀림. 명령 전 1로 전환 필수
3. **EEPROM(주소 16)을 매 사이클마다 갱신** → EEPROM 수명 소모. **RAM(주소 48)을 사용**
4. **개별 WritePos로 12개 모터 순차 명령** → 동기화 깨짐. **반드시 SyncWrite 사용**
5. **Overload 기본값(80 %·800 ms) 그대로** → 사고 시 보호 지연으로 모터·기어 먼저 망가짐
6. **부호 디코딩 안 된 raw 워드를 임계치 비교에 사용** → 1072 같은 값이 나와 오판. `read_load()` 결과(부호 적용)를 사용할 것
7. **각 관절의 Position Limit를 안 걸어둠** → 소프트웨어 버그 한 번에 다리 자기 몸 부수기

---

## 9. 빌드 및 실행

```bash
# 공통
git clone <https://github.com/dgmz/feetech-servo-tool.git>
cd feetech-servo-tool
python -m venv venv

# Windows
venv\\Scripts\\activate
pip install -r requirements.txt
build-win.bat

# macOS
source venv/bin/activate
pip install -r requirements.txt
sh build-mac.sh

# Linux
source venv/bin/activate
pip install -r requirements.txt
sh build-linux.sh
```

빌드 결과물은 `dist/` 디렉터리에 생성된다.

---

## 10. 알려진 한계

- 설정 파일 저장/로드 미구현
- SCS/STS 외 시리즈는 코드상 정의되어 있으나 미검증
- AppImage는 Ubuntu 24.10 기반 빌드라 구버전 배포판에서 동작 불안정 가능
- `onAutoDebugTimerTimeout`에 `auto_debug_timer.stop()` 오타 존재 (코드 리뷰 시 발견)

---

## 11. 빠른 참조 카드

```
풀스케일 = 1000 = 100.0 %  (모든 토크 관련 레지스터 공통)
1 카운트 = 0.1 %
UI "Torque" = 부호 디코딩 끝난 값 (그대로 사용 OK)
UI "Current PWM" raw = 부호 비트(0x400) 미디코딩, 직접 비교 금지

정지 시 안전선:  |Torque| < 200  (20 %)
보행 시 안전선:  |Torque| < 400  (40 %)
즉시 정지선:     |Torque| > 850  (85 %)

Position 1 step ≈ 0.088° (12-bit, 0–4095 ≈ 360°)
Voltage   1 카운트 = 0.1 V
Current   1 카운트 ≈ 6.5 mA
Time      1 카운트 = 10 ms (보호 시간) / 2 µs (Return Delay)

영점 보정: Torque Enable = 128 (주소 40)
EEPROM 잠금: Lock = 1 (주소 55)
```