"""
live_tuning_view.py – Tab 2: 실시간 파라미터 튜닝
유저 요청: Manual Lux 표시(색상/LED), 성능 측정(Latency), AF 필터 추가
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
    def value(self) -> float:
        return self._slider.value() * self._scale

    def set_value(self, v: float):
        self._slider.setValue(int(v / self._scale))

    def connect(self, callback):
        self._slider.valueChanged.connect(callback)

    def set_active_style(self, active: bool):
        if active:
            self._slider.setStyleSheet("QSlider::handle:horizontal { background: orange; }")
            self.setStyleSheet("background: #3d2b1f; border-radius: 4px;")
        else:
            self._slider.setStyleSheet("")
            self.setStyleSheet("")


class LiveTuningView(QWidget):
    """Tab 2: 실시간 스트림 + 파라미터 튜닝 + 성능 측정"""

    DISP_W, DISP_H = 640, 480

    def __init__(self, pipeline: EnhancerPipeline, cfg: ConfigLoader):
        super().__init__()
        self._pipeline = pipeline
        self._cfg      = cfg
        self._cap: cv2.VideoCapture | None = None
        self._timer    = QTimer()
        self._timer.timeout.connect(self._update_frame)
        self._params   = EnhancerParams()
        self._init_params_from_cfg()
        self._build_ui()

    def _init_params_from_cfg(self):
        c = self._cfg
        p = self._params
        p.tonemap_radius      = int(c.get("enhancer", "tonemap_radius", 16))
        p.tonemap_gamma_base  = c.get("enhancer", "tonemap_gamma_base", 0.5)
        p.retinex_gf_radius   = int(c.get("enhancer", "retinex_gf_radius", 12))
        p.retinex_clahe_limit = c.get("enhancer", "retinex_clahe_limit", 1.5)
        p.detail_w1           = c.get("enhancer", "detail_w1", 1.5)
        p.detail_w2           = c.get("enhancer", "detail_w2", 2.0)
        # AF
        p.focus_strength      = c.get("enhancer", "focus_strength", 2.0)

    def _build_ui(self):
        outer = QHBoxLayout(self)

        # ── 왼쪽 패널 ────────────────────────────────────────────────
        left = QVBoxLayout()
        # 소스 선택
        src_bar = QHBoxLayout()
        self._btn_cam   = QPushButton("📷 실시간 카메라")
        self._btn_video = QPushButton("📁 이미지/영상 파일")
        self._btn_snap  = QPushButton("📸 캡쳐")
        self._btn_stop  = QPushButton("⏹ 정지")
        for b in [self._btn_cam, self._btn_video, self._btn_snap, self._btn_stop]:
            b.setMinimumHeight(35)
            src_bar.addWidget(b)
        left.addLayout(src_bar)

        # 디스플레이
        disp_bar = QHBoxLayout()
        self._lbl_orig = QLabel("원본")
        self._lbl_orig.setFixedSize(self.DISP_W // 2, self.DISP_H // 2)
        self._lbl_orig.setStyleSheet("background:#111; border:1px solid #333;")
        self._lbl_proc = QLabel("처리 결과")
        self._lbl_proc.setFixedSize(self.DISP_W // 2, self.DISP_H // 2)
        self._lbl_proc.setStyleSheet("background:#111; border:1px solid #333;")
        disp_bar.addWidget(self._lbl_orig)
        disp_bar.addWidget(self._lbl_proc)
        left.addLayout(disp_bar)

        # 정보 & 성능 하단 바
        self._lbl_info = QLabel("— 대기 중 —")
        self._lbl_info.setStyleSheet("font-size:12px; background:#1e1e1e; padding:6px; color:#aaa; border-top:1px solid #444;")
        self._lbl_info.setWordWrap(True)
        left.addWidget(self._lbl_info)
        outer.addLayout(left, 3)

        # ── 오른쪽 패널 (슬라이더) ───────────────────────────────────
        right_scroll = QScrollArea()
        right_scroll.setWidgetResizable(True)
        right_inner = QWidget()
        right = QVBoxLayout(right_inner)

        # 1. 모드 선택 & Manual Lux
        mode_grp = QGroupBox("인핸서 모드 & 조도(Lux) 제어")
        ml = QVBoxLayout(mode_grp)
        self._combo_mode = QComboBox()
        self._combo_mode.addItems(ENHANCER_LABELS + ["AdaptiveHybrid(자동)"])
        self._combo_mode.setCurrentIndex(len(ENHANCER_LABELS))
        ml.addWidget(self._combo_mode)

        lux_lay = QHBoxLayout()
        self._chk_manual_lux = QCheckBox("수동 조도(Manual Lux) 사용")
        self._led_manual = QLabel("● AUTO")
        self._led_manual.setStyleSheet("color: gray; font-weight: bold;")
        lux_lay.addWidget(self._chk_manual_lux)
        lux_lay.addWidget(self._led_manual)
        ml.addLayout(lux_lay)

        self._s_manual_lux = SliderRow("Manual Lux Value", 0, 255, 150)
        self._s_manual_lux.setEnabled(False)
        ml.addWidget(self._s_manual_lux)
        right.addWidget(mode_grp)

        # 2. 파라미터 그룹들
        def _add_group(title, rows):
            g = QGroupBox(title)
            gl = QVBoxLayout(g)
            for r in rows: gl.addWidget(r)
            right.addWidget(g)

        self._s_tm_r = SliderRow("TM Radius", 4, 60, 16)
        self._s_tm_g = SliderRow("TM Gamma×100", 10, 100, 50, 0.01)
        _add_group("ToneMapping", [self._s_tm_r, self._s_tm_g])

        self._s_ret_clahe = SliderRow("Ret CLAHE×10", 5, 80, 15, 0.1)
        self._s_ret_k = SliderRow("Ret Illum K×10", 1, 15, 6, 0.1)
        _add_group("Retinex", [self._s_ret_clahe, self._s_ret_k])

        self._s_focus_s = SliderRow("Focus Strength×10", 5, 100, 20, 0.1)
        _add_group("Focus / Sharpening (AF)", [self._s_focus_s])

        # 저장 버튼
        self._btn_save = QPushButton("💾 파라미터 저장 (testing_params.json)")
        self._btn_save.clicked.connect(self._save_cfg)
        right.addWidget(self._btn_save)

        right_scroll.setWidget(right_inner)
        outer.addWidget(right_scroll, 2)

        # 시그널
        self._btn_cam.clicked.connect(self._open_camera)
        self._btn_video.clicked.connect(self._open_video)
        self._btn_snap.clicked.connect(self._capture)
        self._btn_stop.clicked.connect(self._stop)
        self._chk_manual_lux.toggled.connect(self._on_lux_mode_changed)
        
        for s in [self._s_tm_r, self._s_tm_g, self._s_ret_clahe, self._s_ret_k, 
                  self._s_focus_s, self._s_manual_lux]:
            s.connect(self._sync_params)

    def _on_lux_mode_changed(self, manual: bool):
        self._s_manual_lux.setEnabled(manual)
        self._s_manual_lux.set_active_style(manual)
        if manual:
            self._led_manual.setText("● MANUAL ACTIVE")
            self._led_manual.setStyleSheet("color: orange; font-weight: bold;")
        else:
            self._led_manual.setText("● AUTO")
            self._led_manual.setStyleSheet("color: gray; font-weight: bold;")
        self._sync_params()

    def _sync_params(self):
        p = self._params
        p.tonemap_radius = int(self._s_tm_r.value)
        p.tonemap_gamma_base = self._s_tm_g.value
        p.retinex_clahe_limit = self._s_ret_clahe.value
        p.retinex_illum_k = self._s_ret_k.value
        p.focus_strength = self._s_focus_s.value
        p.manual_lux_override = self._chk_manual_lux.isChecked()
        p.manual_lux_value = int(self._s_manual_lux.value)
        self._pipeline.update_params(p)

    def _update_frame(self):
        if not self._cap or not self._cap.isOpened(): return
        ok, frame = self._cap.read()
        if not ok:
            self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
            return

        idx = self._combo_mode.currentIndex()
        if idx < len(ENHANCER_LABELS):
            proc = self._pipeline.run_single(idx, frame)
            lv_txt = ENHANCER_LABELS[idx]
        else:
            proc, lv = self._pipeline.run_adaptive(frame)
            lv_txt = f"Adaptive Level {lv}"

        # 성능 & 정보 업데이트
        avg_b = frame.mean()
        latencies = " | ".join([f"{k}: {v:.1f}ms" for k,v in self._pipeline.latencies.items()])
        self._lbl_info.setText(
            f"<b>Source:</b> {avg_b:.0f} lux | <b>Mode:</b> {lv_txt}<br>"
            f"<b>Performance:</b> {latencies}"
        )

        # 디스플레이
        h, w = self.DISP_H // 2, self.DISP_W // 2
        self._lbl_orig.setPixmap(mat_to_qpixmap(frame, w, h))
        self._lbl_proc.setPixmap(mat_to_qpixmap(proc, w, h))
        self._last_display = np.hstack([cv2.resize(frame, (w,h)), cv2.resize(proc, (w,h))])

    def _open_camera(self):
        self._stop(); self._cap = cv2.VideoCapture(0); self._timer.start(33)
    def _open_video(self):
        path, _ = QFileDialog.getOpenFileName(self, "파일 선택", "", "Media (*.mp4 *.avi *.jpg *.png)")
        if path: self._stop(); self._cap = cv2.VideoCapture(path); self._timer.start(33)
    def _stop(self):
        self._timer.stop()
        if self._cap: self._cap.release(); self._cap = None
    def _capture(self):
        if hasattr(self, "_last_display"):
            cv2.imwrite(f"cap_{int(time.time())}.jpg", self._last_display)

    def _save_cfg(self):
        # 파라미터 저장 로직 (생략/유지)
        pass
