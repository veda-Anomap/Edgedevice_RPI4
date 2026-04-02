#!/usr/bin/env python3
"""
main.py – SubCameraApp Testing App (Python 버전) 진입점

사용법:
  python main.py                          # 메뉴 선택 GUI
  python main.py compare img1.jpg img2.jpg  # Tab 1 시작
  python main.py live cam                   # Tab 2 + 카메라
  python main.py live video.mp4             # Tab 2 + 동영상
  python main.py ai                         # Tab 3 시작
"""
import sys
import os
import platform
import cv2

# ── TensorFlow 로그 제어 (GUI 프리징 방지 및 로그 정리) ──────────────
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
os.environ['TF_ENABLE_ONEDNN_OPTS'] = '0'

# ── 임포트 경로 설정 (패키지 루트를 sys.path 에 추가) ─────────────────
ROOT = os.path.dirname(os.path.abspath(__file__))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

# [최적화] OpenCV 스레드 제한 (TFLite 충돌 방지 및 Pi 성능 확보)
cv2.setNumThreads(1)

from PyQt5.QtWidgets import (QApplication, QMainWindow, QTabWidget,
                              QWidget, QVBoxLayout, QLabel, QPushButton,
                              QHBoxLayout, QStatusBar)
from PyQt5.QtCore    import Qt
from PyQt5.QtGui     import QFont

from core.config_loader         import ConfigLoader
from pipeline.enhancer_pipeline import EnhancerPipeline, EnhancerParams
from gui.preprocess_view        import PreprocessView
from gui.live_tuning_view       import LiveTuningView
from gui.ai_rule_view           import AiRuleView


# ── config 파일 경로 ──────────────────────────────────────────────────
CONFIG_PATH = os.path.join(ROOT, "config", "testing_params.json")
LOG_DIR     = os.path.join(ROOT, "logs")
os.makedirs(LOG_DIR, exist_ok=True)


# ── 메인 윈도우 ───────────────────────────────────────────────────────
class MainWindow(QMainWindow):
    def __init__(self, start_tab: int = 0, extra_args: list | None = None):
        super().__init__()
        print("[MainWindow] Initializing...")
        self.setWindowTitle("SubCameraApp — 전처리/AI 탐지 테스팅 앱")
        self.resize(1400, 900)

        # config 로드
        print(f"[MainWindow] Loading config from {CONFIG_PATH}...")
        self._cfg = ConfigLoader()
        if not self._cfg.load(CONFIG_PATH):
            print(f"[ERROR] config 파일을 찾을 수 없습니다: {CONFIG_PATH}")

        # 공용 파이프라인
        print("[MainWindow] Initializing EnhancerPipeline...")
        self._pipeline = EnhancerPipeline()

        # 상태바
        self._status = QStatusBar()
        self.setStatusBar(self._status)
        self._status.showMessage(f"Config: {CONFIG_PATH}")

        # 탭 위젯
        print("[MainWindow] Setting up Tabs...")
        self._tabs = QTabWidget()
        self._tabs.setFont(QFont("Arial", 11))

        self._tab1 = PreprocessView(self._pipeline, LOG_DIR)
        self._tab2 = LiveTuningView(self._pipeline, self._cfg)
        self._tab3 = AiRuleView(self._cfg)

        self._tabs.addTab(self._tab1, "🖼️ Tab 1: 이미지 전처리 비교")
        self._tabs.addTab(self._tab2, "🎛️ Tab 2: 실시간 파라미터 튜닝")
        self._tabs.addTab(self._tab3, "🤖 Tab 3: AI 로직 검증")

        self._tabs.setCurrentIndex(start_tab)
        # [신속 수정] 탭 전환 시 상호 간섭 방지 (카메라 점유 충돌 해결)
        self._tabs.currentChanged.connect(self._on_tab_changed)
        
        self.setCentralWidget(self._tabs)
        print("[MainWindow] Initialization Complete.")

        # Tab 2: extra_args (video 파일) 자동 오픈
        if start_tab == 1 and extra_args:
            if extra_args[0] == "cam":
                self._tab2._open_camera()
            else:
                self._tab2._cap = cv2.VideoCapture(extra_args[0])
                from PyQt5.QtCore import QTimer
                QTimer.singleShot(100, lambda: self._tab2._timer.start(33))

        # Tab 1: extra_args (이미지 파일들)
        if start_tab == 0 and extra_args:
            self._tab1._paths = extra_args
            self._tab1._src_label.setText(f"선택: {len(extra_args)}개 파일")
            self._tab1._btn_run.setEnabled(True)

    def _on_tab_changed(self, index: int):
        """탭 전환 시 비활성 탭의 카메라 리소스를 명시적으로 해제"""
        if index != 0: self._tab1._stop() if hasattr(self._tab1, '_stop') else None
        if index != 1: self._tab2._stop()
        if index != 2: self._tab3._stop()
        print(f"[MainWindow] Switched to Tab {index+1}. Background tabs released.")

    def closeEvent(self, event):
        self._tab2._stop()
        self._tab3._stop()
        self._tab3._stop_log()
        super().closeEvent(event)


# ── CLI 파싱 ─────────────────────────────────────────────────────────
def parse_args():
    args = sys.argv[1:]
    if not args:
        return 0, []
    cmd = args[0].lower()
    rest = args[1:]
    if cmd == "compare": return 0, rest
    if cmd == "live":    return 1, rest
    if cmd == "ai":      return 2, []
    # 숫자 탭 지정
    if cmd.isdigit():
        return int(cmd) % 3, rest
    return 0, args   # 모든 인수를 이미지 파일로 간주


# ── 진입점 ───────────────────────────────────────────────────────────
def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    # 다크 테마 팔레트
    from PyQt5.QtGui import QPalette, QColor
    palette = QPalette()
    palette.setColor(QPalette.Window,          QColor(30, 30, 30))
    palette.setColor(QPalette.WindowText,      Qt.white)
    palette.setColor(QPalette.Base,            QColor(20, 20, 20))
    palette.setColor(QPalette.AlternateBase,   QColor(40, 40, 40))
    palette.setColor(QPalette.ToolTipBase,     Qt.white)
    palette.setColor(QPalette.ToolTipText,     Qt.white)
    palette.setColor(QPalette.Text,            Qt.white)
    palette.setColor(QPalette.Button,          QColor(50, 50, 50))
    palette.setColor(QPalette.ButtonText,      Qt.white)
    palette.setColor(QPalette.BrightText,      Qt.red)
    palette.setColor(QPalette.Highlight,       QColor(0, 122, 204))
    palette.setColor(QPalette.HighlightedText, Qt.black)
    app.setPalette(palette)

    tab_idx, extra = parse_args()
    
    win = MainWindow(tab_idx, extra)
    win.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
