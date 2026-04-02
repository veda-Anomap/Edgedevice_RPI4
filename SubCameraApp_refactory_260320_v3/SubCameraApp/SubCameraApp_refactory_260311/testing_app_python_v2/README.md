# SubCameraApp — 전처리/AI 탐지 테스팅 앱 (Python)

> **SubCameraApp_refactory_260320_v3** 프로젝트의 이미지 전처리 알고리즘과 낙상 감지(AI) 룰을 시각적으로 점검·검증·파라미터 실험하는 Python 기반 GUI 도구입니다.

---

## 주요 기능

| 탭 | 기능 |
|----|------|
| **Tab 1 – 전처리 비교** | 여러 이미지를 선택 → 9개 알고리즘(Retinex, ToneMap, WWGIF 등) 3×3 그리드로 동시 비교. 5종 품질 지표(선명도·채도·밝기·Tenengrad·노이즈) 오버레이 및 JSON/TXT 저장 |
| **Tab 2 – 실시간 튜닝** | 카메라 또는 동영상 파일에서 실시간 스트리밍. 슬라이더로 전처리 파라미터(GF Radius, CLAHE Clip, Gamma, Detail Weight 등)를 즉시 조정. 결과 즉시 반영 및 저장 |
| **Tab 3 – AI 룰 시각화** | **TFLite 추론 모드** (실제 YOLO-Pose 모델 사용) 또는 **슬라이더 더미 모드**에서 낙상 판정 6개 룰 조건을 실시간으로 시각화. 낙상 룰 파라미터 슬라이더 조정 후 CSV 로그 저장 |

---

## 프로젝트 구조

```
testing_app_python/
├── config/
│   └── testing_params.json     ← 모든 파라미터 (default/value/min/max/desc)
├── core/
│   ├── config_loader.py        ← JSON 로드/저장 (section.key 접근)
│   └── metrics_calculator.py   ← 이미지 품질 5종 지표
├── pipeline/
│   ├── enhancer_pipeline.py    ← 9개 인핸서 (C++ AdvancedEnhancers 포트)
│   └── fall_rule_simulator.py  ← 낙상 판정 룰 (C++ FallDetector 포트)
├── gui/
│   ├── preprocess_view.py      ← Tab 1 위젯
│   ├── live_tuning_view.py     ← Tab 2 위젯
│   └── ai_rule_view.py         ← Tab 3 위젯
├── log/
│   └── log_recorder.py         ← CSV/JSON/TXT 로그 저장
├── main.py                     ← 진입점
└── requirements.txt
```

---

## 설치

```bash
# 1. 저장소 클론
git clone <your-repo-url>
cd SubCameraApp/testing_app_python

# 2. 가상환경 생성 (권장)
python -m venv venv
source venv/bin/activate          # Windows: venv\Scripts\activate

# 3. 의존성 설치
pip install -r requirements.txt

# 4. OpenCV ximgproc 확인 (Guided Filter 필수)
#    opencv-contrib-python 이 이미 ximgproc 포함
```

### TFLite 설치 (Tab 3 실제 추론 사용 시)

```bash
# Raspberry Pi / ARM
pip install tflite-runtime

# 데스크탑 (개발/테스트)
pip install tensorflow
```

---

## 실행 방법

```bash
# ① 메뉴 선택 GUI (탭 클릭)
python main.py

# ② Tab 1 — 이미지 파일 직접 지정
python main.py compare photo1.jpg photo2.jpg

# ③ Tab 2 — 카메라 실시간 튜닝
python main.py live cam

# ④ Tab 2 — 동영상 파일 튜닝
python main.py live /path/to/video.mp4

# ⑤ Tab 3 — AI 룰 시각화 (슬라이더/TFLite 모드 선택 가능)
python main.py ai
```

---

## 파라미터 설정 (`config/testing_params.json`)

모든 파라미터는 JSON에서 직접 수정하거나 GUI 슬라이더로 조정하고 **저장** 버튼으로 반영합니다.

```json
{
  "enhancer": {
    "tonemap_gamma_base": { "default": 0.5, "min": 0.1, "max": 1.0, "value": 0.5,
                            "desc": "ToneMappingEnhancer 감마 베이스" }
  },
  "fall_detector": {
    "fall_velocity_threshold": { "default": 18.0, "min": 2.0, "max": 80.0, "value": 18.0,
                                  "desc": "중심 Y 하강 속도 임계값 (px/frame)" }
  }
}
```

| 섹션 | 주요 파라미터 |
|------|--------------|
| `preprocessor` | `target_size`, `normalize_scale` |
| `enhancer` | `retinex_gf_radius`, `tonemap_gamma_base`, `detail_w1/w2`, `adaptive_*_thresh` |
| `fall_detector` | `fall_velocity_threshold`, `side_fall_angle`, `frontal_fall_compression`, `vote_window_size`, … |
| `app` | `ai_mode` (`tflite`/`slider`), `model_path`, `num_threads` |

---

## 인핸서 알고리즘 목록

| 번호 | 클래스 | 기반 |
|------|--------|------|
| 1 | `RetinexEnhancer` | HSV + Guided Filter + CLAHE |
| 2 | `LowLightAdvancedEnhancer` | YUV + 감마 LUT + medianBlur |
| 3 | `WWGIFRetinexEnhancer` | WWGIF 멀티스케일 Retinex |
| 4 | `ToneMappingEnhancer` | CVPR 2024 Zero-Shot Illumination |
| 5 | `DetailBoostEnhancer` | ICCV 2025 다중 스케일 DoG |
| 6 | `HybridEnhancer` | ToneMap + DetailBoost 2단계 |
| 7 | `UltimateBalancedEnhancer` | Lab 색공간 + Guided + S-Curve |
| 8 | `CleanSharpEnhancer` | YUV 색상 보존형 샤프닝 |
| 9 | `AdaptiveHybridEnhancer` | 조도 자동 전환 (4-Level) |

---

## 낙상 감지 룰 (Tab 3)

`FallRuleSimulator` 는 C++ `FallDetector.cpp` 와 동일한 6개 조건을 구현합니다.

| 룰 | 조건 A | 조건 B |
|----|--------|--------|
| 측면 낙상 | `box_w > body_h × ratio` | `angle < side_angle` |
| 정면 낙상 | `compression < threshold` | `angle < frontal_angle` |
| 동적 낙상 | `velocity > threshold` | `angle < dynamic_angle` |
| 앉기 필터 | `knee > hip`, `ankle > knee`, `angle > sitting_angle` → 억제 |
| 투표 | 슬라이딩 윈도우 내 `vote_count >= vote_threshold` → 확정 |

---

## 로그 출력

| 종류 | 파일 | 내용 |
|------|------|------|
| 지표 TXT | `<base>_analysis_<ts>.txt` | 9개 알고리즘 × 5종 지표 |
| 지표 JSON | `<base>_analysis_<ts>.json` | 동일 (구조화) |
| 낙상 룰 CSV | `fall_rules_<ts>.csv` | 프레임별 6개 룰 ON/OFF + 투표 |

---

## 환경 요구사항

| 항목 | 최소 사양 |
|------|-----------|
| Python | 3.10+ |
| OpenCV | 4.8+ (contrib 포함) |
| PyQt5  | 5.15+ |
| TFLite | `tflite-runtime` 또는 `tensorflow` (Tab 3 실제 추론 시) |
| 플랫폼 | Windows / Linux / Raspberry Pi OS (64-bit) |

---

## 기여 및 라이선스

본 도구는 `SubCameraApp` 프로젝트의 개발·검증 용도로 작성되었습니다.
