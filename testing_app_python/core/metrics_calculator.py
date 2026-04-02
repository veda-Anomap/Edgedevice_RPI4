"""
metrics_calculator.py – 이미지 품질 5종 지표 계산 (260313_compare.cpp 포트)
"""
import numpy as np
import cv2
from dataclasses import dataclass


@dataclass
class ImageMetrics:
    name: str = ""
    sharpness: float = 0.0      # Laplacian Variance
    colorfulness: float = 0.0   # Hasler-Suesstrunk
    brightness: float = 0.0     # 평균 밝기
    tenengrad: float = 0.0      # Sobel 기반 에지 강도
    noise_estimate: float = 0.0 # Brenner Gradient
    latency: float = 0.0        # 처리 시간 (ms)


class MetricsCalculator:
    """260313_compare.cpp 의 5종 지표를 Python으로 재현"""

    @staticmethod
    def compute(img: np.ndarray, name: str = "", latency: float = 0.0) -> ImageMetrics:
        m = ImageMetrics(name=name, latency=latency)
        m.sharpness     = MetricsCalculator.get_sharpness(img)
        m.colorfulness  = MetricsCalculator.get_colorfulness(img)
        m.brightness    = MetricsCalculator.get_brightness(img)
        m.tenengrad     = MetricsCalculator.get_tenengrad(img)
        m.noise_estimate= MetricsCalculator.get_noise_estimate(img)
        return m

    @staticmethod
    def get_sharpness(img: np.ndarray) -> float:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) if img.ndim == 3 else img
        lap = cv2.Laplacian(gray, cv2.CV_64F)
        return float(lap.var())

    @staticmethod
    def get_colorfulness(img: np.ndarray) -> float:
        if img.ndim != 3:
            return 0.0
        b, g, r = cv2.split(img.astype(np.float32))
        rg = np.abs(r - g)
        yb = np.abs(0.5 * (r + g) - b)
        std_rg, mean_rg = rg.std(), rg.mean()
        std_yb, mean_yb = yb.std(), yb.mean()
        std_root  = np.sqrt(std_rg**2 + std_yb**2)
        mean_root = np.sqrt(mean_rg**2 + mean_yb**2)
        return float(std_root + 0.3 * mean_root)

    @staticmethod
    def get_brightness(img: np.ndarray) -> float:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) if img.ndim == 3 else img
        return float(gray.mean())

    @staticmethod
    def get_tenengrad(img: np.ndarray) -> float:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) if img.ndim == 3 else img
        gx = cv2.Sobel(gray, cv2.CV_64F, 1, 0)
        gy = cv2.Sobel(gray, cv2.CV_64F, 0, 1)
        mag = np.sqrt(gx**2 + gy**2)
        return float((mag**2).sum() / (img.shape[0] * img.shape[1]))

    @staticmethod
    def get_noise_estimate(img: np.ndarray) -> float:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) if img.ndim == 3 else img
        diff = np.abs(gray[:, 1:].astype(np.float32) - gray[:, :-1].astype(np.float32))
        return float(diff.mean())

    @staticmethod
    def draw_overlay(cell: np.ndarray, m: ImageMetrics) -> np.ndarray:
        """지표를 셀 이미지에 오버레이 (in-place)"""
        h = cell.shape[0]
        # 제목
        cv2.putText(cell, m.name, (10, 25), cv2.FONT_HERSHEY_SIMPLEX,
                    0.55, (0, 0, 0), 3)
        cv2.putText(cell, m.name, (10, 25), cv2.FONT_HERSHEY_SIMPLEX,
                    0.55, (0, 255, 255), 1)
        # 하단 지표
        line1 = f"T:{m.tenengrad:.0f} Ns:{m.noise_estimate:.1f}"
        line2 = f"S:{m.sharpness:.0f} C:{m.colorfulness:.1f} B:{m.brightness:.0f}"
        cv2.putText(cell, line1, (8, h - 40), cv2.FONT_HERSHEY_SIMPLEX,
                    0.42, (0, 0, 0), 2)
        cv2.putText(cell, line1, (8, h - 40), cv2.FONT_HERSHEY_SIMPLEX,
                    0.42, (255, 255, 255), 1)
        cv2.putText(cell, line2, (8, h - 22), cv2.FONT_HERSHEY_SIMPLEX,
                    0.40, (0, 255, 255), 1)
        return cell
