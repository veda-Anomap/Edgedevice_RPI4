"""
ai_rule_view.py – Tab 3: AI 탐지 룰 시각화
TFLite 실제 추론 모드 / 슬라이더 더미 모드 모두 지원
"""
from __future__ import annotations
import numpy as np
import cv2
import math
import time
import threading
import os

from PyQt5.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QSlider, QPushButton, QGroupBox, QCheckBox,
                               QScrollArea, QComboBox, QFileDialog, QSizePolicy,
                               QProgressBar, QSpinBox, QGridLayout)
from PyQt5.QtCore    import Qt, QTimer, QThread, pyqtSignal
from PyQt5.QtGui     import QPixmap, QImage, QColor, QPainter, QFont

from pipeline.fall_rule_simulator import FallRuleSimulator, FallRuleParams, RuleResult
from core.config_loader           import ConfigLoader
from log.log_recorder             import LogRecorder

# TFLite 선택적 임포트 (지연 로드 제거, 안정적인 tensorflow 사용)
# [주의] 모듈 레벨에서 import tensorflow 시 윈도우에서 QT와 충돌로 인한 silent crash 발생 가능.
# 따라서 InferenceThread 내에서 지연 로드함.


def mat_to_qpixmap(img: np.ndarray, w: int, h: int) -> QPixmap:
    # [중요] .copy() 필수: NumPy 버퍼 수명 문제로 인한 세그폴트(종료) 방지
    h_img, w_img, c = img.shape
    bytes_per_line = c * w_img
    q_img = QImage(img.data, w_img, h_img, bytes_per_line, QImage.Format_BGR888).copy()
    return QPixmap.fromImage(q_img).scaled(w, h, Qt.KeepAspectRatio, Qt.SmoothTransformation)


class SliderRow(QWidget):
    def __init__(self, label, lo, hi, val, scale=1.0, parent=None):
        super().__init__(parent)
        self._scale = scale
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self._lbl = QLabel(f"{label}:")
        self._lbl.setFixedWidth(220)
        self._slider = QSlider(Qt.Horizontal)
        self._slider.setRange(lo, hi)
        self._slider.setValue(val)
        self._val_lbl = QLabel(f"{val*scale:.2g}")
        self._val_lbl.setFixedWidth(55)
        self._slider.valueChanged.connect(
            lambda v: self._val_lbl.setText(f"{v*scale:.2g}"))
        lay.addWidget(self._lbl)
        lay.addWidget(self._slider)
        lay.addWidget(self._val_lbl)

    @property
    def value(self):
        return self._slider.value() * self._scale

    def set_value(self, v):
        self._slider.setValue(int(v / self._scale))

    def connect(self, cb):
        self._slider.valueChanged.connect(cb)


class FrameBuffer:
    def __init__(self):
        self.lock = threading.Lock()
        self.frame = None

    def set(self, frame):
        with self.lock:
            self.frame = frame.copy()

    def get(self):
        with self.lock:
            return None if self.frame is None else self.frame.copy()

class InferenceThread(QThread):
    result_ready = pyqtSignal(object, float)

    def __init__(self, model_path, threads: int, cfg: ConfigLoader):
        super().__init__()
        self.buffer = FrameBuffer()
        self.running = True
        self._model_path = model_path
        self._threads = threads
        self._cfg = cfg
        
        # [동기화] ai_only_test_done.py 처럼 생성자(메인 스레드 컨텍스트)에서 초기화
        try:
            import tensorflow as tf
            print(f"[AiRuleView] Load TFLite (Direct/Init): {self._model_path}")
            # num_threads 제거 (ai_only_test_done.py와 동일하게)
            self.interpreter = tf.lite.Interpreter(model_path=self._model_path)
            self.interpreter.allocate_tensors()

            self.input_details = self.interpreter.get_input_details()
            self.output_details = self.interpreter.get_output_details()
            self.input_shape = self.input_details[0]['shape']
            self.h, self.w = self.input_shape[1], self.input_shape[2]
            
            # AppConfig::KPT_SKELETON (0-indexed)
            self.SKELETON = [
                (15,13),(13,11),(16,14),(14,12),(11,12),(5,11),(6,12),
                (5,6),(5,7),(6,8),(7,9),(8,10),(1,2),(0,1),(0,2),(1,3),(2,4),(3,5),(4,6)
            ]
        except Exception as e:
            print("[AiRuleView] AI Load Error in __init__:", e)
            raise e

    def set_frame(self, frame):
        self.buffer.set(frame)

    def run(self):
        # run()에서는 루프만 실행
        while self.running:
            frame = self.buffer.get()
            if frame is None:
                self.msleep(10)
                continue

            try:
                # Preprocess
                img = cv2.resize(frame, (self.w, self.h))
                img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
                img = img.astype(np.float32) / 255.0
                input_data = np.expand_dims(img, axis=0)

                t0 = time.time()
                self.interpreter.set_tensor(self.input_details[0]['index'], input_data)
                self.interpreter.invoke()

                # Output [1, 100, 57]
                output = self.interpreter.get_tensor(self.output_details[0]['index'])[0]
                
                detections = []
                h_orig, w_orig = frame.shape[:2]
                
                conf_thresh = self._cfg.get("fall_detector", "confidence_threshold", 0.5)

                for row in output:
                    conf = row[4]
                    if conf > conf_thresh:
                        x1 = int(row[0] * w_orig)
                        y1 = int(row[1] * h_orig)
                        x2 = int(row[2] * w_orig)
                        y2 = int(row[3] * h_orig)
                        bw, bh = x2 - x1, y2 - y1
                        
                        kpts = []
                        for i in range(17):
                            kx = int(row[6 + i*3] * w_orig)
                            ky = int(row[6 + i*3 + 1] * h_orig)
                            kpts.append((kx, ky))
                        
                        detections.append({'bbox': (x1, y1, bw, bh), 'kpts': kpts})

                latency = (time.time() - t0) * 1000
                self.result_ready.emit(detections, latency)

            except Exception as e:
                print("[AiRuleView] AI Inference Error:", e)

            self.msleep(1)

    def stop(self):
        self.running = False
        self.wait()


def _rule_badge(text: str, ok: bool) -> str:
    """HTML 생성"""
    color = "#22c55e" if ok else "#ef4444"
    icon  = "✅" if ok else "❌"
    return f'<span style="color:{color}; font-size:13px;">{icon} {text}</span>'


class AiRuleView(QWidget):
    """Tab 3: 낙상 감지 룰 시각화 + TFLite ON/OFF 모드"""

    DISP_W, DISP_H = 640, 480
    NUM_KPT = 17
    # COCO skeleton pairs (0-indexed) - AppConfig.h와 동기화
    SKELETON = [(15,13),(13,11),(16,14),(14,12),(11,12),(5,11),(6,12),
                (5,6),(5,7),(6,8),(7,9),(8,10),(1,2),(0,1),(0,2),(1,3),(2,4),(3,5),(4,6)]

    def __init__(self, cfg: ConfigLoader):
        super().__init__()
        self._cfg       = cfg
        self._sim       = FallRuleSimulator(self._load_fall_params())
        self._cap       = None
        self._ai_thread = None
        self._timer     = QTimer()
        self._timer.timeout.connect(self._update_frame)
        self._frame_idx = 0
        self._log_file  = None
        self._last_result: RuleResult | None = None
        self._detections = []
        self._ai_latency = 0.0
        self._build_ui()

    def _load_fall_params(self):
        c = self._cfg
        return FallRuleParams(
            confidence_threshold    = c.get("fall_detector","confidence_threshold",0.5),
            fall_velocity_threshold = c.get("fall_detector","fall_velocity_threshold",18.0),
            side_fall_aspect_ratio  = c.get("fall_detector","side_fall_aspect_ratio",0.9),
            side_fall_angle         = c.get("fall_detector","side_fall_angle",45.0),
            frontal_fall_compression= c.get("fall_detector","frontal_fall_compression",0.45),
            frontal_fall_angle      = c.get("fall_detector","frontal_fall_angle",50.0),
            dynamic_fall_angle      = c.get("fall_detector","dynamic_fall_angle",55.0),
            sitting_angle           = c.get("fall_detector","sitting_angle",50.0),
            vote_window_size        = int(c.get("fall_detector","vote_window_size",6)),
            vote_threshold          = int(c.get("fall_detector","vote_threshold",2)),
        )

    def _build_ui(self):
        outer = QHBoxLayout(self)

        # ── 왼쪽: 영상 + 스켈레톤 ────────────────────────────────────
        left = QVBoxLayout()

        # 소스 제어 (Grid Layout으로 변경)
        src_grp = QGroupBox("영상 소스 및 AI 제어")
        src_lay = QGridLayout(src_grp)
        
        src_lay.addWidget(QLabel("모드:"), 0, 0)
        self._combo_src = QComboBox()
        self._combo_src.addItems(["📷 실시간 스트리밍", "📽️ 동영상 파일", "🖼️ 단일 이미지"])
        src_lay.addWidget(self._combo_src, 0, 1)
        
        src_lay.addWidget(QLabel("Cam Index:"), 0, 2)
        self._spin_cam = QSpinBox()
        self._spin_cam.setRange(0, 10)
        self._spin_cam.setValue(1)
        src_lay.addWidget(self._spin_cam, 0, 3)
        
        self._btn_open = QPushButton("▶ 시작 / 파일 열기")
        self._btn_stop = QPushButton("⏹ 정지")
        self._btn_snap = QPushButton("📸 캡쳐")
        
        src_lay.addWidget(self._btn_open, 1, 0, 1, 2)
        src_lay.addWidget(self._btn_stop, 1, 2, 1, 1)
        src_lay.addWidget(self._btn_snap, 1, 3, 1, 1)
        
        ai_lay = QHBoxLayout()
        self._chk_ai = QCheckBox("TFLite 추론 (yolo26n-pose_int8.tflite)")
        self._spin_threads = QSpinBox()
        self._spin_threads.setRange(1, 4); self._spin_threads.setValue(2)
        ai_lay.addWidget(self._chk_ai); ai_lay.addWidget(QLabel("스레드:")); ai_lay.addWidget(self._spin_threads)
        src_lay.addLayout(ai_lay, 2, 0, 1, 4)
        
        log_lay = QHBoxLayout()
        self._btn_log_start = QPushButton("⏺ 로그 기록 시작"); self._btn_log_stop = QPushButton("⏹ 중단")
        self._btn_log_stop.setEnabled(False)
        log_lay.addWidget(self._btn_log_start); log_lay.addWidget(self._btn_log_stop)
        src_lay.addLayout(log_lay, 3, 0, 1, 4)
        
        left.addWidget(src_grp)
        self._spin_threads.setRange(1, 4)
        self._spin_threads.setValue(2) # 기본값 2로 조정 (연산 부하와 IO 안정성 균형)
        self._spin_threads.setFixedWidth(50)
        self._spin_threads.setToolTip("TFLite Interpreter 스레드 수")

        # 비디오 삽입
        self._lbl_video = QLabel("영상/카메라 선택")
        self._lbl_video.setAlignment(Qt.AlignCenter)
        self._lbl_video.setFixedSize(self.DISP_W, self.DISP_H)
        self._lbl_video.setStyleSheet("background:#111; color:#888; font-size:16px;")
        left.addWidget(self._lbl_video)

        # 지표 정보
        self._lbl_metrics = QLabel("— 지표 대기 중 —")
        self._lbl_metrics.setAlignment(Qt.AlignCenter)
        self._lbl_metrics.setStyleSheet("font-size:12px; padding:4px;")
        left.addWidget(self._lbl_metrics)

        outer.addLayout(left, 3)

        # ── 오른쪽: 룰 상태 + 파라미터 슬라이더 ─────────────────────
        right_scroll = QScrollArea()
        right_scroll.setWidgetResizable(True)
        right_inner = QWidget()
        right = QVBoxLayout(right_inner)

        # 룰 상태 패널
        rule_grp = QGroupBox("룰 판정 상태")
        self._rule_lbl = QLabel("— 대기 중 —")
        self._rule_lbl.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        self._rule_lbl.setWordWrap(True)
        self._rule_lbl.setTextFormat(Qt.RichText)
        self._rule_lbl.setMinimumHeight(240)
        self._rule_lbl.setStyleSheet("padding:8px; font-size:13px; background:#1e1e1e; color:#fff;")
        rl = QVBoxLayout(rule_grp)
        rl.addWidget(self._rule_lbl)
        right.addWidget(rule_grp)

        # 슬라이더 모드 그룹 (TFLite OFF 시 활성화)
        self._slider_grp = QGroupBox("슬라이더 모드 — 지표 직접 입력")
        sl_lay = QVBoxLayout(self._slider_grp)
        self._s_body_h  = SliderRow("Body Height(px)",   1, 600, 300)
        self._s_body_w  = SliderRow("Body Width(px)",    1, 600, 80)
        self._s_box_h   = SliderRow("Box Height(px)",    1, 600, 320)
        self._s_angle   = SliderRow("Spine Angle(deg)",  0, 90,  70)
        self._s_vel     = SliderRow("Velocity Δcy(px)",  0, 100, 0)
        self._s_knee_y  = SliderRow("Knee Y(px)",        0, 600, 400)
        self._s_hip_y   = SliderRow("Hip Y(px)",         0, 600, 300)
        self._s_ankle_y = SliderRow("Ankle Y(px)",       0, 600, 470)
        for s in [self._s_body_h, self._s_body_w, self._s_box_h, self._s_angle,
                  self._s_vel, self._s_knee_y, self._s_hip_y, self._s_ankle_y]:
            sl_lay.addWidget(s)
            s.connect(self._on_slider_changed)
        right.addWidget(self._slider_grp)

        # 파라미터 튜닝 그룹
        param_grp = QGroupBox("낙상 룰 파라미터 조정 (물리/상태 머신)")
        pg = QVBoxLayout(param_grp)
        self._sp_norm_vel = SliderRow("Norm Velocity Thresh", 5, 50, 12, 0.1)
        self._sp_still    = SliderRow("Stillness Thresh(s)",   5, 50, 15, 0.1)
        self._sp_ground   = SliderRow("Ground Zone Ratio",     50, 95, 80, 0.01)
        
        self._sp_sar   = SliderRow("Side AspectRatio×10", 3, 25, 9, 0.1)
        self._sp_sang  = SliderRow("Side Angle(deg)",     10, 85, 45)
        self._sp_fcomp = SliderRow("Frontal Compress×100",10, 90, 45, 0.01)
        self._sp_fang  = SliderRow("Frontal Angle(deg)",  10, 85, 50)
        self._sp_dang  = SliderRow("Dynamic Angle(deg)",  10, 85, 55)
        self._sp_sit   = SliderRow("Sitting Angle(deg)",  10, 85, 50)
        
        for s in [self._sp_norm_vel, self._sp_still, self._sp_ground,
                  self._sp_sar, self._sp_sang, self._sp_fcomp,
                  self._sp_fang, self._sp_dang, self._sp_sit]:
            pg.addWidget(s)
            s.connect(self._on_param_changed)
        self._btn_save_fp = QPushButton("💾 낙상 파라미터 저장")
        self._btn_save_fp.clicked.connect(self._save_fall_params)
        pg.addWidget(self._btn_save_fp)
        right.addWidget(param_grp)

        right_scroll.setWidget(right_inner)
        outer.addWidget(right_scroll, 2)

        # 시그널 연결
        self._btn_open.clicked.connect(self._on_open_source)
        self._btn_stop.clicked.connect(self._stop)
        self._btn_snap.clicked.connect(self._snap)
        self._chk_ai.toggled.connect(self._on_ai_toggled)
        self._btn_log_start.clicked.connect(self._start_log)
        self._btn_log_stop.clicked.connect(self._stop_log)

    # ── TFLite 로드 (메인 스레드 할당 - 안정성 우선) ────────────────────
    def _on_ai_toggled(self, checked: bool):
        if checked:
            # [수정] 스케일/경로 안정성 위해 절대 경로 사용 (gui 폴더의 부모 폴더)
            import os
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            model_name = self._cfg.get_str("app", "model_path", "yolo26n-pose_int8.tflite")
            model_path = os.path.join(base_dir, model_name)
            
            if not os.path.exists(model_path):
                print(f"[AiRuleView] Error: Model not found at {model_path}")
                self._chk_ai.setChecked(False)
                return

            threads = self._spin_threads.value()
            self._chk_ai.setEnabled(False)
            self._chk_ai.setText("🔄 모델 로딩 중 (메인 스레드)...")
            
            try:
                self._ai_thread = InferenceThread(model_path, threads, self._cfg)
            except Exception as e:
                print(f"[AiRuleView] AI Start Error: {e}")
                self._chk_ai.setChecked(False)
                self._chk_ai.setEnabled(True)
                self._chk_ai.setText("TFLite 추론 (로드 실패)")
                return
                
            self._ai_thread.result_ready.connect(self._on_ai_result)
            
            import platform
            self._ai_thread.start() # Normal Priority
            
            # 로딩 상태 표시 후 1초 뒤에 버튼 활성화 (임시)
            QTimer.singleShot(1500, lambda: self._chk_ai.setEnabled(True))
            QTimer.singleShot(1500, lambda: self._chk_ai.setText("TFLite 추론 ON"))
            self._slider_grp.setEnabled(False)
            print("[AiRuleView] AI Inference Thread (Async Load) Started.")
        else:
            if self._ai_thread:
                self._ai_thread.stop()
                self._ai_thread = None
            self._chk_ai.setText("TFLite 추론 ON")
            self._slider_grp.setEnabled(True)
            self._detections = []

    def _on_ai_result(self, detections, latency):
        self._detections = detections
        self._ai_latency = latency

    # ── 소스 제어 ─────────────────────────────────────────────────────
    def _on_open_source(self):
        mode_idx = self._combo_src.currentIndex()
        if mode_idx == 0: # 카메라
            self._open_camera()
        elif mode_idx == 1: # 비디오
            self._open_video()
        elif mode_idx == 2: # 단일 이미지
            self._open_image()

    def _open_camera(self):
        self._stop()
        idx = self._spin_cam.value()
        import platform
        if platform.system() == 'Windows':
            print(f"[AiRuleView] Attempting Windows DSHOW (Index: {idx})")
            self._cap = cv2.VideoCapture(idx, cv2.CAP_DSHOW)
            if not self._cap or not self._cap.isOpened():
                print("[AiRuleView] DSHOW Failed. Trying MSMF...")
                self._cap = cv2.VideoCapture(idx, cv2.CAP_MSMF)
            if not self._cap or not self._cap.isOpened():
                print("[AiRuleView] MSMF Failed. Trying CAP_ANY...")
                self._cap = cv2.VideoCapture(idx) # [추가] 마지막 표준 폴백

            if self._cap and self._cap.isOpened():
                self._cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
                self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
                self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
                self._cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            else:
                print(f"[AiRuleView] Failed to open camera at Index {idx}.")
        else:
            self._cap = cv2.VideoCapture(0)

        if self._cap and self._cap.isOpened():
            self._timer.start(33)
            self._btn_open.setEnabled(False)

    def _open_video(self):
        path, _ = QFileDialog.getOpenFileName(self, "동영상 선택", "", "Video (*.mp4 *.avi *.mkv)")
        if path:
            self._stop(); self._cap = cv2.VideoCapture(path); self._timer.start(33); self._btn_open.setEnabled(False)

    def _open_image(self):
        path, _ = QFileDialog.getOpenFileName(self, "이미지 선택", "", "Image (*.jpg *.png *.bmp)")
        if path:
            self._stop()
            img = cv2.imread(path)
            if img is not None:
                self._static_frame = img
                self._timer.start(33) # 타이머로 계속 그림 (AI 실시간 반영 위해)
                self._btn_open.setEnabled(False)

    def _stop(self):
        self._timer.stop()
        if self._cap:
            self._cap.release()
            self._cap = None
            time.sleep(0.3) # [안정성] 리소스 해제 하드웨어 지연 대응
        if hasattr(self, "_static_frame"):
            self._static_frame = None
        if self._ai_thread:
            self._ai_thread.stop()
            self._ai_thread = None
        self._detections = []
        self._btn_open.setEnabled(True)

    def _snap(self):
        """현재 화면 스냅샷 저장"""
        if self._lbl_video.pixmap():
            os.makedirs("snaps", exist_ok=True)
            fname = f"snaps/snap_{int(time.time())}.png"
            self._lbl_video.pixmap().save(fname)
            print(f"[AiRuleView] Snapshot saved: {fname}")

    # ── 로그 ─────────────────────────────────────────────────────────
    def _start_log(self):
        _, self._log_writer, self._log_file = LogRecorder.open_fall_log(".")
        self._btn_log_start.setEnabled(False)
        self._btn_log_stop.setEnabled(True)

    def _stop_log(self):
        if self._log_file:
            self._log_file.close()
            self._log_file = None
        self._log_writer = None
        self._btn_log_start.setEnabled(True)
        self._btn_log_stop.setEnabled(False)

    # ── 파라미터 변경 ─────────────────────────────────────────────────
    def _on_param_changed(self, _=None):
        p = FallRuleParams(
            fall_norm_velocity_thresh = self._sp_norm_vel.value,
            stillness_thresh_sec      = self._sp_still.value,
            ground_zone_ratio         = self._sp_ground.value,
            side_fall_aspect_ratio    = self._sp_sar.value,
            side_fall_angle          = self._sp_sang.value,
            frontal_fall_compression = self._sp_fcomp.value,
            frontal_fall_angle       = self._sp_fang.value,
            dynamic_fall_angle       = self._sp_dang.value,
            sitting_angle            = self._sp_sit.value,
        )
        self._sim.update_params(p)
        if not (self._chk_ai.isChecked() and self._ai_thread): # Changed from self._interp to self._ai_thread
            self._on_slider_changed()

    def _on_slider_changed(self, _=None):
        if self._chk_ai.isChecked() and self._ai_thread: # Changed from self._interp to self._ai_thread
            return
        r = self._sim.simulate_from_metrics(
            body_height  = self._s_body_h.value,
            body_width   = self._s_body_w.value,
            box_height   = self._s_box_h.value,
            spine_angle  = self._s_angle.value,
            velocity_delta= self._s_vel.value,
            knee_y       = self._s_knee_y.value,
            hip_y        = self._s_hip_y.value,
            ankle_y      = self._s_ankle_y.value,
        )
        self._last_result = r
        self._refresh_rule_panel(r)
        blank = np.zeros((self.DISP_H, self.DISP_W, 3), np.uint8)
        self._draw_dummy_skeleton(blank, r)
        self._lbl_video.setPixmap(mat_to_qpixmap(blank, self.DISP_W, self.DISP_H))

    def _save_fall_params(self):
        c = self._cfg
        p = self._sim._params
        c.set("fall_detector", "fall_norm_velocity_thresh", p.fall_norm_velocity_thresh)
        c.set("fall_detector", "stillness_thresh_sec",      p.stillness_thresh_sec)
        c.set("fall_detector", "ground_zone_ratio",         p.ground_zone_ratio)
        c.set("fall_detector", "side_fall_aspect_ratio",    p.side_fall_aspect_ratio)
        c.set("fall_detector", "side_fall_angle",          p.side_fall_angle)
        c.set("fall_detector", "frontal_fall_compression", p.frontal_fall_compression)
        c.set("fall_detector", "frontal_fall_angle",       p.frontal_fall_angle)
        c.set("fall_detector", "dynamic_fall_angle",       p.dynamic_fall_angle)
        c.set("fall_detector", "sitting_angle",            p.sitting_angle)
        c.save()

    def _update_frame(self):
        frame = None
        if hasattr(self, "_static_frame") and self._static_frame is not None:
            frame = self._static_frame
        elif self._cap and self._cap.isOpened():
            ok, f = self._cap.read()
            if not ok:
                if self._cap.get(cv2.CAP_PROP_FRAME_COUNT) > 0:
                    self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0) # Video Loop
                else:
                    print("[AiRuleView] 프레임 읽기 실패 (CAP_READ_ERROR)")
                    self._stop()
                return
            frame = f

        if frame is None: return
        
        self._frame_idx += 1
        canvas = frame.copy()

        if self._chk_ai.isChecked() and self._ai_thread:
            # 스레드로 프레임 전송 (비동기)
            self._ai_thread.set_frame(frame)
            
            # 이전 결과(또는 최신 결과)로 드로잉
            for det in self._detections:
                box = det['bbox']
                kpts = det['kpts']
                
                # 낙상 시뮬레이터 적용
                # track_id가 명시되지 않은 점을 고려하여 enumerate 사용
                pass

            for track_id, det in enumerate(self._detections):
                box = det['bbox']
                kpts = det['kpts']
                r = self._sim.check_fall(box, kpts, track_id, img_h=canvas.shape[0])
                self._last_result = r
                self._refresh_rule_panel(r)
                if self._log_writer:
                    LogRecorder.write_fall_row(self._log_writer, self._frame_idx, track_id, r)
                
                self._draw_skeleton(canvas, kpts, r.is_falling)
                x, y, bw, bh = box
                color = (0,0,255) if r.is_falling else (0,255,0)
                cv2.rectangle(canvas, (x,y), (x+bw,y+bh), color, 2)
                cv2.putText(canvas, "!!! FALL !!!" if r.is_falling else "OK", (x, y-8),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)
        
        self._lbl_video.setPixmap(mat_to_qpixmap(canvas, self.DISP_W, self.DISP_H))
        if self._last_result:
            r = self._last_result
            self._lbl_metrics.setText(
                f"State: <b>{r.current_state}</b> | NormVel: {r.norm_velocity:.2f} | "
                f"Angle: {r.spine_angle:.1f}° | Scale: {r.body_height:.0f}px"
            )

    def _refresh_rule_panel(self, r: RuleResult):
        state_color = "#3498db" if r.current_state == "STANDING" else ("#e67e22" if r.current_state == "FALLING" else "#e74c3c")
        lines = [
            f"<b style='color:{state_color}; font-size:16px;'>[현 상태: {r.current_state}]</b>",
            "",
            _rule_badge("Phys: Norm Velocity > Threshold",  r.dynamic_cond_a),
            _rule_badge("Ground: Head in Ground Zone",      r.current_state == "LYING_DOWN"),
            _rule_badge("Time: Still for > Thresh",         r.raw_fall),
            "",
            _rule_badge("Legacy Side: AspectRatio + Angle", r.side_fall),
            "",
            _rule_badge(f"결과: <b>{'⚠️ FALL DETECTED' if r.is_falling else 'SAFE'}</b>",
                        r.is_falling),
        ]
        self._rule_lbl.setText("<br>".join(lines))

    def _draw_skeleton(self, canvas: np.ndarray, kpts: list, is_fall: bool):
        color = (0, 0, 255) if is_fall else (0, 255, 0)
        for a, b in self.SKELETON:
            if a < len(kpts) and b < len(kpts):
                p1 = kpts[a]; p2 = kpts[b]
                # 0,0 좌표(미검출)는 그리지 않음
                if p1[0] > 0 and p1[1] > 0 and p2[0] > 0 and p2[1] > 0:
                    cv2.line(canvas, p1, p2, (255, 255, 0), 2)
        for pt in kpts:
            if pt[0] > 0 and pt[1] > 0:
                cv2.circle(canvas, pt, 3, (0, 255, 255), -1)

    def _draw_dummy_skeleton(self, canvas: np.ndarray, r: RuleResult):
        cx = self.DISP_W // 2
        verdict = "⚠ FALL" if r.is_falling else "OK"
        col     = (0,0,255) if r.is_falling else (0,255,0)
        cv2.putText(canvas, verdict, (cx-60, 240),
                    cv2.FONT_HERSHEY_SIMPLEX, 2.0, col, 4)

    def closeEvent(self, event):
        self._stop()
        self._stop_log()
        super().closeEvent(event)
