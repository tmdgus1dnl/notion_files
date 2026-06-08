# `frag_queue.h` 및 `reassembly_shm.c` 상세 분석 (메모리 큐 & 재조립 파이프라인)

네트워크 단(UDP)에서 들어온 쪼개진 패킷 조각(Fragments)들을 Qt 앱이 렌더링할 수 있는 하나의 온전한 프레임으로 복원하는 핵심 파이프라인입니다.

---

## 1. `frag_queue.h` (패킷 큐 동기화)
수신 스레드(Producer)와 재조립 스레드(Consumer) 간의 데이터를 안전하고 빠르게 넘기기 위해 **Blocking Queue**를 구현한 헤더입니다.

### 📌 포함된 핵심 헤더 및 API
- `#include <pthread.h>`: 동기화를 위한 핵심 뮤텍스, 조건변수 지원 헤더.
- **`pthread_mutex_init`, `pthread_mutex_lock`**: 큐의 Head/Tail 인덱스나 버퍼에 동시 접근하여 데이터가 꼬이는 것을 막는 상호 배제(Mutex) 락입니다.
- **`pthread_cond_init`, `pthread_cond_wait`, `pthread_cond_signal`**: 조건 변수(CondVar)입니다. 큐가 비어있으면 `pop` 스레드는 잠들고(`wait`), 새로운 데이터가 `push`되면 `signal`을 보내 잠든 스레드를 깨웁니다. 불필요한 폴링(While(1) 무한루프)으로 인한 CPU 낭비를 없앱니다.
- **`pthread_cond_broadcast`**: 프로그램 종료 시 `stop=1`을 세팅하고 대기 중인 **모든** `pop` 스레드들을 일제히 깨워 스스로 종료하게 만듭니다.
- **`__builtin_memcpy`**: 컴파일러 내장 복사 함수로, 일반 `memcpy`보다 오버헤드가 적어 수신된 버퍼를 큐 배열 안으로 초고속으로 복사합니다.

### 📌 주요 로직 (Drop 정책)
```c
if (q->count == FRAG_QUEUE_SIZE) {
    q->head = (q->head + 1) % FRAG_QUEUE_SIZE; // 가장 오래된 항목 버림
    q->count--;
}
```
- 영상 스트리밍 특성상 패킷이 밀려 큐가 가득 찼을 때, 큐에 빈 공간이 생길 때까지 기다리면(Blocking) 지연(Latency)이 기하급수적으로 늘어납니다. 따라서 락을 걸고 가장 오래된 데이터를 덮어쓰는(Overwriting) **Drop 정책**을 적용하여 실시간성을 유지합니다.

---

## 2. `reassembly_shm.c` (프레임 재조립 및 SHM 쓰기)
`FragQueue`에서 조각을 빼내어 메모리 슬롯에 차곡차곡 조립하고, 완성되면 공유 메모리(Triple Buffer)에 쓰는 스레드입니다.

### 📌 포함된 핵심 헤더 및 API
- `#include <stdatomic.h>`: Lock-free 기반 버퍼 스왑 처리를 위함.
- `#include <time.h>` (via `utils.h`의 `now_us()`): 타임아웃 감시용 고해상도 타이머.
- **`calloc` / `malloc` (`<stdlib.h>`)**: 
  - 각 스레드의 임시 조립 슬롯(`ReasmSlot`)의 크기는 약 200KB이며 총 6개(1.2MB)가 필요합니다. 이를 지역 변수(스택)로 선언하면 스택 오버플로우가 발생할 수 있으므로, 시작 시 `calloc`을 통해 힙(Heap) 메모리에 할당합니다.
- **`sem_post` (`<semaphore.h>`)**: SHM 버퍼에 이미지 쓰기를 마친 후, 잠들어 있는 Qt 앱의 렌더링 스레드에 "데이터가 준비되었다"는 신호를 쏘아줍니다.
- **`pthread_rwlock_wrlock`**: 타임아웃 발생 시 메타데이터의 드롭 카운트(`img_drop_count`)를 올리기 위해 배타적 쓰기 락을 획득합니다.

### 📌 보안 및 버그 방어 로직 (OOB 방어)
```c
if (offset >= REASM_BUF_MAX || (uint32_t)plen > REASM_BUF_MAX - offset) {
    // 버퍼 오버플로/언더플로 차단 ...
}
```
- 네트워크 패킷은 조작되거나 손상될 수 있습니다. 헤더에 명시된 `offset`과 `plen`(크기)이 내가 할당한 메모리 `REASM_BUF_MAX` 범위를 벗어난다면, 해커의 버퍼 오버플로우 공격이나 메모리 커럽션(Segmentation Fault)으로 프로그램이 죽을 수 있습니다. 이를 복사 전(`memcpy`)에 원천 차단하는 매우 강력한 방어 로직입니다.

### 📌 Lock-Free Triple Buffer 쓰기 기법 (`commit_image`)
```c
int ready = atomic_load(&shm->img_ready_idx);
int slot = (ready >= 0) ? (ready + 1) % IMG_SLOTS : 0;
// 데이터 복사...
atomic_store(&shm->img_ready_idx, slot);
```
- 락(`Mutex`)을 걸고 데이터를 복사하면 Qt 앱이 렌더링하느라 락을 잡고 있을 때 BridgeDaemon이 멈추는 병목이 생깁니다.
- 이를 해결하기 위해, 현재 Qt가 보고 있는 인덱스(`ready`)와 **겹치지 않는 다음 인덱스**(`slot`)를 스스로 계산하여 복사합니다. 복사가 끝난 후 `atomic_store`를 이용해 새 인덱스를 원자적으로 스왑(Swap)합니다. Qt 앱 역시 `atomic_load`로 최신 인덱스만 읽어가므로 서로 충돌이 0%가 됩니다.
