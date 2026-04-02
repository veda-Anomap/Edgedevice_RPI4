# Stream Layer (Capture & Pipeline)

## 📁 폴더 개요
본 폴더는 시스템의 데이터 동맥인 **영상 스트림 파이프라인**을 관리합니다. 카메라로부터 원본 영상을 캡처하고, 이를 멀티스레드 기반으로 전처리, AI 추론, 전송 모듈에 공급하는 핵심 역할을 수행합니다.

## 🛠 문제 해결 및 최적화
DOCUMENT (`requestwrap_analysis.md`, `non_blocking_capture_plan.md`)에 정의된 치명적인 안정성 문제를 해결하기 위해 다음과 같은 설계가 적용되었습니다.

1.  **RequestWrap Assertion 해결 (Non-Blocking Capture)**:
    - **문제**: GStreamer의 `libcamerasrc`가 하류(Downstream) 처리 지연으로 인해 버퍼 풀이 고갈되어 크래시가 발생하는 문제.
    - **해결**: 캡처 루프에서 `raw_frame`을 획득 즉시 `clone()`하여 독립 복사본을 생성하고, 하위 GStreamer 자원을 즉시 반환하는 **Non-Blocking** 구조를 구현했습니다.
2.  **GStreamer 파이프라인 최적화 (ISP 가속)**:
    - CPU 부하를 줄이기 위해 `v4l2convert` 하드웨어 ISP 스케일러를 사용하여 `1080p -> 480p` 리사이징을 하드웨어에서 처리합니다.
    - 모든 `queue`에 `leaky=downstream` 및 `max-size-buffers=2` 설정을 추가하여 버퍼 누증을 원천 차단했습니다.
3.  **지연 초기화 (Lazy Initialization)**:
    - `GStreamerCamera`는 실제 `open()` 요청이 올 때까지 `cv::VideoCapture` 객체 생성을 지연시켜, 초기 부팅 시의 라이브러리 간섭 및 하드웨어 프로빙 크래시를 방지합니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: 하드웨어 카메라 및 GStreamer 파이프라인의 영상 스트림.
- **Output**: 
  - `imageprocessing`: 원본 프레임 전달 (전처리 및 개선용).
  - `ai`: 1 FPS 주기로 복사된 프레임 전달 (추론용).
  - `buffer`: 순환 버퍼 및 이벤트 녹화기용 프레임 전달.
  - `network`: 인코딩 및 스트리밍 송출.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **SRP (Single Responsibility)**: `StreamPipeline`은 스레드 오케스트레이션에만 집중하며, 카메라 I/O는 `GStreamerCamera`가 전담하여 원칙을 잘 준수하고 있습니다.
- **OCP (Open/Closed)**: `ICamera` 인터페이스를 통해 GStreamer 외의 다른 카메라 백엔드(예: V4L2 직접 접근)로 변경 시 파이프라인 코드를 수정할 필요가 없습니다.

### ⚠️ 코드 클린업 대상
- **Long Function**: `StreamPipeline::startStreaming()` 함수가 파이프라인 문자열 생성부터 스레드 시작까지 약 100라인 이상을 점유하고 있습니다. 파이프라인 생성 부분(`buildCapturePipeline`, `buildSendPipeline`)을 별도 메서드로 분리하는 것을 추천합니다.
- **Nested Logic**: `cameraLoop()` 내부의 연속 실패 진단 로직(`if (consecutive_failures == 30) ... else if ...`)이 다소 길어 가독성을 해치고 있습니다. 이를 `DiagnosticLogger::logCameraStatus()`와 같은 유틸리티로 추출할 수 있습니다.
