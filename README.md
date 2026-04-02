# VEDA Final

SubCameraApp 기반 엣지 비전 프로젝트의 최종 스냅샷입니다.
이 브랜치는 코드와 문서 중심으로 관리되며, 빌드 산출물/이미지/대형 모델 파일은 제외합니다.

## Repository Layout

- `SubCameraApp_refactory_260320_v3/`
  - C++ 메인 런타임(캡처, 전처리, AI 추론, 이벤트 전송)
  - 실제 빌드 루트: `SubCameraApp_refactory_260320_v3/SubCameraApp/SubCameraApp_refactory_260311/`
- `testing_app_python/`
  - Python GUI 기반 전처리/룰 검증 도구
- 루트 실험 스크립트
  - `260326_firedetect_hanwha_detectenhanced*.py`
  - `ai_only_test_done.py`
  - `pi_fire_fusion_pipeline_ultralytics.py`
  - `rpi_heartbeat.cpp`

## Required Libraries

### C++ Runtime

- OpenCV 4.8+ (`opencv_core`, `imgproc`, `videoio`, `highgui`, `ximgproc`)
- TensorFlow Lite C++ (`tensorflow/lite/interpreter.h`, `libtensorflowlite.so`)
- GStreamer 1.0 (`libcamerasrc`, `v4l2convert`, `x264enc`, `rtph264pay`, `appsink`)
- nlohmann/json (header-only)
- POSIX thread/runtime libs (`pthread`, `dl`, `rt`)

### Python Testing App

- Python 3.10+
- `numpy`
- `opencv-contrib-python` (ximgproc 필요)
- `PyQt5`
- `tflite-runtime` (Raspberry Pi 권장) 또는 `tensorflow`

## Recommended Specs

| Item | Minimum | Recommended |
|---|---|---|
| Device | Raspberry Pi 4 (4GB) | Raspberry Pi 4/5 (8GB) |
| OS | Raspberry Pi OS 64-bit | Raspberry Pi OS 64-bit (Bookworm) |
| CPU | 4-core ARM | 4-core+ ARM, active cooling |
| Camera | `/dev/video0` 접근 가능 장치 | libcamera 호환 CSI 카메라 |
| Serial | `/dev/serial0` (115200) | UART + STM32 브리지 연결 |
| Network | LAN/Wi-Fi | 유선 LAN |

기본 실행 파라미터는 코드 기준으로 `CAPTURE 1920x1080`, `PROCESS 640x480`, `FPS 30`, `AI interval 5`입니다.

## Install (Raspberry Pi OS / Ubuntu)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config git \
  libopencv-dev libopencv-contrib-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
  libcamera-dev libcamera-apps \
  nlohmann-json3-dev \
  python3 python3-venv python3-pip
```

> `x264enc`는 `gstreamer1.0-plugins-ugly`에 포함됩니다.

## Build and Run (C++)

```bash
cd SubCameraApp_refactory_260320_v3/SubCameraApp/SubCameraApp_refactory_260311
mkdir -p build
cd build
cmake ..
make -j4
./subcam_main
```

필요 시:

```bash
sudo ./subcam_main
```

## Run Testing App (Python)

```bash
cd testing_app_python
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install numpy opencv-contrib-python PyQt5
# Raspberry Pi
pip install tflite-runtime
# x86 개발 PC 대안
# pip install tensorflow
python main.py
```

## Verification Checklist

```bash
# GStreamer plugin check
gst-inspect-1.0 libcamerasrc
gst-inspect-1.0 x264enc

# Python OpenCV ximgproc check
python -c "import cv2; print('ximgproc' in dir(cv2))"
```

## Optimization Notes

- 멀티스레드 파이프라인 분리(Capture / Process / AI / Tx)
- 캡처 프레임 `clone()` 전략으로 GStreamer 버퍼 점유시간 축소
- queue `leaky=downstream` 및 `max-buffers` 제한으로 backpressure 완화
- `AI_INFERENCE_INTERVAL`로 추론 주기 제어

## Operational Cautions

- 카메라/UART 권한이 필요할 수 있습니다.
- 카메라 충돌 시 기존 점유 프로세스를 먼저 정리하세요.
- `config/AppConfig.h`, `config/edge_device_config.json` 값과 실제 장비 구성이 일치해야 합니다.
- `build/`, 로그, 대용량 산출물은 커밋하지 않는 정책입니다.

## References

- OpenCV Docs: https://docs.opencv.org/
- GStreamer Docs: https://gstreamer.freedesktop.org/documentation/
- TensorFlow Lite: https://www.tensorflow.org/lite
- nlohmann/json: https://github.com/nlohmann/json
