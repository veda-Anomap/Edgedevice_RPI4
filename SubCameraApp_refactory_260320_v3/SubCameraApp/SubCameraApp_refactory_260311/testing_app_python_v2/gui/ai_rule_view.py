"""
ai_rule_view.py – Tab 3: AI 탐지 룰 시각화
TFLite 실제 추론 모드 / 슬라이더 더미 모드 모두 지원
"""
from __future__ import annotations
import numpy as np
import cv2
import math
import time

from PyQt5.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QSlider, QPushButton, QGroupBox, QCheckBox,
                               QScrollArea, QComboBox, QFileDialog, QSizePolicy,
                               QProgressBar, QSpinBox)
from PyQt5.QtCore    import Qt, QTimer
from PyQt5.QtGui     import QPixmap, QImage, QColor, QPainter, QFont

from pipeline.fall_rule_simulator import FallRuleSimulator, FallRuleParams, RuleResult
from core.config_loader           import ConfigLoader
from log.log_recorder             import LogRecorder

# TFLite 선택적 임포트 (지연 로딩)
TFLITE_AVAILABLE = False
tflite = None


def mat_to_qpixmap(img: np.ndarray, w: int, h: int) -> QPixmap:
    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    qi  = QImage(rgb.data, rgb.shape[1], rgb.shape[0],
                 rgb.strides[0], QImage.Format_RGB888)
    return QPixmap.fromImage(qi).scaled(w, h, Qt.KeepAspectRatio, Qt.SmoothTransformation)


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


def _rule_badge(text: str, ok: bool) -> str:
    """HTML 생성"""
    color = "#22c55e" if ok else "#ef4444"
    icon  = "✅" if ok else "❌"
    return f'<span style="color:{color}; font-size:13px;">{icon} {text}</span>'


class AiRuleView(QWidget):
    """Tab 3: 낙상 감지 룰 시각화 + TFLite ON/OFF 모드"""

    DISP_W, DISP_H = 640, 480
    NUM_KPT = 17
    # COCO skeleton pairs (0-indexed)
    SKELETON = [(15,13),(13,11),(16,14),(14,12),(11,12),(5,11),(6,12),
                (5,6),(5,7),(6,8),(7,9),(8,10),(1,2),(0,1),(0,2),(1,3),(2,4),(3,5),(4,6)]

    def __init__(self, cfg: ConfigLoader):
        super().__init__()
        self._cfg       = cfg
        self._sim       = FallRuleSimulator(self._load_fall_params())
        self._cap: cv2.VideoCapture | None = None
        self._interp    = None
        self._timer     = QTimer()
        self._timer.timeout.connect(self._update_frame)
        self._frame_idx = 0
        self._log_writer= None
        self._log_file  = None
        self._last_result: RuleResult | None = None
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

        # 소스 제어
        src_bar = QHBoxLayout()
        self._btn_cam     = QPushButton("📷 카메라")
        self._btn_video   = QPushButton("📁 동영상")
        self._btn_stop    = QPushButton("⏹ 정지")
        
        self._chk_ai      = QCheckBox("TFLite 추론 ON")
        self._chk_ai.setChecked(False)

        self._spin_threads = QSpinBox()
        self._spin_threads.setRange(1, 4)
        self._spin_threads.setValue(3)
        self._spin_threads.setFixedWidth(50)
        self._spin_threads.setToolTip("TFLite Interpreter 스레드 수")

        self._btn_log_start = QPushButton("▶ 로그 시작")
        self._btn_log_stop  = QPushButton("⏹ 로그 중단")
        self._btn_log_stop.setEnabled(False)

        for w in [self._btn_cam, self._btn_video, self._btn_stop,
                  self._chk_ai, QLabel("스레드:"), self._spin_threads,
                  self._btn_log_start, self._btn_log_stop]:
            src_bar.addWidget(w)
        left.addLayout(src_bar)

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
        param_grp = QGroupBox("낙상 룰 파라미터 조정")
        pg = QVBoxLayout(param_grp)
        self._sp_vel   = SliderRow("Velocity Thresh",     20, 800, 180, 0.1)
        self._sp_sar   = SliderRow("Side AspectRatio×10", 3, 25, 9, 0.1)
        self._sp_sang  = SliderRow("Side Angle(deg)",     10, 85, 45)
        self._sp_fcomp = SliderRow("Frontal Compress×100",10, 90, 45, 0.01)
        self._sp_fang  = SliderRow("Frontal Angle(deg)",  10, 85, 50)
        self._sp_dang  = SliderRow("Dynamic Angle(deg)",  10, 85, 55)
        self._sp_sit   = SliderRow("Sitting Angle(deg)",  10, 85, 50)
        self._sp_vwin  = SliderRow("Vote Window",          1, 30, 6)
        self._sp_vthr  = SliderRow("Vote Threshold",       1, 15, 2)
        for s in [self._sp_vel, self._sp_sar, self._sp_sang, self._sp_fcomp,
                  self._sp_fang, self._sp_dang, self._sp_sit, self._sp_vwin, self._sp_vthr]:
            pg.addWidget(s)
            s.connect(self._on_param_changed)
        self._btn_save_fp = QPushButton("💾 낙상 파라미터 저장")
        self._btn_save_fp.clicked.connect(self._save_fall_params)
        pg.addWidget(self._btn_save_fp)
        right.addWidget(param_grp)

        right_scroll.setWidget(right_inner)
        outer.addWidget(right_scroll, 2)

        # 연결
        self._btn_cam.clicked.connect(self._open_camera)
        self._btn_video.clicked.connect(self._open_video)
        self._btn_stop.clicked.connect(self._stop)
        self._chk_ai.toggled.connect(self._on_ai_toggled)
        self._btn_log_start.clicked.connect(self._start_log)
        self._btn_log_stop.clicked.connect(self._stop_log)

    # ── TFLite 로드 ───────────────────────────────────────────────────
    def _load_tflite(self):
        global TFLITE_AVAILABLE, tflite
        print("[AiRuleView] TFLite 라이브러리 (tflite-runtime) 로드 시도...")
        try:
            import tflite_runtime.interpreter as _tflite
            tflite = _tflite
            TFLITE_AVAILABLE = True
        except ImportError:
            print("[AiRuleView] tflite-runtime을 찾을 수 없습니다. (pip install tflite-runtime)")
            TFLITE_AVAILABLE = False
            self._chk_ai.setChecked(False)
            self._chk_ai.setText("TFLite 미설치")
            return False

        model_path = self._cfg.get_str("app", "model_path", "yolo26n-pose_int8.tflite")
        threads = self._spin_threads.value()
        try:
            print(f"[AiRuleView] 모델 파일 로드: {model_path} (Threads: {threads})")
            self._interp = tflite.Interpreter(model_path=str(model_path), num_threads=threads)
            self._interp.allocate_tensors()
            self._in_det  = self._interp.get_input_details()[0]
            self._out_det = self._interp.get_output_details()[0]
            print(f"[AiRuleView] TFLite 로드 완료")
            return True
        except Exception as e:
            print(f"[AiRuleView] TFLite 초기화 실패: {e}")
            self._interp = None
            return False

    def _on_ai_toggled(self, checked: bool):
        if checked:
            ok = self._load_tflite()
            if not ok:
                 self._chk_ai.setChecked(False)
            self._slider_grp.setEnabled(not self._chk_ai.isChecked())
        else:
            self._interp = None
            self._slider_grp.setEnabled(True)

    # ── 소스 제어 ─────────────────────────────────────────────────────
    def _open_camera(self):
        self._stop()
        self._cap = cv2.VideoCapture(0)
        self._timer.start(33)

    def _open_video(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "동영상", "", "Video (*.mp4 *.avi *.mkv *.mov)")
        if path:
            self._stop()
            self._cap = cv2.VideoCapture(path)
            self._timer.start(33)

    def _stop(self):
        self._timer.stop()
        if self._cap:
            self._cap.release()
            self._cap = None

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
            fall_velocity_threshold  = self._sp_vel.value,
            side_fall_aspect_ratio   = self._sp_sar.value,
            side_fall_angle          = self._sp_sang.value,
            frontal_fall_compression = self._sp_fcomp.value,
            frontal_fall_angle       = self._sp_fang.value,
            dynamic_fall_angle       = self._sp_dang.value,
            sitting_angle            = self._sp_sit.value,
            vote_window_size         = int(self._sp_vwin.value),
            vote_threshold           = int(self._sp_vthr.value),
        )
        self._sim.update_params(p)
        if not (self._chk_ai.isChecked() and self._interp):
            self._on_slider_changed()

    def _on_slider_changed(self, _=None):
        if self._chk_ai.isChecked() and self._interp:
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
        c.set("fall_detector", "fall_velocity_threshold",  p.fall_velocity_threshold)
        c.set("fall_detector", "side_fall_aspect_ratio",   p.side_fall_aspect_ratio)
        c.set("fall_detector", "side_fall_angle",          p.side_fall_angle)
        c.set("fall_detector", "frontal_fall_compression", p.frontal_fall_compression)
        c.set("fall_detector", "frontal_fall_angle",       p.frontal_fall_angle)
        c.set("fall_detector", "dynamic_fall_angle",       p.dynamic_fall_angle)
        c.set("fall_detector", "sitting_angle",            p.sitting_angle)
        c.set("fall_detector", "vote_window_size",         p.vote_window_size)
        c.set("fall_detector", "vote_threshold",           p.vote_threshold)
        c.save()

    # ── TFLite 추론 ───────────────────────────────────────────────────
    def _infer(self, frame: np.ndarray) -> list[tuple]:
        h, w = frame.shape[:2]
        inp = cv2.resize(frame, (640, 640))
        inp = cv2.cvtColor(inp, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        inp = inp[np.newaxis]
        self._interp.set_tensor(self._in_det["index"], inp)
        self._interp.invoke()
        out = self._interp.get_tensor(self._out_det["index"])[0]
        conf_thresh = self._cfg.get("fall_detector","confidence_threshold",0.5)
        detections = []
        for row in out:
            if row[4] < conf_thresh: continue
            x1 = int(row[0] * w); y1 = int(row[1] * h)
            x2 = int(row[2] * w); y2 = int(row[3] * h)
            box = (x1, y1, x2-x1, y2-y1)
            kpts = [(int(row[6+j*3]*w), int(row[6+j*3+1]*h)) for j in range(17)]
            detections.append((box, kpts))
        return detections

    def _update_frame(self):
        if not self._cap or not self._cap.isOpened(): return
        ok, frame = self._cap.read()
        if not ok:
            self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
            return
        self._frame_idx += 1
        canvas = frame.copy()

        if self._chk_ai.isChecked() and self._interp:
            try:
                detections = self._infer(frame)
            except:
                detections = []
            for track_id, (box, kpts) in enumerate(detections):
                r = self._sim.check_fall(box, kpts, track_id)
                self._last_result = r
                self._refresh_rule_panel(r)
                if self._log_writer:
                    LogRecorder.write_fall_row(self._log_writer, self._frame_idx, track_id, r)
                self._draw_skeleton(canvas, kpts, r.is_falling)
                x, y, bw, bh = box
                color = (0,0,255) if r.is_falling else (0,255,0)
                cv2.rectangle(canvas, (x,y), (x+bw,y+bh), color, 2)
                cv2.putText(canvas, "⚠ FALL" if r.is_falling else "OK", (x, y-8),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)
        
        self._lbl_video.setPixmap(mat_to_qpixmap(canvas, self.DISP_W, self.DISP_H))
        if self._last_result:
            r = self._last_result
            self._lbl_metrics.setText(
                f"Body H:{r.body_height:.0f}px  W:{r.body_width:.0f}px  "
                f"Compress:{r.compression_ratio:.2f}  Angle:{r.spine_angle:.1f}°  "
                f"Vel:{r.velocity_delta:.1f}px  "
                f"Vote:{r.vote_count}/{r.vote_window}"
            )

    def _refresh_rule_panel(self, r: RuleResult):
        lines = [
            _rule_badge("SideA: box_w > body_h×ratio",  r.side_cond_a),
            _rule_badge("SideB: angle < side_angle",    r.side_cond_b),
            _rule_badge("→ 측면낙상",                   r.side_fall),
            "",
            _rule_badge("FrontA: compression < thresh", r.frontal_cond_a),
            _rule_badge("FrontB: angle < front_angle",  r.frontal_cond_b),
            _rule_badge("→ 정면낙상",                   r.frontal_fall),
            "",
            _rule_badge("DynA: velocity > thresh",      r.dynamic_cond_a),
            _rule_badge("DynB: angle < dyn_angle",      r.dynamic_cond_b),
            _rule_badge("→ 동적낙상",                   r.dynamic_fall),
            "",
            _rule_badge("앉기 필터 (억제)",              r.is_sitting),
            "",
            _rule_badge(f"Vote {r.vote_count}/{r.vote_window}  → 낙상 확정",
                        r.is_falling),
        ]
        self._rule_lbl.setText("<br>".join(lines))

    def _draw_skeleton(self, canvas: np.ndarray, kpts: list, is_fall: bool):
        color = (0, 0, 255) if is_fall else (0, 255, 0)
        for a, b in self.SKELETON:
            if a < len(kpts) and b < len(kpts):
                cv2.line(canvas, kpts[a], kpts[b], color, 2)
        for pt in kpts:
            cv2.circle(canvas, pt, 4, (255, 255, 0), -1)

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
