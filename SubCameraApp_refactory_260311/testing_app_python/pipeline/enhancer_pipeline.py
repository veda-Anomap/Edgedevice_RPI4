import numpy as np
import cv2
import os
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field

# ── 헬퍼 함수 ────────────────────────────────────────────────────────
def guided_filter(img: np.ndarray, guide: np.ndarray, r: int, eps: float) -> np.ndarray:
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
        return mean_a * I + mean_b

# ── 인터페이스 (SOLID: Strategy Pattern) ───────────────────────────────
class IEnhancer(ABC):
    def __init__(self, name: str):
        self.name = name
        self.latency_ms = 0.0

    @abstractmethod
    def apply(self, img: np.ndarray) -> np.ndarray: pass

    def run(self, img: np.ndarray) -> np.ndarray:
        t0 = time.perf_counter()
        res = self.apply(img)
        self.latency_ms = (time.perf_counter() - t0) * 1000.0
        return res

    def update_params(self, params: "EnhancerParams"): pass

# ── 인핸서 구현체들 (C++ baseline 기준) ───────────────────────────────────

class RetinexEnhancer(IEnhancer):
    def __init__(self, radius=12, eps=0.01, clahe_limit=1.5, k=40.0):
        super().__init__("RETINEX")
        self.r = radius; self.eps = eps; self.limit = clahe_limit; self.k = k
        self.clahe = cv2.createCLAHE(clipLimit=clahe_limit, tileGridSize=(8, 8))

    def update_params(self, p: "EnhancerParams"):
        self.r = p.retinex_gf_radius; self.limit = p.retinex_clahe_limit; self.k = p.retinex_illum_k
        self.clahe = cv2.createCLAHE(clipLimit=self.limit, tileGridSize=(8, 8))

    def apply(self, img: np.ndarray) -> np.ndarray:
        hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
        h, s, v = cv2.split(hsv)
        vf = v.astype(np.float32)
        illum = guided_filter(vf, vf, self.r, self.eps * 255 * 255)
        reflect = np.divide(vf, illum + 1.0) * 255.0
        reflect = np.clip(reflect, 0, 255).astype(np.uint8)
        reflect = cv2.bilateralFilter(reflect, 5, 50, 50)
        reflect = self.clahe.apply(reflect)
        # 2nd GF
        ref_f = reflect.astype(np.float32) / 255.0
        ref_f = guided_filter(ref_f, ref_f, 3, 0.01)
        reflect = (ref_f * 255.0).astype(np.uint8)
        # Final eV
        eL = illum * (255.0 + self.k) / (illum + self.k)
        eV = np.clip(eL * reflect.astype(np.float32) / 255.0, 0, 255).astype(np.uint8)
        res = cv2.cvtColor(cv2.merge([h, s, eV]), cv2.COLOR_HSV2BGR)
        blur = cv2.GaussianBlur(res, (0, 0), 2); return cv2.addWeighted(res, 1.4, blur, -0.4, 0)

class LowLightAdvancedEnhancer(IEnhancer):
    def __init__(self):
        super().__init__("YUV ADV")
        self.clahe = cv2.createCLAHE(clipLimit=2.5, tileGridSize=(8, 8))
    def apply(self, img: np.ndarray) -> np.ndarray:
        yuv = cv2.cvtColor(img, cv2.COLOR_BGR2YUV)
        y, u, v = cv2.split(yuv)
        gamma = 0.5 if y.mean() < 50 else 0.8
        lut = np.clip((np.arange(256)/255.0)**gamma * 255.0, 0, 255).astype(np.uint8)
        y = cv2.LUT(y, lut); y = self.clahe.apply(y)
        u = cv2.medianBlur(u, 5); v = cv2.medianBlur(v, 5)
        res = cv2.cvtColor(cv2.merge([y, u, v]), cv2.COLOR_YUV2BGR)
        blur = cv2.GaussianBlur(res, (0, 0), 3); return cv2.addWeighted(res, 1.3, blur, -0.3, 0)

class WWGIFEnhancer(IEnhancer):
    def __init__(self, k=30.0):
        super().__init__("WWGIF")
        self.k = k
    def _wwgif(self, I, p, r, eps):
        mI = cv2.boxFilter(I,-1,(2*r+1,2*r+1)); mp = cv2.boxFilter(p,-1,(2*r+1,2*r+1)); mIp = cv2.boxFilter(I*p,-1,(2*r+1,2*r+1))
        varI = cv2.boxFilter(I*I,-1,(2*r+1,2*r+1)) - mI*mI
        gx = cv2.Sobel(I, cv2.CV_32F, 1, 0); gy = cv2.Sobel(I, cv2.CV_32F, 0, 1)
        gamma = 1.0 / (1.0 + np.exp(-cv2.magnitude(gx, gy)))
        a = (mIp - mI*mp)/(varI + eps)
        ma = cv2.boxFilter(a,-1,(2*r+1,2*r+1)); mb = cv2.boxFilter(mp - a*mI,-1,(2*r+1,2*r+1))
        return ma*I + mb
    def apply(self, img: np.ndarray) -> np.ndarray:
        hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV); h, s, v = cv2.split(hsv)
        vf = v.astype(np.float32)/255.0; L = np.zeros_like(vf)
        for r in [15, 30, 80]: L += self._wwgif(vf, vf, r, 0.04)
        L /= 3.0; R = np.clip(cv2.createCLAHE(2.0,(8,8)).apply(np.clip((vf/(L+0.001))*255.0,0,255).astype(np.uint8)).astype(np.float32)/255.0,0,1)
        Rf = self._wwgif(R, R, 5, 0.01); Lf = L*255.0; Lm = L.mean()*255.0
        eV = np.clip(Lf * (255.0+self.k)/(np.maximum(Lf, Lm)+self.k) * Rf, 0, 255).astype(np.uint8)
        res = cv2.cvtColor(cv2.merge([h, s, eV]), cv2.COLOR_HSV2BGR)
        blur = cv2.GaussianBlur(res, (0, 0), 2); return cv2.addWeighted(res, 1.25, blur, -0.25, 0)

class TonemapEnhancer(IEnhancer):
    def __init__(self, r=16, g=0.5):
        super().__init__("TONEMAP")
        self.r = r; self.g = g
        self.eps = 0.01
    def update_params(self, p: "EnhancerParams"):
        self.r = p.tonemap_radius; self.g = p.tonemap_gamma_base
    def apply(self, img: np.ndarray) -> np.ndarray:
        b, g, r = cv2.split(img); v = cv2.max(cv2.max(b, g), r).astype(np.float32)/255.0
        illum = guided_filter(v, v, self.r, getattr(self, 'eps', 0.01))
        # BUG FIX: 사용자가 설정한 gamma_base(self.g)를 반영함. 기본값은 0.5
        gamma = np.power(self.g, (0.5 - illum.mean()) / 0.5)
        lut_i = np.power(illum, gamma); res = []
        for c in [b, g, r]:
            cf = c.astype(np.float32)/255.0
            res.append(np.clip((cf/(illum+0.001))*lut_i*255.0, 0, 255).astype(np.uint8))
        return cv2.merge(res)

# class DetailBoostEnhancer(IEnhancer):
#     def __init__(self, w1=1.5, w2=2.0):
#         super().__init__("DETAIL")
#         self.w1 = w1; self.w2 = w2
#     def update_params(self, p: "EnhancerParams"):
#         self.w1 = p.detail_w1; self.w2 = p.detail_w2
#     def apply(self, img: np.ndarray) -> np.ndarray:
#         f32 = img.astype(np.float32); b1 = cv2.GaussianBlur(f32, (3, 3), 1.0); b2 = cv2.GaussianBlur(f32, (7, 7), 2.0)
#         res = f32 + (f32 - b1) * self.w1 + (b1 - b2) * self.w2
#         return np.clip(res, 0, 255).astype(np.uint8)

class DetailBoostEnhancer(IEnhancer):
    def __init__(self, w1=1.5, w2=2.0):
        super().__init__("DETAIL")
        self.w1 = w1; self.w2 = w2

    def update_params(self, p: "EnhancerParams"):
        self.w1 = p.detail_w1; self.w2 = p.detail_w2

    def apply(self, img: np.ndarray) -> np.ndarray:
        
        # 1. YUV 색공간 변환 (색상 노이즈 분리를 위해 필수)
        yuv = cv2.cvtColor(img, cv2.COLOR_BGR2YUV)
        y, u, v = cv2.split(yuv)
        yf = y.astype(np.float32)

        # 최적화 1: GaussianBlur(7,7)을 boxFilter(5,5)로 대체
        # boxFilter는 커널 크기에 상관없이 연산량이 일정함 (O(1) complexity per pixel)
        # 3x3 가우시안은 엣지 보존을 위해 유지하되, boxFilter로 대체 가능
        b1 = cv2.GaussianBlur(yf, (3, 3), 1.0)
        b2 = cv2.boxFilter(yf, -1, (5, 5))
        
        # 최적화 2: 연산식 통합 (Memory Read/Write 최소화)
        # 기존: f32 + (f32 - b1) * w1 + (b1 - b2) * w2
        # 정리: f32 * (1 + w1) - b1 * (w1 - w2) - b2 * w2
        # 이렇게 정리하면 중간 결과물(diff) 생성 없이 한 번의 행렬 연산으로 끝남
        y_res = yf * (1.0 + self.w1) - b1 * (self.w1 - self.w2) - b2 * self.w2
        y_final = np.clip(y_res, 0, 255).astype(np.uint8)

        # 3. [컬러 노이즈 해결] UV 채널 노이즈 제거 (Chroma Denoise)
        # 3x3 박스 필터로 색상 채널을 살짝 뭉개 무지개 노이즈를 지웁니다. 
        # (medianBlur보다 훨씬 빠르면서 효과적입니다)
        u_final = cv2.boxFilter(u, -1, (3, 3))
        v_final = cv2.boxFilter(v, -1, (3, 3))

        # 4. 재결합 (Merge)
        return cv2.cvtColor(cv2.merge([y_final, u_final, v_final]), cv2.COLOR_YUV2BGR)

class UltimateBalancedV5Enhancer(IEnhancer):
    def __init__(self):
        super().__init__("BALANCED_V5")
    def apply(self, img: np.ndarray) -> np.ndarray:
        lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB); l, a, b = cv2.split(lab); lf = l.astype(np.float32)/255.0
        illum = guided_filter(lf, lf, 16, 0.01)
        enhanced_l = 0.7 * np.power(lf, 0.4) + 0.3 / (1.0 + np.exp(-10.0 * (lf - 0.35)))
        detail = lf - illum; _, mask = cv2.threshold(np.abs(detail), 0.01, 1.0, cv2.THRESH_BINARY)
        enhanced_l += detail * mask * 2.2; gain = 1.0 + np.log1p((enhanced_l/(lf+0.01))*0.5)
        res_lab = [np.clip(enhanced_l * 255.0, 0, 255).astype(np.uint8)]
        for c in [a, b]: res_lab.append(np.clip(128.0 + (c.astype(np.float32)-128.0)*gain, 0, 255).astype(np.uint8))
        return cv2.cvtColor(cv2.merge(res_lab), cv2.COLOR_LAB2BGR)

class CleanSharpEnhancer(IEnhancer):
    def __init__(self):
        super().__init__("CleanSharp")
        self.s = 1.2
    def update_params(self, p: "EnhancerParams"):
        self.s = p.focus_strength
    def apply(self, img: np.ndarray) -> np.ndarray:
        yuv = cv2.cvtColor(img, cv2.COLOR_BGR2YUV); y, u, v = cv2.split(yuv)
        u = cv2.medianBlur(u, 3); v = cv2.medianBlur(v, 3); yf = y.astype(np.float32)/255.0
        illum = guided_filter(yf, yf, 12, 0.01)
        ey = np.power(yf, np.power(0.7, 0.5-illum.mean())) + (yf - illum)*self.s
        res = cv2.cvtColor(cv2.merge([np.clip(ey*255.0,0,255).astype(np.uint8), u, v]), cv2.COLOR_YUV2BGR)
        blur = cv2.GaussianBlur(res, (0,0), 2); return cv2.addWeighted(res, 1.2, blur, -0.2, 0)

# ── AF 전략 (Video Only) ────────────────────────────────────────────────

class AFGuided(IEnhancer):
    def __init__(self, s=2.0): super().__init__("Guided(AF)"); self.s = s
    def update_params(self, p: "EnhancerParams"): self.s = p.focus_strength
    def apply(self, img: np.ndarray) -> np.ndarray:
        f32 = img.astype(np.float32); guided = guided_filter(img, img, 4, 0.01*255*255).astype(np.float32)
        sharp = f32 + (f32 - guided) * self.s
        mn = cv2.erode(img, np.ones((3,3))); mx = cv2.dilate(img, np.ones((3,3)))
        return np.clip(sharp, mn.astype(np.float32), mx.astype(np.float32)).astype(np.uint8)

class AFGradRatio(IEnhancer):
    def __init__(self, s=2.0): super().__init__("GradRatio(AF)"); self.s = s
    def update_params(self, p: "EnhancerParams"): self.s = p.focus_strength
    def apply(self, img: np.ndarray) -> np.ndarray:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)/255.0
        g = cv2.magnitude(cv2.Sobel(gray, cv2.CV_32F,1,0), cv2.Sobel(gray, cv2.CV_32F,0,1))
        rb = cv2.GaussianBlur(gray, (5,5), 1.0); grb = cv2.magnitude(cv2.Sobel(rb, cv2.CV_32F,1,0), cv2.Sobel(rb, cv2.CV_32F,0,1))
        f_map = cv2.blur(np.exp(-(g/(grb+0.001))), (15,15))
        lap = cv2.Laplacian(img, cv2.CV_16S, ksize=3).astype(np.float32)
        sharp = img.astype(np.float32) - (lap * cv2.merge([f_map]*3) * self.s * f_map.mean() * 5.0)
        mn = cv2.erode(img, np.ones((3,3))); mx = cv2.dilate(img, np.ones((3,3)))
        return np.clip(sharp, mn.astype(np.float32), mx.astype(np.float32)).astype(np.uint8)

class AFELP(IEnhancer):
    def __init__(self, s=2.0): super().__init__("ELP(AF)"); self.s = s; self.last = None
    def update_params(self, p: "EnhancerParams"): self.s = p.focus_strength
    def apply(self, img: np.ndarray) -> np.ndarray:
        curr = cv2.GaussianBlur(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), (3,3), 0.5).astype(np.float32)
        if self.last is None: self.last = curr; return img
        diff = cv2.absdiff(curr, self.last); _, w = cv2.threshold(diff, 5.0, 1.0, cv2.THRESH_TOZERO)
        w = cv2.blur(w / 20.0, (9,9)); lap = cv2.merge([cv2.Laplacian(curr, cv2.CV_32F, ksize=3)]*3)
        sharp = img.astype(np.float32) - (lap * cv2.merge([w]*3) * self.s * 2.0)
        mn = cv2.erode(img, np.ones((3,3))); mx = cv2.dilate(img, np.ones((3,3)))
        self.last = curr; return np.clip(sharp, mn.astype(np.float32), mx.astype(np.float32)).astype(np.uint8)

@dataclass
class EnhancerParams:
    tonemap_radius: int = 16
    tonemap_gamma_base: float = 0.5
    retinex_gf_radius: int = 12
    retinex_clahe_limit: float = 1.5
    retinex_illum_k: float = 40.0
    detail_w1: float = 1.5
    detail_w2: float = 2.0
    focus_strength: float = 2.0
    manual_lux_override: bool = False
    manual_lux_value: int = 150

ENHANCER_LABELS = [
    "1. RETINEX", "2. YUV ADV", "3. WWGIF", "4. TONEMAP", "5. DETAIL",
    "6. HYBRID", "7. BALANCED_V5", "8. CleanSharp", "9. RAW ORIGIN",
    "Guided(AF)", "GradRatio(AF)", "ELP(AF)"
]

class EnhancerPipeline:
    def __init__(self):
        self._p = EnhancerParams()
        self._enhancers = {
            0: RetinexEnhancer(), 1: LowLightAdvancedEnhancer(), 2: WWGIFEnhancer(),
            3: TonemapEnhancer(), 4: DetailBoostEnhancer(), 6: UltimateBalancedV5Enhancer(),
            7: CleanSharpEnhancer(), 9: AFGuided(), 10: AFGradRatio(), 11: AFELP()
        }
        self.latencies = {}

    def update_params(self, p: EnhancerParams):
        self._p = p
        for e in self._enhancers.values(): e.update_params(p)

    def run_single(self, idx: int, img: np.ndarray) -> np.ndarray:
        if idx == 8: return img.copy() # RAW ORIGIN
        if idx == 5: # HYBRID (TONEMAP + DETAIL)
            t = self._enhancers[3].run(img)
            d = self._enhancers[4].run(t)
            self.latencies["HYBRID"] = self._enhancers[3].latency_ms + self._enhancers[4].latency_ms
            return d
        if idx in self._enhancers:
            res = self._enhancers[idx].run(img)
            self.latencies[self._enhancers[idx].name] = self._enhancers[idx].latency_ms
            return res
        return img.copy()

    def run_all(self, img: np.ndarray, selected_indices: list[int] = None) -> list[tuple[np.ndarray, float]]:
        if selected_indices is None: selected_indices = list(range(0, 9))
        results = []
        for idx in selected_indices:
            res = self.run_single(idx, img)
            name = ENHANCER_LABELS[idx]
            # [추가] Hybrid 전용 지연시간 처리
            lat = self.latencies.get("HYBRID", 0) if idx == 5 else self.latencies.get(name, 0)
            results.append((res, lat))
        return results

    # def run_adaptive(self, img: np.ndarray) -> tuple[np.ndarray, str, float]:
    #     img_mean = img.mean()
    #     t0 = time.perf_counter()
        
    #     # 1. 하이브리드 암도(Darkness) 산출 (0~255, 숫자가 클수록 어두움)
    #     if self._p.manual_lux_override:
    #         sensor_lux = self._p.manual_lux_value # 0(밝음) -> 255(어두움)
    #         hybrid_darkness = (sensor_lux * 0.7) + (255.0 - img_mean) * 0.3
    #     else:
    #         # 센서 오버라이드가 꺼져있다면 이미지만으로 darkness 유추 (센서값이 없으므로 100% 의존)
    #         hybrid_darkness = 255.0 - img_mean

    #     processed = img.copy()

    #     # 2. 전 구간 연속형(Continuous) 파이프라인
    #     # t_total: 0.0 (가장 밝음) ~ 1.0 (가장 어두움)
    #     t_total = max(0.0, min(1.0, hybrid_darkness / 255.0))

    #     # [파라미터 선형 보간 설정]
    #     # (1) 노이즈 전처리 (커널 크기: 밝을때 1 -> 어두울때 5)
    #     # t_total 에 따라 1, 3, 5 로 부드럽게 증가
    #     box_k = int(1 + 4 * t_total)
    #     if box_k % 2 == 0: box_k += 1 # 홀수 유지

    #     # (2) Tonemap 강도 (Gamma/Eps)
    #     # UI에서 설정한 값을 베이스로 쓰지 않고 코드에서 구간을 통제하는 예시
    #     # 밝을 때는 원본 유지(gamma 1.0), 어두울 때는 펌핑(gamma 0.7)
    #     gamma = 1.0 - (1.0 - 0.70) * t_total
    #     eps   = 0.01 + (0.10 - 0.01) * t_total

    #     # (3) Detail Boost 가중치
    #     # 밝을 때 (w1=1.5, w2=0.7) -> 어두울 때 (w1=2.5, w2=1.0)
    #     curr_w1 = 1.5 + (2.5 - 1.5) * t_total
    #     curr_w2 = 0.7 + (1.0 - 0.7) * t_total

    #     # --- 순차 적용 로직 ---
    #     # A. 전처리 (Box Filter)
    #     # if box_k > 1:
    #     #     processed = cv2.boxFilter(processed, -1, (box_k, box_k))

    #     # B. Tonemap 적용
    #     if gamma < 0.98: # 거의 1.0이면 연산 낭비이므로 약하게 배제
    #         self._p.tonemap_gamma_base = gamma
    #         self._enhancers[3].update_params(self._p)
    #         self._enhancers[3].eps = eps
    #         processed = self._enhancers[3].run(processed)
    #         self._enhancers[3].eps = 0.01 # Reset

    #     # C. Detail Boost 적용
    #     orig_w1, orig_w2 = self._p.detail_w1, self._p.detail_w2
    #     self._p.detail_w1, self._p.detail_w2 = curr_w1, curr_w2
    #     self._enhancers[4].update_params(self._p)
    #     res = self._enhancers[4].run(processed)
        
    #     # UI 롤백
    #     self._p.detail_w1, self._p.detail_w2 = orig_w1, orig_w2
    #     self._enhancers[4].update_params(self._p)
        
    #     lvl = f"k:{box_k} g:{gamma:.2f} w1:{curr_w1:.1f}"
                
    #     self.latencies["Adaptive"] = (time.perf_counter()-t0)*1000.0
    #     return res, f"Hybrid {lvl}", hybrid_darkness

    def run_adaptive(self, img: np.ndarray) -> tuple[np.ndarray, str, float]:
        img_mean = img.mean()
        t0 = time.perf_counter()
        
        # 1. 하이브리드 암도 계산 (기존 유지)
        sensor_lux = self._p.manual_lux_value if self._p.manual_lux_override else (255.0 - img_mean)
        hybrid_darkness = (sensor_lux * 0.7) + (255.0 - img_mean) * 0.3
        t_total = max(0.0, min(1.0, hybrid_darkness / 255.0))

        # --- 전문가 최적화 구간 ---
        
        # A. 컬러 노이즈 원천 차단 (YUV 분리 처리)
        # 어두울수록 UV 채널 블러 강도를 높임 (3, 5, 7...)
        yuv = cv2.cvtColor(img, cv2.COLOR_BGR2YUV)
        y, u, v = cv2.split(yuv)
        
        if t_total > 0.4: # 어두워지기 시작할 때
            uv_k = int(3 + 4 * (t_total - 0.4)) # 0.4~1.0 구간에서 3~7 사이즈
            if uv_k % 2 == 0: uv_k += 1
            u = cv2.medianBlur(u, uv_k)
            v = cv2.medianBlur(v, uv_k)
        
        # B. 휘도(Y) 채널 전처리 (Box Filter는 Y에만 적용하여 엣지 보존)
        y_processed = y
        if t_total > 0.6: # 정말 어두울 때만 밝기 채널 노이즈 제거
            y_processed = cv2.boxFilter(y, -1, (3, 3))
        
        processed_img = cv2.cvtColor(cv2.merge([y_processed, u, v]), cv2.COLOR_YUV2BGR)

        # C. 가변 엣지 가중치 (밝을 때 엣지를 더 날카롭게)
        # w1: 밝을 때(1.8) -> 어두울 때(2.5) / 엣지가 흐려보이지 않게 시작점 상향
        curr_w1 = 1.8 + (2.5 - 1.8) * t_total 
        
        # D. Tonemap 파라미터 최적화
        gamma = 1.0 - (1.0 - 0.65) * t_total
        # eps를 너무 키우면 엣지가 뭉개지므로 상한선을 0.05로 조절
        eps = 0.01 + (0.05 - 0.01) * t_total 

        # --- 실제 필터 적용 ---
        # Tonemap (TonemapEnhancer 내부 로직이 BGR 기반이라면 위에서 합친 processed_img 사용)
        if gamma < 0.95:
            self._p.tonemap_gamma_base = gamma
            self._enhancers[3].update_params(self._p)
            self._enhancers[3].eps = eps
            processed_img = self._enhancers[3].apply(processed_img)

        # Detail Boost (최종 샤프닝)
        self._p.detail_w1 = curr_w1
        self._enhancers[4].update_params(self._p)
        res = self._enhancers[4].apply(processed_img)

        self.latencies["Adaptive"] = (time.perf_counter()-t0)*1000.0
        return res, f"Hybrid Lvl", hybrid_darkness