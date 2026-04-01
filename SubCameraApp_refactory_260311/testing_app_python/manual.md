# SubCameraApp Testing App 사용 설명서 (Manual)

본 어플리케이션은 SubCameraApp의 이미지 전처리 알고리즘과 AI 낙상 탐지 로직을 검증하고 최적의 파라미터를 찾기 위한 테스팅 도구입니다.

---

## 🖼️ Tab 1: 이미지 전처리 비교 (Standard 9-Grid)
여러 이미지 파일에 대해 **C++ 베이스라인과 동일한 9단계 알고리즘**을 적용하여 비교 분석합니다.

- **표준 9단계 알고리즘**:
  1. **RETINEX**: 조명 분리 및 반사율 복원 (야간 시인성)
  2. **YUV ADV**: YUV 감마 LUT + Median 필터 (저조도 노이즈 억제)
  3. **WWGIF**: 가중치 기반 가이드 필터 (엣지 보호)
  4. **TONEMAP**: 역광/고대비 보정
  5. **DETAIL**: 멀티스케일 디테일 부스팅
  6. **HYBRID**: TONEMAP + DETAIL 순차 적용 (최상의 선명도)
  7. **BALANCED_V5**: 자연스러운 색감과 대비
  8. **CleanSharp**: 에지 보존형 샤프닝
  9. **RAW ORIGIN**: 원본 대조용

- **사용법**:
  1. `📂 이미지 파일 선택`: 테스트할 이미지 파일들을 선택합니다.
  2. `▶ 분석 실행`: 선택된 알고리즘들이 적용된 결과 그리드가 하단에 생성됩니다.

---

## 🎛️ Tab 2: 실시간 파라미터 튜닝
카메라 또는 영상 파일 스트림에 대해 파라미터를 실시간으로 조절하며 결과와 성능(Latency)을 확인합니다.

- **주요 기능**:
  - **인핸서 모드**: 특정 필터 고정 또는 `AdaptiveHybrid(자동)` 모드 선택 가능.
  - **Manual Lux Override**: 수동 조도 설정을 통해 특정 환경(저조도 등)에서의 적응형 로직 동작을 테스트합니다. (활성화 시 주황색 인디케이터 점등)
  - **성능 모니터링**: 하단 바에서 각 필터별 처리 시간(ms)을 0.1ms 단위로 실시간 확인 가능합니다.
  - **Focus / Sharpening (AF)**: 3가지 신규 AF 전략(Guided, GradRatio, ELP)을 테스트할 수 있습니다.

---

## 🤖 Tab 3: AI 로직 검증
TFLite 모델을 이용한 실제 추론 또는 수동 슬라이더를 통해 낙상 감지 룰셋을 검증합니다.

- **TFLite 추론 옵션**:
  - `스레드(Num Threads)`: 라즈베리파이 환경에 맞춰 1~4개 중 선택 가능합니다. (권장: 3 또는 4)
- **로그 유틸리티**: `▶ 로그 시작` 버튼으로 추론 결과와 룰 판정 수치를 CSV로 기록할 수 있습니다.

---

## 💡 AF(Focus) 필터 기술 가이드

| 필터명 | 기술 특징 | 권장 용도 |
|:---:|:---|:---|
| **Guided Sharpening** | 가이드 필터 기반의 링잉(Ringing) 억제 샤프닝 | 일반적인 화질 개선, 경계면 보존 |
| **Gradient Ratio** | 원본과 재블러링(Reblur) 이미지의 기울기 비율 분석 | 포커스가 흐릿한(Out-of-focus) 이미지 복원 |
| **Digital ELP** | CVPR 2025 ELP 응용, 모션 변화량 기반 적응형 샤프닝 | 움직임에 의한 고스팅/블러링 억제 |

### [권장] 상황별 임계값 (Threshold) 가이드

| 상황 | 추천 알고리즘 | 권장 Strength | 기대 효과 |
|:---:|:---:|:---:|:---|
| **저조도 (Low-Light)** | YUV ADV / WWGIF | - | 노이즈 없는 밝기 확보 |
| **실내 (Indoor)** | HYBRID / BALANCED_V5 | 1.5 ~ 2.0 | 자연스러운 선명도 |
| **실외 (Outdoor)** | CleanSharp | 2.0 ~ 3.0 | 뚜렷한 외곽선 및 대비 |
| **모션 (Motion)** | Digital ELP (AF) | 2.5 ~ 4.0 | 잔상 제거 및 디블러 |

> [!TIP]
> **성능 최적화**: 라즈베리파이 4B 이상에서는 TFLite 스레드를 3개로 설정할 때 지연 시간과 발열 사이에서 가장 효율적인 성능을 보입니다.

---

## 🛠️ 트러블슈팅: TFLite 설치 오류 (Windows/Python 3.12+)

Windows 환경에서 `pip install tflite-runtime` 실행 시 "No matching distribution" 에러가 발생하는 경우 아래 방법 중 하나를 선택하세요.

### 방법 1: `tensorflow` 설치 (가장 확실함)
`tflite-runtime`은 Windows 및 최신 파이썬(3.12+) 지원이 늦어지는 경우가 많습니다. 이 경우 전체 라이브러리를 설치하면 `tensorflow.lite`를 통해 자동 Fallback 됩니다.
```powershell
pip install tensorflow
```

### 방법 2: 커뮤니티 빌드 Wheel 사용 (경량화)
경량화를 원하신다면 [이곳](https://github.com/bunkahle/tflite-runtime-wheels)과 같이 파이썬 3.12용으로 미리 빌드된 `.whl` 파일을 다운로드하여 직접 설치할 수 있습니다.

---
© 2026 SubCameraApp Dev Team.
