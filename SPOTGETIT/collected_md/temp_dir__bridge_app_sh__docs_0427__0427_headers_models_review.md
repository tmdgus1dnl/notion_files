# 공통 헤더 및 유틸리티 모듈 상세 분석 (`proto.h`, `shm_def.h` 등)

전체 데몬 아키텍처의 근간이 되는 데이터 구조와 매크로 기법이 집약되어 있는 공통 정의 파일들입니다.

---

## 1. `proto.h` (통신 프로토콜 정의)

Jetson 로봇, RPi5 Bridge, PC 간에 오가는 모든 네트워크 패킷의 뼈대(Wire Format)를 선언합니다.

### 📌 컴파일러 메모리 패딩 제어
```c
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  robot_id;
    uint16_t frag_idx;
    // ...
} PktHeader;
```
- **`__attribute__((packed))`**: GCC 컴파일러 지시자입니다. C언어는 구조체의 변수 접근 속도(Alignment)를 최적화하기 위해 눈에 보이지 않는 패딩 바이트(빈공간)를 삽입합니다. 하지만 네트워크로 보낼 땐 이 빈 공간 때문에 바이트가 틀어집니다.
- `packed` 속성을 주면 메모리를 빈틈없이 1바이트 단위로 바짝 붙여, 수신부에서 `(PktHeader *)buf` 같은 다이렉트 캐스팅만으로 파싱이 가능하게 만듭니다. 이른바 **Zero-copy 파싱**의 핵심입니다.

### 📌 패킷 분할 메커니즘 지원
- 이미지나 라이다의 한 프레임은 `PROTO_MTU`(1400바이트)보다 크기 때문에 `frag_idx`(현재 조각 인덱스), `frag_total`(전체 조각 수), `payload_offset`(재조립 시 데이터 시작 위치) 등의 헤더 정보를 정의해 MTU 이하 크기로 쪼개서 송신할 수 있는 규격을 확립했습니다.

---

## 2. `shm_def.h` (공유 메모리 레이아웃)

Qt 애플리케이션과 데이터를 나누기 위한 메모리 맵(Memory Map) 설계도입니다.

### 📌 C++ / C 호환 원자(Atomic) 매크로
```c
#ifdef __cplusplus
#include <atomic>
#define ATOMIC_UINT8 std::atomic<uint8_t>
#else
#include <stdatomic.h>
#define ATOMIC_UINT8 _Atomic uint8_t
#endif
```
- Qt 애플리케이션은 C++로 컴파일(`g++`)되고, BridgeApp은 C언어로 컴파일(`gcc`)됩니다.
- 둘은 같은 메모리 영역을 공유하므로 타입이 완벽히 일치해야 하는데, C와 C++은 Atomic 타입을 표기하는 문법이 다릅니다. 이 매크로는 컴파일러를 감지하여 알맞은 헤더와 타입을 자동으로 치환함으로써 크로스 컴파일 호환성을 완벽히 맞췄습니다.

### 📌 멀티 버퍼 배열 구조
```c
ImgSlot img_slots[IMG_SLOTS];
ATOMIC_INT img_ready_idx;
sem_t img_sem;
```
- **데이터 저장소**(`img_slots[3]`), **포인터**(`img_ready_idx`), **알람**(`img_sem`)이 하나의 세트로 묶여 Lock-free Triple Buffering을 구현하도록 설계되어 있습니다.

---

## 3. `bridge_ctx.h` (스레드 분리 설계)
```c
typedef struct {
    SharedData      *shm_arr[MAX_ROBOTS];
    FragQueue       *fq_arr[MAX_ROBOTS];
    JetsonAddrTable *addr_table;
    int              num_robots;
    volatile int     stop;
} JetsonRxCtx;
```
- 멀티스레드 프로그래밍에서 전역 변수를 남발하면 락을 잡기 어렵고 코드가 엉킵니다.
- 이 프로젝트는 각 스레드(`rx`, `tx` 등)가 필요로 하는 포인터들만 `~Ctx` 구조체로 묶어 `pthread_create`의 `arg` 매개변수로 우아하게 주입(Dependency Injection)합니다.
- `volatile int stop`: CPU 레지스터 캐싱을 방지해 메인 스레드가 `stop=1`을 선언하면 각 자식 스레드가 지연 없이 실시간으로 변화를 감지하도록 `volatile`을 달아두었습니다.

---

## 4. `cmd_dispatch.c` (패킷 포장/발송 헬퍼)
`jetson_tx`와 `pc_link` 양쪽에서 동일한 `send_cmd_to_jetson` 함수를 호출합니다.

### 📌 버퍼 직렬화 기법 (Zero-copy)
```c
uint8_t buf[sizeof(PktHeader) + sizeof(CmdPayload)];
PktHeader  *hdr = (PktHeader *)buf;
CmdPayload *cmd = (CmdPayload *)(buf + sizeof(PktHeader));
```
- 별도의 구조체를 `malloc`하거나 구조체를 복사한 뒤 이어붙이는 작업(Serialization overhead)을 생략했습니다.
- 스택 메모리에 커다란 바이트 배열(`buf`)을 하나 잡고, 메모리 주소(포인터)를 쪼개어 앞쪽은 헤더로, 뒷쪽은 페이로드 영역으로 직접 캐스팅해 값을 우겨넣고 즉시 `sendto`를 호출하는 매우 빠르고 효율적인 C언어 네트워크 로직입니다.
