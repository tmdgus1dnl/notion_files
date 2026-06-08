# Bridge App 락프리 기법 적용 상세 보고서

작성일: 2026-04-28

## 1. 결론 요약

이 프로젝트에서 락프리 기법은 전체 시스템에 일괄 적용된 것이 아니라, **공유메모리에서 고빈도/대용량 데이터의 최신 슬롯을 교체하는 지점**과 **연결 상태/패킷 카운터 같은 작은 상태값을 공유하는 지점**에 선택적으로 적용되어 있다.

핵심 구현은 `shm_def.h`의 `_Atomic` 필드 정의와 `reassembly_shm.c`, `jetson_rx.c`, `bridge_main.c`, `pc_link.c`의 `atomic_load`, `atomic_store`, `atomic_fetch_add` 호출이다. 반대로 `frag_queue.h`의 프래그먼트 큐, `JetsonAddrTable`, Odom, 드롭 카운터는 mutex/rwlock 기반이므로 락프리 영역이 아니다.

## 2. 락프리 적용 위치 한눈에 보기

| 기능 | 락프리 여부 | 주요 소스 | 핵심 코드 |
|---|---:|---|---|
| 이미지 최신 프레임 공개 | 적용 | `shm_def.h`, `reassembly_shm.c` | `img_slots[3]`, `img_ready_idx`, `atomic_load/store` |
| LiDAR 최신 프레임 공개 | 적용 | `shm_def.h`, `reassembly_shm.c` | `lidar_slots[3]`, `lidar_ready_idx`, `atomic_load/store` |
| Jetson 연결 상태 표시 | 적용 | `shm_def.h`, `jetson_rx.c`, `bridge_main.c`, `pc_link.c` | `meta.jetson_connected`, `atomic_load/store` |
| 패킷 수신 카운터 | 적용 | `shm_def.h`, `jetson_rx.c`, `bridge_main.c` | `meta.pkt_count`, `atomic_fetch_add/load` |
| 새 이미지/LiDAR 도착 알림 | 부분 적용 아님 | `shm_def.h`, `reassembly_shm.c` | `sem_t`, `sem_post` |
| UDP fragment 큐 | 미적용 | `frag_queue.h`, `jetson_rx.c`, `reassembly_shm.c` | `pthread_mutex_t`, `pthread_cond_t` |
| Odom 공유 | 미적용 | `shm_def.h`, `jetson_rx.c`, `pc_link.c` | `pthread_rwlock_t odom_lock` |
| 드롭 카운터 | 미적용 | `shm_def.h`, `reassembly_shm.c`, `bridge_main.c`, `pc_link.c` | `pthread_rwlock_t meta_lock` |
| Jetson 주소 테이블 | 미적용 | `bridge_ctx.h`, `jetson_rx.c`, `cmd_dispatch.c`, `protocol_timer.c` | `pthread_mutex_t mu` |

여기서 말하는 락프리는 `pthread_mutex_lock()` 같은 사용자 공간 락을 잡지 않고 원자 연산으로 공유 상태를 갱신한다는 의미다. C11 atomics가 실제 CPU에서 항상 완전한 하드웨어 lock-free 명령으로 내려가는지는 타입/플랫폼에 따라 달라질 수 있지만, 이 코드의 `int`, `uint8_t` 원자 필드는 일반적인 ARM/Linux 환경에서 매우 작은 원자 연산으로 처리되는 구조다.

## 3. 공유메모리 원자 필드 정의

### 3.1 소스 위치

- `shm_def.h:23-31`
- `shm_def.h:74-82`
- `shm_def.h:87-95`

### 3.2 구현 내용

`shm_def.h`는 C와 C++ 양쪽에서 같은 공유메모리 레이아웃을 쓰도록 원자 타입을 매크로로 감싼다.

```c
#ifdef __cplusplus
#include <atomic>
#define ATOMIC_UINT8 std::atomic<uint8_t>
#define ATOMIC_INT std::atomic<int>
#else
#include <stdatomic.h>
#define ATOMIC_UINT8 _Atomic uint8_t
#define ATOMIC_INT _Atomic int
#endif
```

BridgeDaemon은 C 코드이므로 `_Atomic` 타입으로 컴파일되고, Qt 쪽 C++ 코드는 같은 헤더를 포함하면 `std::atomic` 타입으로 접근할 수 있게 설계되어 있다.

공유메모리 안의 락프리 핵심 필드는 다음과 같다.

```c
ATOMIC_UINT8 jetson_connected;
ATOMIC_INT   pkt_count;
ATOMIC_INT   img_ready_idx;
ATOMIC_INT   lidar_ready_idx;
```

각 필드의 역할은 다음과 같다.

| 필드 | 역할 | Writer | Reader |
|---|---|---|---|
| `img_ready_idx` | 최신 이미지가 들어 있는 슬롯 번호 | `reassembly_shm.c` | Qt App |
| `lidar_ready_idx` | 최신 LiDAR가 들어 있는 슬롯 번호 | `reassembly_shm.c` | Qt App |
| `jetson_connected` | Jetson 연결 상태 | `jetson_rx.c`, `bridge_main.c` | `bridge_main.c`, `pc_link.c`, Qt App |
| `pkt_count` | watchdog용 누적 수신 패킷 수 | `jetson_rx.c` | `bridge_main.c` |

## 4. 이미지 Triple Buffer 락프리 공개

### 4.1 소스 위치

- 슬롯 정의: `shm_def.h:40-52`, `shm_def.h:87-90`
- 초기화: `bridge_main.c:109-112`
- 커밋 구현: `reassembly_shm.c:88-115`
- 커밋 호출: `reassembly_shm.c:254-260`

### 4.2 동작 흐름

이미지는 `IMG_SLOTS`가 3으로 정의된 Triple Buffer를 사용한다.

1. `reassembly_shm_thread()`가 fragment queue에서 조각을 꺼내 프레임을 재조립한다.
2. `frags_recv == frags_total`이 되면 `commit_image()`를 호출한다.
3. `commit_image()`는 현재 공개된 슬롯 번호를 `atomic_load(&shm->img_ready_idx)`로 읽는다.
4. 다음 슬롯 `(ready + 1) % IMG_SLOTS`를 쓰기 대상으로 선택한다.
5. 완성된 JPEG 데이터를 `img_slots[slot]`에 `memcpy()`로 복사한다.
6. 복사가 끝난 뒤 `atomic_store(&shm->img_ready_idx, slot)`로 최신 슬롯 번호만 원자적으로 교체한다.
7. `sem_post(&shm->img_sem)`로 Qt reader를 깨운다.

핵심은 **데이터 복사가 끝난 뒤 인덱스를 공개한다**는 순서다. reader는 슬롯 인덱스만 원자적으로 읽으면 완성된 슬롯을 볼 수 있고, writer와 reader 사이에 별도 mutex를 잡지 않는다.

### 4.3 코드 단위 설명

`bridge_main.c:109-112`

```c
atomic_store(&shm->img_ready_idx, -1);
atomic_store(&shm->lidar_ready_idx, -1);
```

공유메모리 생성 직후 아직 공개된 프레임이 없다는 뜻으로 ready index를 `-1`로 초기화한다.

`reassembly_shm.c:91-95`

```c
int ready = atomic_load(&shm->img_ready_idx);
int slot = 0;
if (ready >= 0) {
    slot = (ready + 1) % IMG_SLOTS;
}
```

현재 최신 슬롯을 원자적으로 읽고 다음 슬롯을 계산한다. 이 구간에는 `pthread_mutex_lock()`이 없다.

`reassembly_shm.c:104-111`

```c
ImgSlot *s = &shm->img_slots[slot];
memcpy(s->data, rs->buf, size);
s->size         = size;
s->frame_id     = rs->frame_id;
s->timestamp_us = rs->timestamp_us;
atomic_store(&shm->img_ready_idx, slot);
```

슬롯 내부 데이터를 먼저 채운 다음, 마지막에 `img_ready_idx`를 갱신한다. 즉 `img_ready_idx`가 publication pointer 역할을 한다.

### 4.4 효과

- 이미지 복사 중 Qt reader가 mutex 대기하지 않는다.
- Qt 렌더링이 느려도 BridgeDaemon의 재조립 스레드는 슬롯에 계속 쓸 수 있다.
- reader는 매번 최신 index만 보면 되므로 오래된 프레임은 자연스럽게 건너뛸 수 있다.
- 대용량 이미지 전체에 락을 걸지 않고 작은 `int` 인덱스만 원자적으로 바꾼다.

## 5. LiDAR Triple Buffer 락프리 공개

### 5.1 소스 위치

- 슬롯 정의: `shm_def.h:43-72`, `shm_def.h:92-95`
- 초기화: `bridge_main.c:109-112`
- 커밋 구현: `reassembly_shm.c:117-162`
- 커밋 호출: `reassembly_shm.c:261-263`

### 5.2 동작 흐름

LiDAR도 이미지와 같은 패턴을 사용한다.

1. 현재 공개된 LiDAR 슬롯을 `atomic_load(&shm->lidar_ready_idx)`로 읽는다.
2. 다음 슬롯을 `(ready + 1) % LIDAR_SLOTS`로 선택한다.
3. payload에서 point count와 `x/y/z/intensity` 배열을 파싱해 `lidar_slots[slot]`에 쓴다.
4. 쓰기가 끝나면 `atomic_store(&shm->lidar_ready_idx, slot)`로 최신 슬롯을 공개한다.
5. `sem_post(&shm->lidar_sem)`로 reader에게 새 스캔 도착을 알린다.

### 5.3 코드 단위 설명

`reassembly_shm.c:119-122`

```c
int ready = atomic_load(&shm->lidar_ready_idx);
int slot  = (ready < 0) ? 0 : (ready + 1) % LIDAR_SLOTS;
LidarSlot *s = &shm->lidar_slots[slot];
```

현재 ready index를 기준으로 다음 write slot을 고른다.

`reassembly_shm.c:148-160`

```c
s->count        = count;
s->frame_id     = rs->frame_id;
s->timestamp_us = rs->timestamp_us;
...
atomic_store(&shm->lidar_ready_idx, slot);
```

포인트 배열 복사가 끝난 뒤 ready index를 갱신한다. 이미지와 동일하게 `lidar_ready_idx`가 최신 LiDAR 슬롯의 publication pointer다.

### 5.4 효과

- LiDAR 포인트 배열처럼 큰 데이터에 rwlock/mutex를 걸지 않는다.
- reader는 현재 공개된 슬롯 번호만 원자적으로 읽으면 된다.
- writer는 reader 상태를 기다리지 않고 다음 프레임을 계속 공개할 수 있다.

## 6. Jetson 연결 상태 락프리 공유

### 6.1 소스 위치

- 필드 정의: `shm_def.h:74-82`
- 연결 상태 on: `jetson_rx.c:140-142`
- watchdog 읽기/해제: `bridge_main.c:226-232`
- PC 상태 패킷 읽기: `pc_link.c:41-44`

### 6.2 동작 흐름

`jetson_rx_thread()`는 어떤 타입이든 Jetson 패킷을 수신하면 해당 robot의 공유메모리에 연결 상태를 1로 저장한다.

```c
atomic_store(&shm->meta.jetson_connected, 1);
atomic_fetch_add(&shm->meta.pkt_count, 1);
```

`watchdog_loop()`는 1초마다 `pkt_count`를 읽고, 3초 동안 값이 변하지 않으면 연결 상태를 0으로 내린다.

```c
uint8_t connected = atomic_load(&shm->meta.jetson_connected);
int cur_pkt = atomic_load(&shm->meta.pkt_count);
...
atomic_store(&shm->meta.jetson_connected, 0);
```

`pc_link.c`는 PC로 상태 패킷을 보낼 때 연결 상태를 lock 없이 읽는다.

```c
s.connected = (uint8_t)atomic_load(&shm->meta.jetson_connected);
```

### 6.3 효과

- `jetson_rx` 고우선순위 수신 루프가 연결 상태 갱신 때문에 mutex에 막히지 않는다.
- watchdog과 PC status sender가 같은 값을 읽어도 데이터 레이스가 없다.
- 상태값이 작고 최신값만 중요하므로 atomic이 mutex보다 적합하다.

## 7. 패킷 카운터 락프리 공유

### 7.1 소스 위치

- 필드 정의: `shm_def.h:76-77`
- 증가: `jetson_rx.c:140-142`
- 읽기: `bridge_main.c:226-236`

### 7.2 동작 흐름

`pkt_count`는 watchdog이 연결 유무를 판단하기 위한 단순 누적 카운터다.

- writer: `jetson_rx_thread()` 단일 수신 스레드
- reader: `watchdog_loop()` 메인 스레드

수신 스레드는 매 패킷마다 `atomic_fetch_add()`로 카운터를 증가시킨다.

```c
atomic_fetch_add(&shm->meta.pkt_count, 1);
```

watchdog은 이전 카운터와 현재 카운터를 비교한다.

```c
int cur_pkt = atomic_load(&shm->meta.pkt_count);
if (cur_pkt == prev_pkt_count[i]) {
    if (++no_pkt_sec[i] >= 3)
        atomic_store(&shm->meta.jetson_connected, 0);
}
```

### 7.3 효과

- 수신 루프에서 카운터 증가가 매우 짧게 끝난다.
- watchdog은 락 없이 읽기만 하므로 수신 루프와 서로 대기하지 않는다.
- 정확한 총량 집계보다 "최근 1초 동안 증가했는가"가 중요하므로 atomic counter가 적합하다.

## 8. 세마포어는 락프리 데이터 보호가 아니라 알림 장치

### 8.1 소스 위치

- 세마포어 정의: `shm_def.h:89-95`
- 초기화: `bridge_main.c:125-134`
- 알림: `reassembly_shm.c:113-114`, `reassembly_shm.c:160-161`

`img_sem`, `lidar_sem`은 새 데이터가 들어왔다는 이벤트를 reader에게 알려주는 용도다. 데이터 일관성은 `img_ready_idx`, `lidar_ready_idx`의 atomic publication으로 보장하고, 세마포어는 reader가 바쁘게 polling하지 않게 깨우는 역할만 한다.

즉 구조는 다음과 같이 분리되어 있다.

| 책임 | 구현 |
|---|---|
| 최신 슬롯 번호의 일관성 | `atomic_store/load` |
| reader 깨우기 | `sem_post/sem_wait` |
| 슬롯 내부 대용량 데이터 보관 | `img_slots[]`, `lidar_slots[]` |

## 9. 락프리가 아닌 구간과 이유

### 9.1 Fragment Queue

소스 위치:

- 정의: `frag_queue.h:23-31`
- push: `frag_queue.h:44-61`
- pop: `frag_queue.h:63-81`
- stop: `frag_queue.h:83-89`
- 사용: `jetson_rx.c:149-151`, `reassembly_shm.c:187-190`

`FragQueue`는 명시적으로 `pthread_mutex_t`와 `pthread_cond_t`를 사용한다.

```c
pthread_mutex_lock(&q->mu);
...
pthread_cond_signal(&q->cv);
pthread_mutex_unlock(&q->mu);
```

이 큐는 락프리 큐가 아니다. producer인 `jetson_rx`와 consumer인 `reassembly_shm`을 분리하기 위한 blocking queue다. 빈 큐에서 consumer를 재우고, push 시 깨우는 동작이 필요하기 때문에 condvar 기반 구현을 선택한 것으로 보인다.

### 9.2 Odom 공유

소스 위치:

- 필드/락 정의: `shm_def.h:97-106`
- writer: `jetson_rx.c:72-88`
- reader: `pc_link.c:46-52`

Odom은 `pthread_rwlock_t odom_lock`으로 보호한다. Odom은 여러 float 필드가 한 세트로 의미를 가지므로, 필드별 atomic보다 rwlock으로 한 번에 일관된 snapshot을 보장하는 방식이 맞다.

### 9.3 드롭 카운터와 일부 meta 필드

소스 위치:

- 필드 정의: `shm_def.h:78-82`, `shm_def.h:108-110`
- writer: `reassembly_shm.c:61-65`
- reader: `bridge_main.c:220-224`, `pc_link.c:54-58`

`img_drop_count`, `lidar_drop_count`는 `meta_lock`으로 보호된다. `jetson_connected`, `pkt_count`는 같은 `BridgeMeta` 안에 있지만 atomic 필드이므로 별도 lock 없이 접근한다. 따라서 `BridgeMeta` 전체가 락프리인 것은 아니고, 필드별로 동기화 전략이 섞여 있다.

### 9.4 Jetson 주소 테이블

소스 위치:

- 구조체 정의: `bridge_ctx.h:12-17`
- 주소 학습: `jetson_rx.c:57-70`
- 커맨드 송신 조회: `cmd_dispatch.c:24-28`
- heartbeat 주소 snapshot: `protocol_timer.c:104-112`

주소 테이블은 mutex 기반이다.

```c
pthread_mutex_lock(&tbl->mu);
...
pthread_mutex_unlock(&tbl->mu);
```

단, `cmd_dispatch.c`와 `protocol_timer.c`는 `sendto()`를 lock 밖에서 수행한다. 즉 락프리는 아니지만 lock hold time을 짧게 유지하는 방식으로 지연을 줄이고 있다.

## 10. 전체 데이터 흐름에서 락프리 적용 지점

### 10.1 이미지/LiDAR 수신 경로

```text
Jetson UDP
  -> jetson_rx.c
     - 연결 상태 atomic_store
     - pkt_count atomic_fetch_add
     - image/lidar fragment는 frag_queue_push (mutex)
  -> frag_queue.h
     - mutex + condvar queue
  -> reassembly_shm.c
     - fragment 재조립
     - commit_image / commit_lidar
     - 슬롯 데이터 복사
     - ready_idx atomic_store
     - sem_post
  -> Qt App
     - sem_wait
     - ready_idx atomic_load
     - 최신 슬롯 read
```

### 10.2 연결 상태 경로

```text
jetson_rx.c
  -> 패킷 수신 시 jetson_connected = 1, pkt_count++

bridge_main.c watchdog_loop
  -> pkt_count atomic_load
  -> 3초 이상 변화 없으면 jetson_connected = 0

pc_link.c
  -> jetson_connected atomic_load
  -> PC 상태 패킷에 포함
```

## 11. 설계상 주의점

### 11.1 현재 Triple Buffer는 최신값 전달에 최적화되어 있다

이 구조는 모든 프레임을 빠짐없이 소비하는 큐가 아니라, **항상 최신 프레임을 빠르게 공개하는 구조**다. reader가 늦으면 중간 프레임은 덮어써질 수 있다. 영상/센서 표시처럼 최신 상태가 중요한 UI에는 적합하지만, 모든 프레임 저장/검증이 필요한 로깅 목적에는 별도 큐나 파일 writer가 필요하다.

### 11.2 reader가 매우 오래 슬롯을 붙잡으면 덮어쓰기 가능성이 있다

writer는 `ready_idx`의 다음 슬롯을 순환해서 쓴다. Qt reader가 특정 슬롯을 매우 오래 읽고 있는데 writer가 여러 프레임을 빠르게 커밋하면, 3개 슬롯을 한 바퀴 돌아 reader가 읽는 슬롯을 다시 쓸 가능성이 있다. 일반적인 렌더링 주기에서는 문제가 작지만, "reader가 현재 읽는 슬롯을 별도 atomic으로 publish"하는 완전한 reader-aware triple buffer는 아니다.

### 11.3 기본 atomic memory order를 사용한다

현재 코드는 `atomic_load`, `atomic_store`, `atomic_fetch_add`에 memory order를 지정하지 않는다. C11 기본값은 sequential consistency다. 코드가 단순하고 안전 쪽으로는 유리하지만, 성능을 더 세밀하게 조정하려면 writer의 ready index store는 release, reader의 ready index load는 acquire로 명시하는 방식도 검토할 수 있다.

### 11.4 공유메모리의 C/C++ atomic ABI는 빌드 환경을 맞춰야 한다

`shm_def.h`는 C에서는 `_Atomic`, C++에서는 `std::atomic`을 사용한다. BridgeDaemon과 Qt App이 같은 플랫폼, 같은 크기/정렬 조건에서 빌드되어야 공유메모리 레이아웃이 일치한다. `int`, `uint8_t` 수준이라 위험은 낮지만, Qt 쪽에서도 같은 헤더를 사용하고 `sizeof(SharedData)` 및 atomic 필드 offset을 유지해야 한다.

## 12. 기능별 소스 코드 매핑

| 기능 | 파일 | 함수/구조체 | 라인 |
|---|---|---|---|
| 원자 타입 C/C++ 호환 정의 | `shm_def.h` | `ATOMIC_UINT8`, `ATOMIC_INT` | 23-31 |
| 이미지 슬롯/ready index 정의 | `shm_def.h` | `img_slots`, `img_ready_idx`, `img_sem` | 87-90 |
| LiDAR 슬롯/ready index 정의 | `shm_def.h` | `lidar_slots`, `lidar_ready_idx`, `lidar_sem` | 92-95 |
| 연결 상태/패킷 카운터 정의 | `shm_def.h` | `BridgeMeta` | 74-82 |
| ready index 초기화 | `bridge_main.c` | `shm_init_one()` | 63-146 |
| 이미지 atomic index swap | `reassembly_shm.c` | `commit_image()` | 88-115 |
| LiDAR atomic index swap | `reassembly_shm.c` | `commit_lidar()` | 117-162 |
| 재조립 완료 후 commit 호출 | `reassembly_shm.c` | `reassembly_shm_thread()` | 254-264 |
| 연결 상태 atomic 갱신 | `jetson_rx.c` | `jetson_rx_thread()` | 140-142 |
| watchdog atomic 읽기/해제 | `bridge_main.c` | `watchdog_loop()` | 210-243 |
| PC 상태 패킷 atomic 읽기 | `pc_link.c` | `send_status_all()` | 33-63 |
| fragment queue mutex 구간 | `frag_queue.h` | `frag_queue_push/pop/stop()` | 44-89 |
| Odom rwlock writer | `jetson_rx.c` | `handle_odom()` | 72-88 |
| Odom rwlock reader | `pc_link.c` | `send_status_all()` | 46-52 |
| 주소 테이블 mutex 정의 | `bridge_ctx.h` | `JetsonAddrTable` | 12-17 |
| 주소 테이블 mutex writer | `jetson_rx.c` | `learn_addr()` | 57-70 |
| 주소 테이블 mutex reader | `cmd_dispatch.c` | `send_cmd_to_jetson()` | 24-28 |
| heartbeat 주소 snapshot | `protocol_timer.c` | `protocol_timer_thread()` | 104-112 |

## 13. 최종 평가

현재 코드의 락프리 적용은 적절히 제한되어 있다. 이미지와 LiDAR처럼 데이터 크기가 크고 최신값만 중요한 경로에서는 atomic index swap으로 mutex 경합을 제거했고, 연결 상태와 패킷 카운터처럼 작은 상태값은 atomic 변수로 빠르게 공유한다.

반면, 프래그먼트 큐처럼 blocking wait가 필요한 구간, Odom처럼 여러 필드를 하나의 일관된 묶음으로 읽어야 하는 구간, 주소 테이블처럼 구조체 전체 snapshot이 필요한 구간은 mutex/rwlock을 사용한다. 따라서 이 프로젝트의 동시성 설계는 "전체 락프리"가 아니라 **대용량 최신 데이터 공개 경로만 락프리화하고, 나머지는 목적에 맞는 전통적 락을 병행하는 하이브리드 구조**로 보는 것이 정확하다.
