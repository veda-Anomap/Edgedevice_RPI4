# AI Intelligence Layer

## 📁 폴더 개요
본 폴더는 시스템의 두뇌 역할을 하며, 주입된 영상 프레임에서 인간의 포즈를 추정(Pose Estimation)하고 낙상 이벤트를 감지(Fall Detection)하는 고수준 AI 로직을 담당합니다.

## 🛠 문제 해결 및 최적화
라즈베리 파이 4B의 제한된 CPU 자원을 효율적으로 활용하기 위해 다음과 같은 기법이 적용되었습니다.

1.  **AI 추론 다운샘플링 (~1 FPS)**:
    - 매 프레임 추론을 수행하지 않고, `StreamPipeline` 레벨에서 약 30프레임(1초)마다 한 번씩만 추론을 수행하도록 최적화하여 CPU 발열과 스로틀링을 방지합니다.
2.  **Zero-copy 데이터 주입**:
    - `PoseEstimator::detect` 과정에서 OpenCV `Mat` 데이터를 TFLite 텐서로 복사할 때, 기존의 중간 버퍼를 거치는 방식 대신 `preprocessor.preprocessToTensor`를 사용하여 입력 텐서 메모리에 직접 쓰기를 수행, 불필요한 메모리 복사를 1회 제거했습니다.
3.  **상태 기반 낙상 판정 (Hysteresis)**:
    - 단순히 한 프레임의 결과만 보지 않고, `FallDetector` 내부에서 상태를 유지하여 일시적인 오탐지(False Positive)를 방지하는 로직이 적용되어 있습니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: `StreamPipeline`으로부터 전달된 640x480 (또는 전처리 완료된) 영상 프레임.
- **Output**: 
  - `StreamPipeline`: 추론 결과(관절 좌표, 낙상 여부) 반환.
  - `controller`: 낙상 발생 시 즉시 JSON 알림 트리거 신호 전달.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **SRP (Single Responsibility)**: `IAiDetector`가 추론 기능을, `FallDetector`가 논리적 판별 기능을 분리하여 명확한 책임을 가집니다.
- **DIP (Dependency Inversion)**: `StreamPipeline`은 `IAiDetector`라는 인터페이스에 의존하며, 구체적인 `PoseEstimator` 클래스와는 분리되어 있어 모델 교체가 용이합니다.

### ⚠️ 코드 클린업 대상
- **Hard-coded Constants**: `PoseEstimator.cpp`의 후처리 루프에서 키포인트 인덱스들이 다소 하드코딩되어 있습니다. 이를 `AppConfig` 내의 `KEYPOINT_MAP` 등을 참조하도록 개선하면 가독성이 향상될 것입니다.
