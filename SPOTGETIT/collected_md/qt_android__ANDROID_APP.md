# qt_android Qt Android 전환 메모

`qt_android`는 `disaster_control_qt`의 Qt Widgets UI를 Android용 Qt/CMake 앱으로 옮긴 구조다.
대시보드, 영상, LiDAR 지도, 로봇 상태, 이벤트 로그, 명령 버튼 로직은 기존 Qt 코드와 동일하게 유지한다.

## 현재 구조

- `src/`
  - 기존 Qt Widgets UI와 worker thread 코드
  - Android에서는 `RobotShmConnection`이 `shm_open` 대신 Binder로 받은 shared memory fd를 `mmap`
- `android/AndroidManifest.xml`
  - QtActivity를 launcher activity로 사용
  - `com.robot.bridge.BridgeMiddlewareService`를 같은 APK의 `:bridge` 프로세스에 등록
- `android/src/com/robot/control/QtBridgeRepository.java`
  - Qt C++에서 호출하는 Java helper
  - `BridgeMiddlewareService` start/bind, robot별 shared memory fd 보관
- `android/src/com/robot/bridge`
  - AIDL과 Android service
- `CMakeLists.txt`
  - Android 빌드 시 `../bride_app_android`의 `bridge_middleware`를 함께 빌드하고 링크
  - `QT_ANDROID_PACKAGE_SOURCE_DIR`로 `android/` 패키지 소스를 사용

## 실행 흐름

1. QtActivity가 시작되고 `MainWindow`가 생성된다.
2. `RobotShmConnection::startBridgeService(4)`가 Java helper를 통해 `BridgeMiddlewareService`를 시작하고 bind한다.
3. 서비스는 `:bridge` 프로세스에서 `bridge_middleware` daemon thread를 실행한다.
4. Qt worker thread는 robot별 fd를 Java helper에서 받아 `SharedData`를 `mmap`한다.
5. 상태, JPEG 영상, LiDAR, 이벤트는 기존 Qt polling/rendering 코드로 읽는다.
6. 명령 버튼은 기존 `ShmCmdQueue` push 로직으로 shared memory command queue에 기록한다.

## 빌드

Qt for Android kit에서 CMake 프로젝트로 `qt_android/CMakeLists.txt`를 연다.
CLI 예시는 설치된 Qt/Android toolchain 경로에 맞춰 다음 형태로 구성한다.

```bash
cmake -S qt_android -B build_qt_android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DQt6_DIR=$QT_ANDROID/lib/cmake/Qt6
cmake --build build_qt_android
```

이 작업 환경에는 Qt Android kit/androiddeployqt가 없어 APK 빌드는 실행하지 못했다.
