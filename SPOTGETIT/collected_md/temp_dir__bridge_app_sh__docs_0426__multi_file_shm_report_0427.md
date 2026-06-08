# 다중 파일 공유 메모리 구조 보고서 (Multi-File SHM)

## 개요
이 보고서는 여러 대의 Jetson 기기들을 지원하기 위해, 각 로봇마다 완전히 독립된 이름(`/robot_bridge_0`, `/robot_bridge_1`, ...)의 공유 메모리 객체를 생성하는 구조를 분석합니다. 

하나의 큰 메모리 블록을 통째로 쪼개어 쓰는 방식이 아닌, **운영체제 레벨에서 파일을 여러 개 만들어서 격리시키는 방식**입니다.

---

## 1. 전반적인 아키텍처 및 동작 구조

### `bridge_app` (데이터 수신 데몬)
1. UDP 포트는 기존처럼 1개(예: `9000번`)만 열어서 모든 Jetson의 패킷을 받아냅니다. 네트워크를 여러 번 열 필요가 없습니다.
2. UDP 데이터가 도착하면, 패킷 헤더의 **`robot_id`** (기존 `_pad` 부분 재활용) 값을 읽습니다.
3. 데몬 실행 시 `MAX_ROBOTS` 수량만큼 `/robot_bridge_0`, `/robot_bridge_1` ... 으로 여러 개의 공유 메모리 파일을 열고, 각각을 배열 포인터(예: `SharedData *shm_arr[4]`)로 관리합니다.
4. 패킷 조립 스레드는 `robot_id` 값에 맞춰서 `shm_arr[robot_id]` 에 해당하는 파일의 주소에만 락을 걸고 데이터를 씁니다.

### `qt_app` (데이터 읽기 및 화면 출력)
1. Qt에서는 로봇 대수만큼 `ShmReader` 인스턴스를 여러 개 생성하는 방식이 적합합니다.
   - 예: `ShmReader *reader_0 = new ShmReader(0);`
2. 생성자에서 넘어온 `id` 값을 이용해 `QString("/robot_bridge_%1").arg(id)` 로 각기 다른 파일을 `mmap`하여 엽니다.
3. 로봇마다 스레드가 분리되어 동작하므로 서로 간섭 없이 데이터를 UI로 안전하게 전달합니다.

---

## 2. 파일별 주요 변경점 설계

### [NEW] `proto.h` 
패킷 헤더에 로봇 ID가 필수로 추가되어야 데몬이 어느 파일로 보낼지 결정할 수 있습니다.
```c
typedef struct __attribute__((packed)) {
    uint8_t  type;            
    uint8_t  robot_id; /* 변경됨: 0, 1, 2... 식별자 */
    uint16_t frag_idx;
...
```

### [MODIFY] `bridge_main.c` 
루프를 돌며 여러 개의 파일을 생성하도록 변경합니다.
```c
#define MAX_ROBOTS 4
SharedData *g_shm_arr[MAX_ROBOTS];

for(int i=0; i<MAX_ROBOTS; i++) {
    char shm_name[32];
    sprintf(shm_name, "/robot_bridge_%d", i);
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    ftruncate(fd, sizeof(SharedData));
    g_shm_arr[i] = mmap(0, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // 각 구조체의 rwlock, sem, atomic 초기화 진행
}
```

### [MODIFY] `qt_app/src/shm_reader.cpp`
파일 이름을 동적으로 받아서 오픈하도록 클래스를 변경합니다.
```cpp
ShmReader::ShmReader(int robot_id, QObject *parent) : QThread(parent) {
    QString name = QString("/robot_bridge_%1").arg(robot_id);
    m_fd = shm_open(name.toStdString().c_str(), O_RDWR, 0666);
    // ...
}
```

---

## 3. 이 방식의 장단점 비교

### 장점 (Pros)
1. **완벽한 격리성**: 운영체제 단위에서 완전히 독립된 파일이므로, 만약 특정 로봇의 상태가 꼬이거나 데몬 쪽 개별 파일 락(Lock)에 데드락이 발생해도 다른 로봇들에 전파되지 않습니다.
2. **유연한 Qt 앱 아키텍처**: Qt에서 1번 로봇만 보고 싶다면 `ShmReader(1)` 하나만 생성하면 되고, 메모리를 필요한 만큼만 사용합니다. 로봇 1대당 뷰어 위젯을 개별적으로 캡슐화(Component화)하기 아주 좋은 패턴입니다.
3. 기존 `shm_def.h`에 작성된 `SharedData`의 사이즈나 구조를 1비트도 건드릴 필요가 없습니다. (그대로 여러 번 호출만 하면 됨)

### 단점 (Cons)
1. 관리해야 할 파일 디스크립터(File Descriptor) 수가 로봇 대수만큼 증가합니다. (리눅스 환경에서는 큰 단점이 아님)
2. 초기화 구문 등에서 `for` 루프가 많아지고, 종료(`unlink`) 시 각 파일을 일일이 해제해 주어야 합니다.

---

> [!TIP]
> **결론 및 코멘트**
> Qt 소프트웨어 쪽에서 로봇 위젯들을 캡슐화하여 "각 탭마다 1개의 로봇을 표시하는 대시보드" 형태로 만들기에는 **다중 파일 방식(해당 보고서 방식)**이 설계상 훨씬 깔끔하고 OOP(객체지향프로그래밍)에 어울립니다. 
> 
> 이 방식이 마음에 드신다면, 이 구조를 바탕으로 구현을 시작할 수 있습니다. 피드백 부탁드립니다.
