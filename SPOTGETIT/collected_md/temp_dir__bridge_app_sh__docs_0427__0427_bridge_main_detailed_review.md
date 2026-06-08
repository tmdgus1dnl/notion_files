# `bridge_main.c` 상세 코드 분석 (라인/API 레벨)

`bridge_main.c`는 BridgeDaemon의 진입점(Entry Point)이자 심장부입니다. 멀티 프로세스/스레드 환경에서 메모리와 스레드를 조율하는 수많은 시스템 API가 사용되었습니다. 

여기서는 코드를 기능 블록 단위로 나누어 **어느 헤더에서 왔는지**, **어떤 역할을 하는지**, **파라미터의 의미는 무엇인지** 상세하게 리뷰합니다.

---

## 1. 헤더 포함 및 전역 변수 (Lines 16~61)

```c
#include <fcntl.h>       // 파일 제어 옵션 (O_CREAT, O_RDWR 등)
#include <pthread.h>     // POSIX 스레드 (pthread_create, pthread_rwlock_t 등)
#include <sched.h>       // 스케줄링 옵션 (SCHED_FIFO, cpu_set_t 등)
#include <signal.h>      // 시그널 처리 (signal, SIGINT, SIGTERM)
#include <stdatomic.h>   // 원자적 연산 (atomic_load, atomic_store 등)
#include <stdio.h>       // 표준 입출력 (printf, perror, fprintf)
#include <stdlib.h>      // 표준 유틸리티 (atoi, malloc 등)
#include <string.h>      // 메모리 제어 (memset)
#include <sys/mman.h>    // 메모리 맵핑 및 공유 메모리 (shm_open, mmap, munmap)
#include <unistd.h>      // 유닉스 표준 (close, ftruncate, sleep, getpid)
#include <sys/stat.h>    // 파일 상태 및 권한 상수 (0666 등)

#include "shm_def.h"     // SharedData 구조체, 세마포어
#include "frag_queue.h"  // FragQueue 패킷 큐
#include "proto.h"       // 통신 규격 (포트 번호)
#include "bridge_ctx.h"  // 스레드별 컨텍스트 구조체
```

**설명:**
운영체제 자원(공유 메모리, 스레드 우선순위, 파일 디스크립터)을 직접 제어해야 하므로 리눅스 시스템 프로그래밍에 필수적인 헤더들이 "대거" 포함되어 있습니다. 
특히 `<stdatomic.h>`는 Lock-free 동기화를 위해, `<sys/mman.h>`는 Qt 앱과의 데이터 공유를 위해 중요하게 쓰입니다. 전역 컨텍스트 `g_ctx`를 하나 선언하여 모든 스레드의 핸들과 자원을 한곳에 모아 관리합니다.

---

## 2. 공유 메모리 초기화 `shm_init_one` (Lines 63~108)

Qt 앱과 실시간으로 데이터를 주고받을 공유 메모리(SHM) 영역을 생성하는 핵심 함수입니다.

```c
snprintf(name, sizeof(name), SHM_NAME_FMT, robot_id); // <stdio.h>
shm_unlink(name); // <sys/mman.h>
```
- **`snprintf`**: 버퍼 오버플로우를 막으면서 안전하게 문자열(예: `"/robot_bridge_0"`)을 만듭니다.
- **`shm_unlink`**: 시스템에 이전 실행 때 남은 쓰레기 공유 메모리 파일이 있다면 삭제하여 충돌을 방지합니다.

```c
int fd = shm_open(name, O_CREAT | O_RDWR, 0666); // <sys/mman.h>, <fcntl.h>
```
- **`shm_open`**: POSIX 공유 메모리 객체를 생성합니다.
  - `name`: "/robot_bridge_0"과 같은 슬래시로 시작하는 이름
  - `O_CREAT | O_RDWR`: 없으면 생성하고, 읽기/쓰기 권한으로 엽니다. (`<fcntl.h>`)
  - `0666`: 모든 사용자가 읽고 쓸 수 있는 파일 권한 부여 (`<sys/stat.h>`)

```c
ftruncate(fd, sizeof(SharedData)); // <unistd.h>
SharedData *shm = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); // <sys/mman.h>
close(fd); // <unistd.h>
```
- **`ftruncate`**: 방금 생성한 공유 메모리의 크기를 `SharedData` 구조체 크기(~600KB 이상)만큼 확장합니다. 초기 생성 시 크기는 0바이트입니다.
- **`mmap`**: 파일 디스크립터(`fd`)를 현재 프로세스의 메모리 주소 공간에 매핑합니다. 
  - `PROT_READ | PROT_WRITE`: 읽기/쓰기 허용.
  - `MAP_SHARED`: 이 메모리에 쓰는 내용은 다른 프로세스(Qt)에서도 실시간으로 보입니다!
- 매핑이 끝났으므로 파일 디스크립터 `fd`는 `close()`로 닫아 자원 누수를 막습니다.

```c
atomic_store(&shm->img_ready_idx, -1); // <stdatomic.h>
```
- **`atomic_store`**: 스레드 안전하게 `-1`을 대입합니다. 초기 상태엔 완료된 프레임이 없음을 뜻합니다.

```c
sem_init(&shm->img_sem, 1, 0); // <semaphore.h> (shm_def.h에 포함)
```
- **`sem_init`**: POSIX 세마포어를 초기화합니다.
  - 두 번째 인자 `1`: **프로세스 간 공유(Process-shared)**를 의미합니다. (이게 0이면 같은 프로그램 내 스레드끼리만 공유 가능)
  - 세 번째 인자 `0`: 초기 세마포어 카운트 값입니다.

```c
pthread_rwlockattr_t rw_attr; // <pthread.h>
pthread_rwlockattr_setpshared(&rw_attr, PTHREAD_PROCESS_SHARED);
pthread_rwlock_init(&shm->odom_lock, &rw_attr);
```
- **`pthread_rwlock_init`**: Odom(위치 정보) 동기화를 위한 읽기/쓰기 락을 생성합니다. 세마포어와 마찬가지로 `PTHREAD_PROCESS_SHARED` 속성을 주어 Qt 앱과 락을 공유할 수 있게 만듭니다.

---

## 3. 스레드 생성 도우미 `create_thread` (Lines 132~157)

BridgeDaemon은 수신, 송신 등의 역할에 따라 스레드 우선순위를 다르게 주어 리얼타임(Real-time) 특성을 확보합니다.

```c
pthread_attr_init(&attr); // <pthread.h>

if (core >= 0) {
    cpu_set_t cpuset;     // <sched.h>
    CPU_ZERO(&cpuset);    
    CPU_SET(core, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
}
```
- **`CPU_ZERO` / `CPU_SET` / `pthread_attr_setaffinity_np`**: 스레드를 특정 CPU 코어(예: 코어 2번)에 완전히 고정(Pinning)시킵니다. 수신 스레드(`jetson_rx`)가 다른 프로세스에 밀려 패킷을 놓치는 일이 없도록 CPU 캐시 히트율을 극대화하는 리얼타임 최적화 기법입니다.

```c
if (priority > 0) {
    struct sched_param param = { .sched_priority = priority }; // <sched.h>
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
}
```
- **`SCHED_FIFO`**: 리눅스 리얼타임 스케줄링 정책입니다. 우선순위(0~99)가 높은 스레드는 스스로 양보하거나 차단되지 않는 한 CPU를 절대 빼앗기지 않습니다. (송/수신 스레드에 60, 80 등 높은 값을 줍니다)
- **`PTHREAD_EXPLICIT_SCHED`**: 스레드 생성 시 메인 스레드의 속성을 상속받지 않고 우리가 정의한 `SCHED_FIFO`와 우선순위를 강제로 적용하도록 설정합니다.

---

## 4. 와치독 `watchdog_loop` (Lines 172~205)

메인 스레드는 초기화 완료 후 1초에 한 번씩 돌면서 로봇들의 연결 상태를 모니터링합니다.

```c
int cur_pkt = atomic_load(&shm->meta.pkt_count); // <stdatomic.h>
if (cur_pkt == prev_pkt_count[i]) {
    if (++no_pkt_sec[i] >= 3)
        atomic_store(&shm->meta.jetson_connected, 0);
}
```
- **`atomic_load`**: `jetson_rx` 스레드가 패킷을 받을 때마다 Lock 없이 1씩 올리고 있는 `pkt_count`를 안전하게 읽어옵니다.
- 1초 전 카운트와 현재 카운트가 같다면 패킷이 끊긴 것입니다. 이 상태가 3초(`no_pkt_sec[i] >= 3`) 유지되면 Qt 앱과 PC에 로봇 연결이 끊겼음을 알리기 위해 `jetson_connected` 상태를 `0`으로 바꿉니다.

---

## 5. 진입점 `main` (Lines 207~336)

```c
umask(0); // <sys/stat.h>
```
- **`umask(0)`**: 리눅스에서 파일 생성 시 기본 권한을 깎아내리는 umask 값을 0으로 리셋합니다. 이를 통해 `shm_open`에서 요청한 `0666`(모두 접근 허용) 권한이 0644 등으로 깎이지 않고 그대로 적용되어 Qt 앱 측에서 권한 에러 없이 접근할 수 있게 보장합니다.

```c
int num_robots = (argc >= 2) ? atoi(argv[1]) : 1; // <stdlib.h>
```
- **`atoi`**: 문자열인 실행 인자(`argv[1]`)를 정수로 변환하여 로봇 대수를 파악합니다.

```c
signal(SIGINT, sig_handler);  // <signal.h>
signal(SIGTERM, sig_handler);
```
- **`signal`**: Ctrl+C (`SIGINT`) 또는 `kill` 명령(`SIGTERM`)이 들어올 때 프로그램이 강제 종료되어 공유 메모리가 시스템에 좀비처럼 남아있는 것을 막습니다. 종료 시그널이 오면 `sig_handler`를 호출해 전역 `stop` 변수를 `1`로 바꿔 스레드들이 스스로 안전하게 종료되도록 유도합니다.

```c
// 스레드 생성 파트 예시
g_ctx.rx_tid = create_thread(jetson_rx_thread, &g_ctx.rx_ctx, 60, 2);
g_ctx.tx_tid = create_thread(jetson_tx_thread, &g_ctx.tx_ctx, 80, -1);
```
- `jetson_rx` (수신)는 **우선순위 60**, **코어 2 고정**.
- `jetson_tx` (송신)는 명령의 즉각적인 반영(비상 정지 등)이 생명이므로 **우선순위 80**(매우 높음)으로 구동됩니다.
- 스레드 중 하나라도 생성에 실패하면 `cleanup_all`을 호출하여 모든 자원(메모리, 파일)을 정리하고 안전하게 종료하는 방어적 프로그래밍 패턴이 적용되어 있습니다.

```c
watchdog_loop(); // 무한 루프 블로킹
pthread_join(g_ctx.rx_tid, NULL); // <pthread.h>
cleanup_all(num_robots);
```
- 메인 스레드는 `watchdog_loop()`에서 무한히 돌다가, 시그널(`Ctrl+C`)을 받으면 루프를 탈출합니다.
- **`pthread_join`**: 탈출 후 모든 자식 스레드가 완전히 종료(리턴)될 때까지 대기합니다.
- 최종적으로 `cleanup_all()`을 통해 세마포어 파괴, `shm_unlink` 등 모든 시스템 자원을 반환하고 깨끗하게 종료(`return 0`)합니다.
