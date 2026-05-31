# epoll 이벤트 플래그가 Edge Trigger(ET)인 이유

핵심 차이는 **epoll_wait를 몇 번 깨어나냐**야.

### 루프를 "어디서" 도느냐가 다름

```c
**LT:**

`데이터 100바이트 도착

epoll_wait → 깨어남
  read(50) → 처리
epoll_wait → 또 깨어남  ← 50바이트 남아서
  read(50) → 처리
epoll_wait → 안 깨어남`
```

epoll_wait 레벨에서 2번 wakeup 발생

```c
**ET:**

`데이터 100바이트 도착

epoll_wait → 깨어남
  read(50) → 처리
  read(50) → 처리
  read() → EAGAIN, 루프 종료
epoll_wait → 안 깨어남`
```

epoll_wait 레벨에서 1번 wakeup 발생

---

**따라서**

연결이 1만 개인 서버에서 500개 fd에 데이터가 남아있다고 하면:

- **LT**: 매 epoll_wait마다 그 500개가 계속 반환됨. 처리 다 될 때까지 반복
- **ET**: 새 데이터 왔을 때만 반환. 한 번에 다 처리하고 끝

# 그러나

**항상 EAGAIN까지 루프 도는 LT라면 wakeup 횟수는 ET랑 같아.**

ET의 실질적인 이점은 다른 데 있어:

---

### 1. 멀티스레드 환경 (진짜 차이)

```c
스레드 A, B, C가 같은 epoll_fd를 epoll_wait 중

LT: fd에 데이터 오면 → A, B, C 전부 깨어남 (썬더링 허)
ET: fd에 데이터 오면 → 하나만 깨어남
```

nginx가 ET 쓰는 핵심 이유가 이거야.

# 그러면 ET → 어떻게 하나만 깨우는데?

epoll 내부에서 ET 플래그가 설정된 fd에 이벤트가 오면, **이미 누군가 처리 중(wakeup됨)이면 ready list에 다시 안 넣어**버림.

LT vs ET 내부 동작:

```c
**LT:**

`데이터 도착 → fd를 ready list에 추가
epoll_wait 호출마다 → ready list 검사 → 데이터 아직 있으면 또 반환
→ 대기 중인 스레드 여러 개면 여러 개 깨어남`

**ET:**

`데이터 도착 → fd를 ready list에 추가
첫 번째 epoll_wait → 가져가면서 해당 fd 이벤트 플래그 클리어
→ 다른 스레드가 epoll_wait 해도 그 fd는 ready list에 없음
→ 새 데이터 와서 다시 엣지 발생해야만 ready list에 재등록
```

즉 **"이벤트 소비 = 플래그 클리어"** 구조라서 하나만 가져갈 수 있어.

LT는 "버퍼에 데이터 있냐?" 를 매번 확인하는 레벨 기반이라 여러 스레드가 동시에 "있어!"를 받을 수 있는 반면, ET는 **상태 변화 신호 자체를 소비**하는 구조라 중복 wakeup이 안 생기는 거야.

쉽게 말해 ET는 이벤트 있는 fd를 받았을때 그 순간 해당 fd의 플래그를 클리어시켜서 ready-list에서 제거

LT는 그런거 없음 → 수동으로 플래그 제거하면 ET랑 똑같긴 함..