# recv 벤치마크: baseline vs io_uring

## 측정 대상
첫 UDP fragment 송신 → SHM 슬롯 READY 전환까지의 레이턴시

| 항목 | 값 |
|---|---|
| FPS | 30 |
| 프레임 크기 | 48 KB (시뮬레이션 JPEG) |
| Fragment 수 | 36개/프레임 (1400B MTU 기준) |
| 총 recv 호출 | 1,080회/초 |
| 측정 프레임 | 300 |

---

## 빌드

```bash
# 의존성
sudo apt install liburing-dev   # RPi4 Raspberry Pi OS
# 또는
sudo dnf install liburing-devel

# 빌드
make all

# RPi4 크로스 컴파일 (x86 호스트에서)
make CC=aarch64-linux-gnu-gcc
```

## 실행

```bash
# 개별 실행
./bench_baseline
./bench_uring

# 비교 실행 (권장)
make compare
```

---

## 커널 요구사항

| 기능 | 최소 커널 |
|---|---|
| io_uring 기본 | 5.1 |
| IORING_OP_RECV | **5.6** (필수) |
| SINGLE_ISSUER | 6.0 |
| DEFER_TASKRUN | **6.1** (권장, jitter 최소화) |

RPi4 Raspberry Pi OS (Bookworm 기준): **6.1.x** → 모든 기능 사용 가능

---

## 이 머신에서 측정한 baseline 결과 (Linux 4.4.0, x86 container)

```
  버전                  mean     p50     p95     p99     max      σ
  baseline (recv)     ~1250   ~1060   ~2350   ~5600  ~9600    ~830   μs
```

※ io_uring은 Linux 5.6+ 필요, 이 환경에서는 실행 불가.

---

## RPi4에서 기대되는 결과

ARM Cortex-A72 (1.5GHz), 단일 코어 기준 추정치:

```
  버전                  mean     p50     p95     p99     max       σ      cpu
  baseline (recv)      ~800    ~650   ~1800   ~6000  ~8000   ~1200   ~2.0%
  io_uring             ~150     ~80    ~300    ~800  ~1500    ~150   ~0.3%

  개선 (예상):
    mean latency   -80%   (~800 → ~150 μs)
    p95  latency   -83%  (~1800 → ~300 μs)
    p99  latency   -87%  (~6000 → ~800 μs)
    jitter (σ)     -87%  (~1200 → ~150 μs)  ← 화면 끊김에 직결
    CPU            -85%    (~2% → ~0.3%)
```

### 왜 이 수치인가

| 오버헤드 | baseline | io_uring |
|---|---|---|
| recv() syscall × 1,080/sec | ~2.2ms/sec | ~0.06ms (배칭) |
| 스레드 wake-up 지연 | 10~50μs/frame | 없음 (completion loop) |
| DEFER_TASKRUN jitter | N/A | ±1~2μs |
| staging→SHM memcpy | 동일 | 동일 (fragment 재조립 필수) |

---

## 주의 사항

1. **loopback 벤치**이므로 실제 네트워크 latency는 포함되지 않는다.
2. **RPi4 thermal throttling**: 장시간 실행 시 클럭 저하 → 사전에 `vcgencmd measure_temp` 확인.
3. **SQPOLL은 사용하지 않는다**: RPi4(4코어)에서 코어 1개를 낭비하는 역효과.
4. **p99 jitter**가 실제 화면 끊김을 결정한다. mean보다 p99/max를 중점 비교할 것.

---

## 파일 구조

```
bench/
├── bench.h           — 공통 타입 / 상수
├── bench_utils.c     — now_ns(), 소켓, 재조립, 분석, 출력
├── recv_baseline.c   — 표준 recv() 구현
├── recv_uring.c      — io_uring + DEFER_TASKRUN 구현
├── bench_main.c      — 오케스트레이터 + 송신자 스레드
├── Makefile
└── README.md
```
