# testing_app_python

SubCameraApp 파이프라인 검증을 위한 Python GUI 테스트 도구입니다.

## 제공 기능

- 전처리 비교(여러 enhancer 시각 비교)
- 실시간 튜닝(파라미터 조정 즉시 반영)
- AI 룰 시뮬레이션(룰 조건 점검)

## 실행 방법

```bash
python -m venv .venv
# Linux/macOS
source .venv/bin/activate
# Windows
# .venv\Scripts\activate
pip install -r requirements.txt
python main.py
```

## 주요 파라미터

- `config/testing_params.json`
  - enhancer 계열 파라미터
  - fall-rule 계열 threshold
  - 앱 실행 모드 관련 설정

## 최적화 팁

- 대용량 영상보다 샘플 프레임 기반 검증을 먼저 수행
- 룰 튜닝 시 한 번에 하나의 파라미터만 변경
- 로그 파일은 주기적으로 정리

## 주의사항

- OpenCV contrib(`ximgproc`)가 필요한 기능이 있습니다.
- GPU/TFLite 환경에 따라 AI 탭 동작이 달라질 수 있습니다.
- 테스트 결과 CSV/로그는 운영 브랜치에 커밋하지 않는 것을 권장합니다.

## 참고

- PyQt5: https://pypi.org/project/PyQt5/
- OpenCV Python: https://docs.opencv.org/
