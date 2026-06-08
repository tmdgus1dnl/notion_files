# Android 포팅 메모

이 디렉터리는 기존 Linux용 BridgeDaemon 빌드를 유지하면서, Android에서 동작하기 위한 1차 포팅 계층을 추가한 상태다.

## 네이티브 IPC

- `bridge_ipc.c`는 공유 메모리 생성 방식을 플랫폼별로 감싼다.
- Linux에서는 `shm_open`, `ftruncate`, `mmap`, `shm_unlink`를 사용한다.
- Android에서는 `ASharedMemory_create`, `ASharedMemory_setProt`, `mmap`을 사용한다.
- `bridge_ipc_region_fd()`는 공유 메모리 fd를 외부로 노출한다. Android에서는 이 fd를 Binder로 클라이언트 프로세스에 전달할 수 있다.

## 공유 메모리 알림

`SharedData`는 이제 원시 `sem_t` 대신 `BridgeSignal`을 저장한다.

- Linux: `BridgeSignal`은 `sem_t`로 정의된다.
- Android: `BridgeSignal`은 `pthread_mutex_t + pthread_cond_t + generation` 조합으로 정의된다.

프레임을 발행하는 코드는 `sem_post()`를 직접 호출하지 않고 `bridge_signal_post()`를 호출한다.

Android 클라이언트가 전달받은 fd를 `mmap`해서 사용할 때도 동일한 `shm_def.h` 구조와 `bridge_signal_timedwait()` 의미를 따라야 한다.

## Android 서비스 연결

Android 골격 코드는 `android/src/main` 아래에 있다.

- `BridgeMiddlewareService`는 JNI를 통해 네이티브 daemon loop를 시작한다.
- `IBridgeMiddleware.aidl`은 특정 robot의 공유 메모리 영역을 `ParcelFileDescriptor`로 반환한다.
- 서비스는 같은 APK 안에서도 UI 프로세스와 미들웨어를 분리하기 위해 `:bridge` 프로세스에서 실행된다.

## 빌드 진입점

Linux 실행 파일:

```bash
make
cmake -S . -B build_linux && cmake --build build_linux
```

Android 네이티브 라이브러리:

- Android Gradle module에서 이 디렉터리를 external native CMake project로 연결한다.
- 대상 라이브러리 이름은 `bridge_middleware`다.
- 최소 API level은 26 이상이어야 한다. `ASharedMemory`가 Android 8.0 이상에서 제공되기 때문이다.
