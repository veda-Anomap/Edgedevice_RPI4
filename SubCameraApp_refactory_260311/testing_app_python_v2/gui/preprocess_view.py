"""
preprocess_view.py – Tab 1: 이미지 전처리 비교
"""
from __future__ import annotations
import os, sys
import numpy as np
import cv2

from PyQt5.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
                               QLabel, QFileDialog, QScrollArea, QSizePolicy,
                               QProgressBar, QMessageBox, QCheckBox, QGroupBox,
                               QGridLayout)
from PyQt5.QtCore    import Qt, QThread, pyqtSignal
from PyQt5.QtGui     import QPixmap, QImage

from pipeline.enhancer_pipeline import EnhancerPipeline, ENHANCER_LABELS
from core.metrics_calculator    import MetricsCalculator
from log.log_recorder           import LogRecorder


def mat_to_qpixmap(img: np.ndarray, w=0, h=0) -> QPixmap:
    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    qimg = QImage(rgb.data, rgb.shape[1], rgb.shape[0],
                  rgb.strides[0], QImage.Format_RGB888)
    px = QPixmap.fromImage(qimg)
    if w and h:
        px = px.scaled(w, h, Qt.KeepAspectRatio, Qt.SmoothTransformation)
    return px


class BatchWorker(QThread):
    progress  = pyqtSignal(int)
    result    = pyqtSignal(str, list, list)
    finished_ = pyqtSignal()

    def __init__(self, paths: list[str], pipeline: EnhancerPipeline, selected_indices: list[int]):
        super().__init__()
        self._paths    = paths
        self._pipeline = pipeline
        self._selected_indices = selected_indices

    def run(self):
        for src_path in self._paths:
            img = cv2.imread(src_path)
            if img is None: continue
            base = cv2.resize(img, (320, 240))
            results  = self._pipeline.run_all(base, self._selected_indices)
            metrics  = [MetricsCalculator.compute(r, ENHANCER_LABELS[self._selected_indices[i]])
                        for i, r in enumerate(results)]
            cells = []
            for r, m in zip(results, metrics):
                cell = r.copy()
                MetricsCalculator.draw_overlay(cell, m)
                cells.append(cell)
            self.result.emit(src_path, cells, metrics)
        self.finished_.emit()


class PreprocessView(QWidget):
    CELL_W, CELL_H = 320, 240

    def __init__(self, pipeline: EnhancerPipeline, log_dir: str = "."):
        super().__init__()
        self._pipeline = pipeline
        self._log_dir  = log_dir
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)

        btn_bar = QHBoxLayout()
        self._btn_open  = QPushButton("📂 이미지 파일 선택 (복수 가능)")
        self._btn_run   = QPushButton("▶ 분석 실행")
        self._btn_save  = QPushButton("💾 지표 저장")
        self._btn_run.setEnabled(False)
        self._btn_save.setEnabled(False)
        for b in [self._btn_open, self._btn_run, self._btn_save]:
            b.setMinimumHeight(35); btn_bar.addWidget(b)
        layout.addLayout(btn_bar)

        algo_grp = QGroupBox("적용할 알고리즘 선택 (AF 포함)")
        algo_lay = QVBoxLayout(algo_grp)
        chk_btns = QHBoxLayout()
        self._btn_all = QPushButton("모두 선택"); self._btn_none = QPushButton("모두 해제")
        chk_btns.addWidget(self._btn_all); chk_btns.addWidget(self._btn_none)
        algo_lay.addLayout(chk_btns)

        self._checks = []
        chk_grid = QGridLayout()
        for i, label in enumerate(ENHANCER_LABELS):
            cb = QCheckBox(label); cb.setChecked(True)
            self._checks.append(cb)
            chk_grid.addWidget(cb, i // 4, i % 4)
        algo_lay.addLayout(chk_grid)
        layout.addWidget(algo_grp)

        self._progress = QProgressBar(); self._progress.setVisible(False)
        layout.addWidget(self._progress)

        self._src_label = QLabel("이미지를 선택하세요.")
        self._src_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self._src_label)

        self._scroll = QScrollArea(); self._scroll.setWidgetResizable(True)
        self._grid_widget = QWidget(); self._grid_layout = QVBoxLayout(self._grid_widget)
        self._scroll.setWidget(self._grid_widget); layout.addWidget(self._scroll)

        self._btn_open.clicked.connect(self._on_open)
        self._btn_run.clicked.connect(self._on_run)
        self._btn_save.clicked.connect(self._on_save)
        self._btn_all.clicked.connect(lambda: self._set_all_checks(True))
        self._btn_none.clicked.connect(lambda: self._set_all_checks(False))
        self._paths = []; self._results = {}

    def _set_all_checks(self, state):
        for cb in self._checks: cb.setChecked(state)

    def _on_open(self):
        files, _ = QFileDialog.getOpenFileNames(self, "이미지 선택", "", "Images (*.jpg *.png *.bmp)")
        if files:
            self._paths = files
            self._src_label.setText(f"선택: {len(files)}개 파일")
            self._btn_run.setEnabled(True)

    def _on_run(self):
        selected = [i for i, cb in enumerate(self._checks) if cb.isChecked()]
        if not selected: return
        self._progress.setVisible(True); self._progress.setRange(0, len(self._paths))
        self._results.clear()
        for i in reversed(range(self._grid_layout.count())):
            it = self._grid_layout.itemAt(i)
            if it.widget(): it.widget().deleteLater()
        self._worker = BatchWorker(self._paths, self._pipeline, selected)
        self._worker.result.connect(self._on_result)
        self._worker.finished_.connect(self._on_done)
        self._worker.start()

    def _on_result(self, src_path, cells, metrics):
        self._results[src_path] = (cells, metrics)
        self._progress.setValue(self._progress.value() + 1)
        num = len(cells); cols = 4 if num >= 4 else num; rows = (num + cols - 1) // cols
        row_imgs = []
        for r in range(rows):
            row_cells = cells[r*cols : (r+1)*cols]
            while len(row_cells) < cols: row_cells.append(np.zeros_like(cells[0]))
            row_imgs.append(np.hstack(row_cells))
        grid = np.vstack(row_imgs)
        lbl_title = QLabel(f"[{os.path.basename(src_path)}]")
        lbl_title.setStyleSheet("font-weight:bold; margin-top:10px; color:#fff;")
        lbl_title.setAlignment(Qt.AlignCenter); self._grid_layout.addWidget(lbl_title)
        img_lbl = QLabel()
        img_lbl.setPixmap(mat_to_qpixmap(grid, self.CELL_W * cols, self.CELL_H * rows))
        img_lbl.setAlignment(Qt.AlignCenter); self._grid_layout.addWidget(img_lbl)

    def _on_done(self):
        self._progress.setVisible(False); self._btn_save.setEnabled(True)

    def _on_save(self):
        for src_path, (_, m) in self._results.items():
            LogRecorder.save_metrics(src_path, m, self._log_dir)
        QMessageBox.information(self, "완료", "지표 데이터가 저장되었습니다.")
