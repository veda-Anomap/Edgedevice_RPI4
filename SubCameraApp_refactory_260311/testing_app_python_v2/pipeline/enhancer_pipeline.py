import numpy as np
import cv2
import os
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field

# ── 헬퍼 함수 ────────────────────────────────────────────────────────
def guided_filter(img: np.ndarray, guide: np.ndarray, r: int, eps: float) -> np.ndarray:
    """OpenCV ximgproc 우선 사용, 없으면 Python 근사 구현"""
    try:
        from cv2 import ximgproc
        return ximgproc.guidedFilter(guide, img, r, eps)
    except (ImportError, AttributeError):
        I = guide.astype(np.float32)
        p = img.astype(np.float32)
        mean_I  = cv2.boxFilter(I, -1, (r, r))
        mean_p  = cv2.boxFilter(p, -1, (r, r))
        mean_Ip = cv2.boxFilter(I * p, -1, (r, r))
        cov_Ip  = mean_Ip - mean_I * mean_p
        mean_II = cv2.boxFilter(I * I, -1, (r, r))
        var_I   = mean_II - mean_I * mean_I
        a = cov_Ip / (var_I + eps)
        b = mean_p - a * mean_I
        mean_a = cv2.boxFilter(a, -1, (r, r))
        mean_b = cv2.boxFilter(b, -1, (r, r))
        q = mean_a * I + mean_b
        return q

# ── 인터페이스 (SOLID: Strategy Pattern) ───────────────────────────────
class IEnhancer(ABC):
    def __init__(self, name: str):
        self.name = name
        self.latency_ms = 0.0

    @abstractmethod
    def apply(self, img: np.ndarray) -> np.ndarray:
        pass

    def run(self, img: np.ndarray) -> np.ndarray:
        t0 = time.perf_counter()
        res = self.apply(img)
        self.latency_ms = (time.perf_counter() - t0) * 1000.0
        return res

    def update_params(self, params: "EnhancerParams"):
        pass

# ── 인핸서 구현체들 ───────────────────────────────────────────────────

class TonemapEnhancer(IEnhancer):
    def __init__(self, radius=16, eps=0.01, gamma=0.5):
        super().__init__("ToneMapping")
        self.radius = radius
        self.eps = eps
        self.gamma = gamma

    def apply(self, img: np.ndarray) -> np.ndarray:
        f32 = img.astype(np.float32) / 255.0
        base = guided_filter(f32, f32, self.radius, self.eps)
        detail = f32 - base
        base_g = np.power(base, self.gamma)
        res = np.clip((base_g + detail) * 255.0, 0, 255).astype(np.uint8)
        return res

    def update_params(self, p: "EnhancerParams"):
        self.radius = p.tonemap_radius
        self.gamma = p.tonemap_gamma_base

class LowLightEnhancer(IEnhancer):
    def __init__(self, g_dark=0.4, g_normal=0.7):
        super().__init__("LowLight")
        self._build_luts(g_dark, g_normal)
        self._clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))

    def _build_luts(self, g_dark, g_normal):
        idx = np.arange(256, dtype=np.float32)
        self._lut04 = np.clip((idx / 255.0)**g_dark * 255.0, 0, 255).astype(np.uint8)
        self._lut07 = np.clip((idx / 255.0)**g_normal * 255.0, 0, 255).astype(np.uint8)

    def apply(self, img: np.ndarray) -> np.ndarray:
        hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
        h, s, v = cv2.split(hsv)
        m = v.mean()
        v_en = cv2.LUT(v, self._lut04 if m < 30 else self._lut07)
        v_en = self._clahe.apply(v_en)
        res = cv2.merge([h, s, v_en])
        return cv2.cvtColor(res, cv2.COLOR_HSV2BGR)

class RetinexEnhancer(IEnhancer):
    def __init__(self, gf_radius=12, eps=0.01, clahe_limit=1.5, k=0.6):
        super().__init__("Retinex")
        self._r = gf_radius
        self._eps = eps
        self._limit = clahe_limit
        self._k = k
        self._clahe = cv2.createCLAHE(clipLimit=clahe_limit, tileGridSize=(8, 8))

    def update_params(self, p: "EnhancerParams"):
        self._r = p.retinex_gf_radius
        if self._limit != p.retinex_clahe_limit:
            self._limit = p.retinex_clahe_limit
            self._clahe = cv2.createCLAHE(clipLimit=self._limit, tileGridSize=(8, 8))
        self._k = p.retinex_illum_k

    def apply(self, img: np.ndarray) -> np.ndarray:
        f32 = img.astype(np.float32) / 255.0
        illum = guided_filter(f32, f32, self._r, self._eps)
        retinex = f32 / (np.power(illum, self._k) + 1e-6)
        res = np.clip(retinex * 255.0, 0, 255).astype(np.uint8)
        lab = cv2.cvtColor(res, cv2.COLOR_BGR2LAB)
        l, a, b = cv2.split(lab)
        l = self._clahe.apply(l)
        res = cv2.merge([l, a, b])
        return cv2.cvtColor(res, cv2.COLOR_LAB2BGR)

class DetailBoostEnhancer(IEnhancer):
    def __init__(self, w1=1.5, w2=2.0):
        super().__init__("DetailBoost")
        self._w1 = w1
        self._w2 = w2

    def update_params(self, p: "EnhancerParams"):
        self._w1 = p.detail_w1
        self._w2 = p.detail_w2

    def apply(self, img: np.ndarray) -> np.ndarray:
        blur1 = cv2.GaussianBlur(img, (3, 3), 0)
        blur2 = cv2.GaussianBlur(img, (7, 7), 0)
        f32 = img.astype(np.float32)
        sh = f32 + (f32 - blur1.astype(np.float32)) * self._w1 + (f32 - blur2.astype(np.float32)) * self._w2
        return np.clip(sh, 0, 255).astype(np.uint8)

# ── 신규 AF 샤프닝 전략들 ──────────────────────────────────────────

class GuidedSharpening(IEnhancer):
    def __init__(self, strength=2.0):
        super().__init__("Guided(AF)")
        self.strength = strength

    def update_params(self, p: "EnhancerParams"):
        self.strength = p.focus_strength

    def apply(self, img: np.ndarray) -> np.ndarray:
        f32 = img.astype(np.float32)
        guided = guided_filter(img, img, 4, 0.01 * 255 * 255).astype(np.float32)
        detail = f32 - guided
        sharp = f32 + detail * self.strength
        min_v = cv2.erode(img, np.ones((3,3), np.uint8)).astype(np.float32)
        max_v = cv2.dilate(img, np.ones((3,3), np.uint8)).astype(np.float32)
        res = np.clip(sharp, min_v, max_v)
        return res.astype(np.uint8)

class GradientRatioSharpening(IEnhancer):
    def __init__(self, strength=2.0):
        super().__init__("GradRatio(AF)")
        self.strength = strength

    def update_params(self, p: "EnhancerParams"):
        self.strength = p.focus_strength

    def apply(self, img: np.ndarray) -> np.ndarray:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32) / 255.0
        gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0)
        gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1)
        g_orig = cv2.magnitude(gx, gy)
        reblurred = cv2.GaussianBlur(gray, (5,5), 1.0)
        rbx = cv2.Sobel(reblurred, cv2.CV_32F, 1, 0)
        rby = cv2.Sobel(reblurred, cv2.CV_32F, 0, 1)
        g_reblur = cv2.magnitude(rbx, rby)
        focus_map = np.exp(-(g_orig / (g_reblur + 0.001)))
        focus_map = cv2.blur(focus_map, (15,15))
        auto_s = self.strength * focus_map.mean() * 5.0
        lap = cv2.Laplacian(img, cv2.CV_16S, ksize=3).astype(np.float32)
        f_map_3ch = cv2.merge([focus_map]*3)
        sharp = img.astype(np.float32) - (lap * f_map_3ch * auto_s)
        min_v = cv2.erode(img, np.ones((3,3), np.uint8)).astype(np.float32)
        max_v = cv2.dilate(img, np.ones((3,3), np.uint8)).astype(np.float32)
        res = np.clip(sharp, min_v, max_v)
        return res.astype(np.uint8)

class DigitalELPSharpening(IEnhancer):
    def __init__(self, strength=2.0):
        super().__init__("ELP(AF)")
        self.strength = strength
        self.last_gray = None

    def update_params(self, p: "EnhancerParams"):
        self.strength = p.focus_strength

    def apply(self, img: np.ndarray) -> np.ndarray:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        gray_f = cv2.GaussianBlur(gray, (3,3), 0.5).astype(np.float32)
        if self.last_gray is None:
            self.last_gray = gray_f
            return img
        lap = cv2.Laplacian(gray_f, cv2.CV_32F, ksize=3)
        diff = cv2.absdiff(gray_f, self.last_gray)
        _, ev_w = cv2.threshold(diff, 5.0, 1.0, cv2.THRESH_TOZERO)
        ev_w = cv2.blur(ev_w / 20.0, (9,9))
        ev_w_3ch = cv2.merge([np.clip(ev_w, 0, 1.0)]*3)
        lap_3ch = cv2.merge([lap]*3)
        sharp = img.astype(np.float32) - (lap_3ch * ev_w_3ch * self.strength * 2.0)
        min_v = cv2.erode(img, np.ones((3,3), np.uint8)).astype(np.float32)
        max_v = cv2.dilate(img, np.ones((3,3), np.uint8)).astype(np.float32)
        res = np.clip(sharp, min_v, max_v)
        self.last_gray = gray_f
        return res.astype(np.uint8)

# ── 파라미터 컨테이너 ──────────────────────────────────────────────────
@dataclass
class EnhancerParams:
    tonemap_radius: int = 16
    tonemap_gamma_base: float = 0.5
    retinex_gf_radius: int = 12
    retinex_clahe_limit: float = 1.5
    retinex_illum_k: float = 0.6
    detail_w1: float = 1.5
    detail_w2: float = 2.0
    adaptive_bright_thresh: int = 185
    adaptive_normal_upper: int = 175
    adaptive_normal_lower: int = 105
    adaptive_dim_upper: int = 95
    adaptive_dim_lower: int = 75
    adaptive_extreme_thresh: int = 65
    focus_strength: float = 2.0
    manual_lux_override: bool = False
    manual_lux_value: int = 150

ENHANCER_LABELS = [
    "0.Original(None)",
    "1.ToneMapping",
    "2.LowLight",
    "3.Retinex",
    "4.DetailBoost",
    "5.Tone+Detail",
    "6.Retinex+Detail",
    "7.Adaptive(Dark-Only)",
    "8.Guided(AF)",
    "9.GradRatio(AF)",
    "10.ELP(AF)"
]

# ── 메인 파이프라인 ───────────────────────────────────────────────────
class EnhancerPipeline:
    def __init__(self):
        self._params = EnhancerParams()
        self._enhancers = {
            1: TonemapEnhancer(),
            2: LowLightEnhancer(),
            3: RetinexEnhancer(),
            4: DetailBoostEnhancer(),
            8: GuidedSharpening(),
            9: GradientRatioSharpening(),
            10: DigitalELPSharpening()
        }
        self.latencies = {}

    def update_params(self, p: EnhancerParams):
        self._params = p
        for e in self._enhancers.values():
            e.update_params(p)

    def run_single(self, idx: int, img: np.ndarray) -> np.ndarray:
        if idx == 0: return img.copy()
        if idx in self._enhancers:
            res = self._enhancers[idx].run(img)
            self.latencies[self._enhancers[idx].name] = self._enhancers[idx].latency_ms
            return res
        # 복합 필터 (5, 6, 7 등)
        if idx == 5: # Tone + Detail
            t = self._enhancers[1].run(img)
            d = self._enhancers[4].run(t)
            self.latencies["Tone+Detail"] = self._enhancers[1].latency_ms + self._enhancers[4].latency_ms
            return d
        if idx == 6: # Retinex + Detail
            r = self._enhancers[3].run(img)
            d = self._enhancers[4].run(r)
            self.latencies["Retinex+Detail"] = self._enhancers[3].latency_ms + self._enhancers[4].latency_ms
            return d
        if idx == 7:
            m = img.mean()
            if m < 80: return self._enhancers[2].run(img)
            return img.copy()
        return img.copy()

    def run_all(self, img: np.ndarray, selected_indices: list[int] = None) -> list[np.ndarray]:
        if selected_indices is None:
            selected_indices = list(range(len(ENHANCER_LABELS)))
        return [self.run_single(idx, img) for idx in selected_indices]

    def run_adaptive(self, img: np.ndarray) -> tuple[np.ndarray, int]:
        p = self._params
        m = p.manual_lux_value if p.manual_lux_override else img.mean()
        
        t0 = time.perf_counter()
        if m < p.adaptive_extreme_thresh:
            res, lv = self._enhancers[2].run(img), 0
        elif m < p.adaptive_dim_lower:
            r = self._enhancers[3].run(img)
            res, lv = self._enhancers[4].run(r), 1
        elif m < p.adaptive_normal_lower:
            res, lv = self._enhancers[1].run(img), 2
        elif m < p.adaptive_normal_upper:
            prev_g = self._enhancers[1].gamma
            self._enhancers[1].gamma = 0.7
            res = self._enhancers[1].run(img)
            self._enhancers[1].gamma = prev_g
            lv = 3
        elif m < p.adaptive_bright_thresh:
            res, lv = img.copy(), 4
        else:
            res, lv = img.copy(), 5
            
        self.latencies["AdaptiveHybrid"] = (time.perf_counter() - t0) * 1000.0
        return res, lv
