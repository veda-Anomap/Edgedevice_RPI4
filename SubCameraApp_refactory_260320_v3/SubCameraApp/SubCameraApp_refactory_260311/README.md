# SubCameraApp Refactory 260311

Raspberry Pi 기반 SubCameraApp 메인 실행 코드입니다.

## Core Features

- GStreamer 기반 실시간 캡처/전송
- OpenCV + TensorFlow Lite Pose 추론
- Fall rule 기반 이벤트 감지
- Event buffer + chunk 전송
- STM32 UART 브리지 연동

## Directory Overview

- `src/ai`: Pose 추론, 트래커, Fall detector
- `src/stream`: 카메라 캡처 및 송출 파이프라인
- `src/imageprocessing`: 저조도 개선 및 화질 튜닝
- `src/edge_device`: UART/브릿지 통신
- `src/network`, `src/protocol`: 네트워크/메시지 처리
- `src/controller`: 런타임 오케스트레이션

## Dependencies

| Category | Required |
|---|---|
| Build toolchain | `cmake (3.10+)`, `g++ (C++17)` |
| Vision | `OpenCV 4.8+`, `opencv_contrib (ximgproc)` |
| AI runtime | TensorFlow Lite C++ (`libtensorflowlite.so`) |
| Streaming | GStreamer 1.0 + plugins (`libcamerasrc`, `v4l2convert`, `x264enc`) |
| Config/JSON | nlohmann/json |
| System | `pthread`, `dl`, `rt`, Linux POSIX socket/termios |

## Install (Raspberry Pi OS / Ubuntu)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libopencv-dev libopencv-contrib-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
  libcamera-dev libcamera-apps \
  nlohmann-json3-dev
```

## TensorFlow Lite Setup Note

현재 `CMakeLists.txt`는 아래 경로를 기본값으로 가정합니다.

- `TF_SRC=/home/suseok/tensorflow_src`
- `libtensorflowlite.so=$TF_SRC/bazel-bin/tensorflow/lite/libtensorflowlite.so`

환경이 다르면 `CMakeLists.txt`의 `TF_SRC` 값을 실제 경로로 수정한 뒤 빌드하세요.

## Build

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

## Run

```bash
./subcam_main
```

권한 이슈가 있으면:

```bash
sudo ./subcam_main
```

## Runtime Specs (Code Defaults)

- Capture: `1920x1080`
- Process/AI input: `640x480`
- Target FPS: `30`
- AI feed interval: every `5` frames
- UART: `/dev/serial0`, `115200`

## Optimization Points

- `queue leaky=downstream` + `max-size-buffers`로 지연 누적 방지
- 캡처 프레임 즉시 `clone()`으로 GStreamer 버퍼 조기 반환
- AI/전처리 분리 스레드로 병목 완화
- `AppConfig.h` 파라미터 기반 성능-정확도 트레이드오프 튜닝

## Operational Cautions

- `/dev/video0`, `/dev/serial0` 점유 상태를 먼저 확인하세요.
- 카메라 접근 실패 시 `pipewire`/타 프로세스 충돌 여부를 점검하세요.
- UART 권한이 없으면 `dialout` 그룹 추가가 필요할 수 있습니다.

```bash
sudo usermod -aG dialout $USER
```

## Quick Validation

```bash
gst-inspect-1.0 libcamerasrc
gst-inspect-1.0 x264enc
```

## References

- OpenCV: https://docs.opencv.org/
- GStreamer: https://gstreamer.freedesktop.org/documentation/
- TensorFlow Lite: https://www.tensorflow.org/lite
