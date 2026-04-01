"""
live_tuning_view.py – Tab 2: 실시간 파라미터 튜닝
유저 요청: 선택한 알고리즘에 해당하는 파라미터만 동적으로 표시
"""
from __future__ import annotations
import numpy as np
import cv2
import time

from PyQt5.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QSlider, QPushButton, QComboBox, QFileDialog,
                               QGroupBox, QGridLayout, QScrollArea, QSizePolicy,
                               QCheckBox)
from PyQt5.QtCore    import Qt, QTimer
from PyQt5.QtGui     import QPixmap, QImage, QColor

from pipeline.enhancer_pipeline import EnhancerPipeline, EnhancerParams, ENHANCER_LABELS
from core.config_loader         import ConfigLoader
from core.metrics_calculator    import MetricsCalculator


def mat_to_qpixmap(img: np.ndarray, w: int, h: int) -> QPixmap:
    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    qi  = QImage(rgb.data, rgb.shape[1], rgb.shape[0],
                 rgb.strides[0], QImage.Format_RGB888)
    return QPixmap.fromImage(qi).scaled(w, h, Qt.KeepAspectRatio, Qt.SmoothTransformation)


class SliderRow(QWidget):
    def __init__(self, label: str, lo: int, hi: int, val: int,
                 scale: float = 1.0, parent=None):
        super().__init__(parent)
        self._scale = scale
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self._lbl = QLabel(f"{label}:")
        self._lbl.setFixedWidth(160)
        self._slider = QSlider(Qt.Horizontal)
        self._slider.setRange(lo, hi)
        self._slider.setValue(val)
        self._val_lbl = QLabel(f"{val * scale:.2f}")
        self._val_lbl.setFixedWidth(55)
        self._slider.valueChanged.connect(
            lambda v: self._val_lbl.setText(f"{v * scale:.2g}"))
        lay.addWidget(self._lbl)
        lay.addWidget(self._slider)
        lay.addWidget(self._val_lbl)

    @property
    def value(self) -> float: return self._slider.value() * self._scale
    def connect(self, cb): self._slider.valueChanged.connect(cb)
    def set_active_style(self, a):
        if a: self._slider.setStyleSheet("QSlider::handle:horizontal { background: orange; }"); self.setStyleSheet("background: #3d2b1f;")
        else: self._slider.setStyleSheet(""); self.setStyleSheet("")


class LiveTuningView(QWidget):
    DISP_W, DISP_H = 1280, 960

    def __init__(self, pipeline: EnhancerPipeline, cfg: ConfigLoader):
        super().__init__()
        self._p = pipeline; self._cfg = cfg; self._params = EnhancerParams()
        self._cap = None; self._timer = QTimer(); self._timer.timeout.connect(self._update_frame)
        self._build_ui()
        self._update_visibility()
        self._sync()

    def _build_ui(self):
        outer = QHBoxLayout(self)
        # 왼쪽
        left = QVBoxLayout()
        src_bar = QHBoxLayout()
        self._btn_cam = QPushButton("📷 실시간 카메라"); self._btn_video = QPushButton("📁 이미지/영상 파일")
        self._btn_snap = QPushButton("📸 캡쳐"); self._btn_stop = QPushButton("⏹ 정지")
        for b in [self._btn_cam, self._btn_video, self._btn_snap, self._btn_stop]: src_bar.addWidget(b)
        left.addLayout(src_bar)

        disp_bar = QHBoxLayout()
        self._lbl_orig = QLabel("원본"); self._lbl_proc = QLabel("결과")
        for l in [self._lbl_orig, self._lbl_proc]: l.setFixedSize(self.DISP_W//2, self.DISP_H//2); l.setStyleSheet("background:#111;"); disp_bar.addWidget(l)
        left.addLayout(disp_bar)

        self._lbl_info = QLabel("— 대기 중 —")
        self._lbl_info.setStyleSheet("font-size:12px; background:#1e1e1e; padding:6px;")
        left.addWidget(self._lbl_info); outer.addLayout(left, 3)

        # 오른쪽
        right_scroll = QScrollArea(); right_scroll.setWidgetResizable(True)
        right_inner = QWidget(); right = QVBoxLayout(right_inner)

        mode_grp = QGroupBox("인핸서 모드 & 조도(Lux) 제어")
        ml = QVBoxLayout(mode_grp)
        self._combo_mode = QComboBox(); self._combo_mode.addItems(ENHANCER_LABELS + ["AdaptiveHybrid(자동)"])
        self._combo_mode.setCurrentIndex(12); ml.addWidget(self._combo_mode)
        lux_lay = QHBoxLayout(); self._chk_lux = QCheckBox("수동 조도 사용"); self._led = QLabel("● AUTO")
        lux_lay.addWidget(self._chk_lux); lux_lay.addWidget(self._led); ml.addLayout(lux_lay)
        self._s_lux = SliderRow("Manual Lux Value", 0, 255, 150); self._s_lux.setEnabled(False); ml.addWidget(self._s_lux)
        right.addWidget(mode_grp)

        # 파라미터 그룹들
        self._g_tm = QGroupBox("ToneMapping")
        lay_tm = QVBoxLayout(self._g_tm); self._s_tm_r = SliderRow("TM Radius", 4, 60, 16); self._s_tm_g = SliderRow("TM Gamma", 10, 100, 50, 0.01)
        lay_tm.addWidget(self._s_tm_r); lay_tm.addWidget(self._s_tm_g); right.addWidget(self._g_tm)

        self._g_ret = QGroupBox("Retinex")
        lay_ret = QVBoxLayout(self._g_ret); self._s_ret_r = SliderRow("GF Radius", 4, 30, 12); self._s_ret_c = SliderRow("CLAHE Limit", 5, 80, 15, 0.1); self._s_ret_k = SliderRow("Illum K", 1, 100, 40)
        lay_ret.addWidget(self._s_ret_r); lay_ret.addWidget(self._s_ret_c); lay_ret.addWidget(self._s_ret_k); right.addWidget(self._g_ret)

        self._g_det = QGroupBox("DetailBoost")
        lay_det = QVBoxLayout(self._g_det); self._s_w1 = SliderRow("Weight 1", 5, 50, 15, 0.1); self._s_w2 = SliderRow("Weight 2", 5, 60, 20, 0.1)
        lay_det.addWidget(self._s_w1); lay_det.addWidget(self._s_w2); right.addWidget(self._g_det)

        self._g_af = QGroupBox("Focus / Sharpening (AF)")
        lay_af = QVBoxLayout(self._g_af); self._s_af = SliderRow("Strength", 5, 100, 20, 0.1)
        lay_af.addWidget(self._s_af); right.addWidget(self._g_af)

        self._btn_save = QPushButton("💾 파라미터 저장"); right.addWidget(self._btn_save)
        right_scroll.setWidget(right_inner); outer.addWidget(right_scroll, 2)

        # 시그널 연결
        self._btn_cam.clicked.connect(self._open_cam); self._btn_video.clicked.connect(self._open_vid)
        self._btn_stop.clicked.connect(self._stop); self._btn_snap.clicked.connect(self._snap)
        self._chk_lux.toggled.connect(self._on_lux_changed); self._combo_mode.currentIndexChanged.connect(self._update_visibility)
        for s in [self._s_tm_r, self._s_tm_g, self._s_ret_r, self._s_ret_c, self._s_ret_k, self._s_w1, self._s_w2, self._s_af, self._s_lux]: s.connect(self._sync)

    def _update_visibility(self):
        idx = self._combo_mode.currentIndex()
        # 0:RET, 1:YUV, 2:WWGIF, 3:TONE, 4:DET, 5:HYB, 6:BAL, 7:CLEAN, 8:RAW, 9:G(AF), 10:R(AF), 11:E(AF), 12:Adapt
        self._g_tm.setVisible(idx in [3, 5, 12])
        self._g_ret.setVisible(idx in [0, 12])
        self._g_det.setVisible(idx in [4, 5, 12])
        self._g_af.setVisible(idx in [9, 10, 11])
        # YUV ADV(1), WWGIF(2), BAL(6), CLEAN(7), RAW(8) 는 현재 추가 파라미터 노출 없음 (C++ 고정값 위주)

    def _on_lux_changed(self, m):
        self._s_lux.setEnabled(m); self._s_lux.set_active_style(m)
        if m:
            self._led.setText("● MANUAL ACTIVE")
            self._led.setStyleSheet("color: white; background-color: #d35400; padding: 4px; border-radius: 4px; font-weight: bold;")
        else:
            self._led.setText("● AUTO (Sensor)")
            self._led.setStyleSheet("color: #27ae60; background-color: #1a1a1a; padding: 4px; border-radius: 4px;")
        self._sync()

    def _sync(self):
        p = self._params; p.tonemap_radius = int(self._s_tm_r.value); p.tonemap_gamma_base = self._s_tm_g.value
        p.retinex_gf_radius = int(self._s_ret_r.value); p.retinex_clahe_limit = self._s_ret_c.value; p.retinex_illum_k = self._s_ret_k.value
        p.detail_w1 = self._s_w1.value; p.detail_w2 = self._s_w2.value; p.focus_strength = self._s_af.value
        p.manual_lux_override = self._chk_lux.isChecked(); p.manual_lux_value = int(self._s_lux.value)
        self._p.update_params(p)

    def _update_frame(self):
        if not self._cap or not self._cap.isOpened(): return
        ok, frame = self._cap.read()
        if not ok: self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0); return
        idx = self._combo_mode.currentIndex()
        if idx < 12: 
            proc = self._p.run_single(idx, frame)
            lv_t = ENHANCER_LABELS[idx]
            hd_str = "N/A"
        else: 
            proc, lv, hd = self._p.run_adaptive(frame)
            lv_t = f"Adaptive Level {lv}"
            hd_str = f"{hd:.1f}"
            
        lat = " | ".join([f"{k}:{v:.1f}ms" for k,v in self._p.latencies.items()])
        mean_lux = frame.mean()
        
        # 지표 계산
        m_raw = MetricsCalculator.compute(frame, "RAW")
        m_proc = MetricsCalculator.compute(proc, "FILTER")
        
        # HTML 5종 지표 표
        metrics_html = (
            f"<table width='100%' style='margin-top:8px; border-collapse:collapse;'>"
            f"<tr style='background:#333;'><th>Metric</th><th>Before (RAW)</th><th>After (Filter)</th><th>Diff</th></tr>"
            f"<tr><td>Sharpness (Lap)</td><td align='center'>{m_raw.sharpness:.0f}</td><td align='center'>{m_proc.sharpness:.0f}</td><td align='center'>{m_proc.sharpness - m_raw.sharpness:+.0f}</td></tr>"
            f"<tr><td>Tenengrad (Edge)</td><td align='center'>{m_raw.tenengrad:.0f}</td><td align='center'>{m_proc.tenengrad:.0f}</td><td align='center'>{m_proc.tenengrad - m_raw.tenengrad:+.0f}</td></tr>"
            f"<tr><td>Colorfulness</td><td align='center'>{m_raw.colorfulness:.1f}</td><td align='center'>{m_proc.colorfulness:.1f}</td><td align='center'>{m_proc.colorfulness - m_raw.colorfulness:+.1f}</td></tr>"
            f"<tr><td>Brightness</td><td align='center'>{m_raw.brightness:.0f}</td><td align='center'>{m_proc.brightness:.0f}</td><td align='center'>{m_proc.brightness - m_raw.brightness:+.0f}</td></tr>"
            f"<tr><td>Noise Estimate</td><td align='center'>{m_raw.noise_estimate:.1f}</td><td align='center'>{m_proc.noise_estimate:.1f}</td><td align='center'>{m_proc.noise_estimate - m_raw.noise_estimate:+.1f}</td></tr>"
            f"</table>"
        )
        
        self._lbl_info.setText(
            f"<p style='margin-bottom:5px;'><b>Image Mean:</b> {mean_lux:.1f} | <b>Hybrid Darkness:</b> {hd_str} <br>"
            f"<b>Mode:</b> {lv_t} | <b>Latency:</b> {lat}</p>{metrics_html}"
        )
        
        cv2.putText(proc, f"Img Mean: {mean_lux:.1f}", (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        cv2.putText(proc, f"Darkness: {hd_str}", (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        cv2.putText(proc, f"Filter: {lv_t}", (10, 90), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

        h, w = self.DISP_H//2, self.DISP_W//2
        self._lbl_orig.setPixmap(mat_to_qpixmap(frame, w, h)); self._lbl_proc.setPixmap(mat_to_qpixmap(proc, w, h))
        self._last = np.hstack([cv2.resize(frame,(w,h)), cv2.resize(proc,(w,h))])

    def _open_cam(self):
        self._stop()
        import platform
        # 라즈베리파이 (Linux) 최적화
        if platform.system() == "Linux":
            pipeline = (
                "libcamerasrc ! "
                "video/x-raw, width=640, height=480, framerate=30/1 ! "
                "videoconvert ! video/x-raw, format=BGR ! appsink"
            )
            print(f"[LiveTuningView] Pi detected. Using GStreamer: {pipeline}")
            self._cap = cv2.VideoCapture(pipeline, cv2.CAP_GSTREAMER)
        # Windows 전용 최적화 설정
        elif platform.system() == 'Windows':
            print("[LiveTuningView] Windows detected. Using DSHOW.")
            self._cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
            if self._cap.isOpened():
                self._cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
                self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
                self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        else:
            self._cap = cv2.VideoCapture(0)

        if self._cap and self._cap.isOpened():
            print(f"[LiveTuningView] Camera opened: {self._cap.getBackendName()}")
            self._timer.start(33)
        else:
            print("[LiveTuningView] Failed to open camera.")

    def _open_vid(self):
        p, _ = QFileDialog.getOpenFileName(self, "파일 선택", "", "Media (*.mp4 *.avi *.jpg *.png)")
        if p: self._stop(); self._cap = cv2.VideoCapture(p); self._timer.start(33)

    def _stop(self):
        self._timer.stop()
        if self._cap:
            self._cap.release()
            self._cap = None

    def _snap(self):
        if hasattr(self, "_last"):
            filename = f"cap_{int(time.time())}.jpg"
            cv2.imwrite(filename, self._last)
            print(f"[LiveTuning] Saved: {filename}")

    def closeEvent(self, e): self._stop(); super().closeEvent(e)
