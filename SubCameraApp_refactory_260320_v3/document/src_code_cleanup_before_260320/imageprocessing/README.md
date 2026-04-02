# Image Processing & Enhancement Layer

## 📁 폴더 개요
본 폴더는 시스템의 시각적 품질과 AI 인식률을 높이기 위한 이미지 전처리 및 강화 알고리즘을 담당합니다. 특히 저조도 환경에서의 품질 개선과 AI 모델 최적화 규격 변환을 수행합니다.

## 🛠 문제 해결 및 최적화
실시간성 확보를 위해 DOCUMENT (`optimization_guide_v2026.md`)의 제안 사항이 반영되었습니다.

1.  **ARM NEON SIMD 가속**:
    - `ToneMappingEnhancer` 및 `AdaptiveHybridEnhancer` 내부의 픽셀 연산 루프에 ARM NEON Intrinsics를 적용했습니다. 단일 명령으로 4개 이상의 픽셀을 동시 처리하여 기존 C++ 루프 대비 처리 속도를 수배 향상시켰습니다.
2.  **적응형 조명 제어 (Hysteresis)**:
    - `AdaptiveHybridEnhancer`는 영상의 평균 밝기를 분석하여 `Normal`, `Dim`, `Extreme` 모드를 자동 전환합니다. 이때 히스테리시스(Hysteresis) 로직을 적용하여 임계값 근처에서의 화면 깜빡임(Flickering)을 원천 차단했습니다.
3.  **경량 필터 적용**:
    - CPU 부하가 큰 `bilateralFilter`나 `medianBlur` 대신, 성능 손실을 최소화하면서 연산 속도가 매우 빠른 `boxFilter` 중심의 최적화 알고리즘으로 대체되었습니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: `StreamPipeline`으로부터 전달된 카메라 원본 프레임.
- **Output**: 
  - `ai`: 텐서 형식의 정규화된 데이터 (`preprocessToTensor`).
  - `rendering`: 시각적으로 강화된 고품질 프레임.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **LSP (Liskov Substitution)**: 모든 Enhancer가 `IImageEnhancer` 인터페이스를 완벽히 준수하며, `AdaptiveHybridEnhancer` 내부에서 다형적으로 교체 사용됩니다.
- **SRP (Single Responsibility)**: `ImagePreprocessor`는 오직 규격 변환에만, `AdvancedEnhancers`는 영상 품질 개선에만 집중합니다.

### ⚠️ 코드 클린업 대상
- **Optimization Fallback**: NEON 가속 코드가 `#ifdef __ARM_NEON` 등으로 감싸져 있지 않아 x86 환경에서의 빌드 호환성이 부족할 수 있습니다. 멀티 플랫폼 지원을 위한 조건부 컴파일 처리가 필요합니다.
- **Duplicated Logic**: 여러 Enhancer 내부에서 `Mat`의 채널 분리(`split`) 및 병합(`merge`) 과정이 중복 발생합니다. 이를 공통 헬퍼 클래스로 통합하면 코드 중복을 줄일 수 있습니다.
