# VEDA Final (2026-04-02)

SubCameraApp 기반 엣지 비전 프로젝트의 최종 스냅샷입니다.
이 브랜치는 **코드 + README 중심**으로 정리되어 있으며, 빌드 산출물/이미지/모델/로그 파일은 제외되어 있습니다.

## 프로젝트 구성

- `SubCameraApp_refactory_260320_v3/`
  - C++ 기반 메인 런타임/스트리밍/AI 파이프라인
  - 하위 `SubCameraApp/SubCameraApp_refactory_260311/`가 실제 코드 루트
- `testing_app_python/`
  - Python GUI 기반 전처리/룰 검증 도구
- 루트 스크립트
  - `260326_firedetect_hanwha_detectenhanced*.py`
  - `ai_only_test_done.py`
  - `pi_fire_fusion_pipeline_ultralytics.py`
  - `rpi_heartbeat.cpp`

## 빠른 시작

### 1) 저장소 클론

```bash
git clone <repo-url>
cd Edgedevice_RPI4
```

### 2) C++ 메인 앱 빌드

```bash
cd SubCameraApp_refactory_260320_v3/SubCameraApp/SubCameraApp_refactory_260311
mkdir -p build
cd build
cmake ..
make -j4
```

### 3) 실행

```bash
./subcam_main
```

> 장치 접근 권한(UART/카메라)에 따라 `sudo`가 필요할 수 있습니다.

### 4) Python 테스트 도구 실행

```bash
cd ../../../../testing_app_python
python -m venv .venv
# Linux/macOS
source .venv/bin/activate
# Windows
# .venv\Scripts\activate
pip install -r requirements.txt
python main.py
```

## 최적화 요소 (요약)

- 멀티스레드 파이프라인 분리
  - Capture / Process / AI / Transmission 단계 분리로 지연 최소화
- 큐/버퍼 안정화
  - 이벤트 큐 상한, 샘플링 기반 AI 입력 게이팅
- 스트림 안정성
  - GStreamer 파이프라인 튜닝 및 backpressure 완화
- 리소스 보호
  - 프로세스/디바이스 충돌 방지용 guard 로직 포함

## 사용 시 주의사항

- 빌드 산출물(`build/`)은 커밋하지 마세요.
- 이미지/영상/모델(`*.png`, `*.jpg`, `*.avi`, `*.pt`, `*.tflite`)은 이 브랜치 정책상 제외됩니다.
- 카메라/STM32 통신 파라미터는 `config/edge_device_config.json`, `config/AppConfig.h`를 먼저 확인하세요.
- Raspberry Pi 환경에서는 OpenCV/GStreamer 버전 차이에 따라 성능 차이가 큽니다.

## 참고 문헌 / 레퍼런스

- OpenCV Documentation: https://docs.opencv.org/
- GStreamer Application Development Manual: https://gstreamer.freedesktop.org/documentation/
- TensorFlow Lite Guide: https://www.tensorflow.org/lite
- nlohmann/json: https://github.com/nlohmann/json

## 브랜치 정책

- `final_0402`: veda_final 스냅샷 기준 배포/검토 브랜치
- 문서/코드 중심 관리, 바이너리/산출물은 제외
