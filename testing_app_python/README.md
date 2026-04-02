# testing_app_python

SubCameraApp 파이프라인 검증을 위한 Python GUI 테스트 도구입니다.

## Features

- 전처리 비교(다중 enhancer 결과 비교)
- 실시간 파라미터 튜닝
- Fall rule 시뮬레이션 및 로그 저장

## Environment Requirements

| Item | Requirement |
|---|---|
| Python | 3.10+ |
| Core libs | `numpy`, `opencv-contrib-python`, `PyQt5` |
| AI option | `tflite-runtime` (Raspberry Pi) or `tensorflow` (x86 dev) |
| OS | Windows / Linux / Raspberry Pi OS |

> OpenCV는 `ximgproc`가 필요하므로 `opencv-contrib-python`을 사용하세요.

## Install

현재 폴더에는 `requirements.txt`가 없으므로 아래 명령으로 설치합니다.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install numpy opencv-contrib-python PyQt5

# Raspberry Pi ARM 권장
pip install tflite-runtime

# 개발 PC 대안
# pip install tensorflow
```

원하면 설치 후 고정 파일 생성:

```bash
pip freeze > requirements.txt
```

## Run

```bash
python main.py
```

## Config

- 주요 설정 파일: `config/testing_params.json`
- 조정 대상
  - enhancer 파라미터
  - fall-rule threshold
  - 앱 실행 모드

## Optimization Tips

- 원본 영상 전체 대신 샘플 프레임으로 1차 튜닝
- 한 번에 한 파라미터만 변경해서 영향 분리
- 로그/결과물 디렉터리는 주기적으로 정리

## Cautions

- 카메라 인덱스/입력 소스가 환경마다 다를 수 있습니다.
- OpenCV/Qt 버전 조합에 따라 GUI 렌더링 성능 차이가 발생할 수 있습니다.
- 테스트 로그/CSV는 운영 브랜치 커밋 대상에서 제외하는 것을 권장합니다.

## Validation

```bash
python -c "import cv2; print(cv2.__version__); print('ximgproc' in dir(cv2))"
python -c "import PyQt5; print('PyQt5 OK')"
```

## References

- OpenCV Python: https://docs.opencv.org/
- PyQt5: https://pypi.org/project/PyQt5/
- TensorFlow Lite: https://www.tensorflow.org/lite
