# SubCameraApp Refactory 260311

Raspberry Pi 기반 SubCameraApp 메인 실행 코드입니다.

## 핵심 기능

- GStreamer 기반 실시간 캡처/스트리밍
- Pose 기반 이벤트 감지 파이프라인
- Edge 장치(STM32) 연동 인터페이스
- 이벤트 버퍼링 및 청크 전송

## 디렉터리 요약

- `src/ai`: 추론/룰 기반 감지
- `src/stream`: 캡처/스트림 파이프라인
- `src/controller`: 앱 오케스트레이션
- `src/edge_device`: UART/브릿지 통신
- `src/imageprocessing`: 전처리/강화 로직
- `src/network`, `src/protocol`: 네트워크/패킷 처리
- `src/util`, `src/system`: 공통 유틸/리소스 관리

## 빌드 방법

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

## 실행 방법

```bash
./subcam_main
```

필요 시:

```bash
sudo ./subcam_main
```

## 최적화 체크포인트

- `AppConfig.h`의 AI interval, queue 상한, threshold 조정
- OpenCV thread 수와 GStreamer queue 구성 튜닝
- 장시간 실행 시 로그/메모리 관찰로 backpressure 확인

## 운영 시 주의사항

- 카메라 장치 충돌 시 기존 프로세스 정리 후 실행
- UART 포트 권한/장치명(`/dev/tty*`) 확인
- 개발 환경과 배포 환경의 라이브러리 버전 일치 권장

## 참고

- OpenCV: https://docs.opencv.org/
- GStreamer: https://gstreamer.freedesktop.org/documentation/
- TensorFlow Lite: https://www.tensorflow.org/lite
