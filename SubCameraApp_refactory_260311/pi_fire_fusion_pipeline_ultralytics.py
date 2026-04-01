import cv2
import numpy as np
import os
import time
import threading
from collections import deque
import urllib.parse

# YOLO 라이브러리 핸들링
try:
    from ultralytics import YOLO
    YOLO_AVAILABLE = True
except ImportError:
    YOLO_AVAILABLE = False

os.environ["DISPLAY"] = ":0"
os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|max_delay;500000"

# --- 1. VideoStream (원래의 안정적인 로직 유지) ---
class VideoStream:
    def __init__(self, url, name):
        self.url = url
        self.name = name
        self.cap = cv2.VideoCapture(self.url, cv2.CAP_FFMPEG)
        self.Q = deque(maxlen=1)
        self.stopped = False
        self.retries = 0

    def start(self):
        self.thread = threading.Thread(target=self.update, daemon=True)
        self.thread.start()
        return self

    def update(self):
        while not self.stopped:
            if not self.cap.isOpened():
                self.reconnect()
                continue
            success, frame = self.cap.read()
            if success:
                self.Q.append(frame)
                self.retries = 0
            else:
                self.reconnect()

    def reconnect(self):
        self.Q.clear()
        wait_time = min(60, 2 ** (self.retries + 1))
        print(f"[!] {self.name} Connection lost. Retrying...")
        time.sleep(wait_time)
        self.cap.release()
        self.cap.open(self.url, cv2.CAP_FFMPEG)
        self.retries += 1

    def read(self):
        return self.Q[-1] if len(self.Q) > 0 else None

    def stop(self):
        self.stopped = True
        self.cap.release()

# --- 2. 정합 엔진 (원래의 Canny Edge 방식으로 완전 복구) ---
class PrecisionAligner:
    def __init__(self, target_size):
        # 복잡한 H_init 대신 단위 행렬로 시작
        self.warp_matrix = np.eye(2, 3, dtype=np.float32)
        self.is_processing = False
        self.lock = threading.Lock()
        self.current_cc = 0.0
        self.target_size = target_size

    def trigger_update(self, rgb_gray, thermal_gray):
        if not self.is_processing:
            self.thread = threading.Thread(target=self._update_matrix, args=(rgb_gray, thermal_gray), daemon=True)
            self.thread.start()

    def _update_matrix(self, rgb_gray, thermal_gray):
        self.is_processing = True
        try:
            # 원래 코드 방식: Canny 에지 추출
            edge_r = cv2.Canny(rgb_gray, 50, 150)
            edge_t = cv2.Canny(cv2.normalize(thermal_gray, None, 0, 255, cv2.NORM_MINMAX), 30, 100)
            
            criteria = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 35, 1e-5)
            
            # ECC 알고리즘 수행 (기존 warp_matrix를 시작점으로 사용)
            cc, new_matrix = cv2.findTransformECC(edge_r, edge_t, self.warp_matrix.copy(), 
                                                 cv2.MOTION_AFFINE, criteria)
            
            with self.lock:
                self.warp_matrix = new_matrix
                self.current_cc = cc
        except cv2.error:
            pass
        finally:
            self.is_processing = False

# --- 3. FireDetector (YOLO + Flickering 하이브리드) ---
class FireDetector:
    def __init__(self, history_len=15):
        self.history = deque(maxlen=history_len)
        self.yolo_model = YOLO('yolov8n.pt') if YOLO_AVAILABLE else None
        self.cached_fire_rects = []

    def detect(self, fused_rgb, thermal_gray, frame_count):
        # 1. 고온 영역 마스크
        _, hot_mask = cv2.threshold(thermal_gray, 200, 255, cv2.THRESH_BINARY)
        
        # 2. 색상 마스크
        hsv = cv2.cvtColor(fused_rgb, cv2.COLOR_BGR2HSV)
        color_mask = cv2.inRange(hsv, (0, 100, 100), (25, 255, 255))
        
        intersection = cv2.bitwise_and(hot_mask, color_mask)
        contours, _ = cv2.findContours(intersection, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        current_fire_found = False
        self.cached_fire_rects = []
        
        for cnt in contours:
            if cv2.contourArea(cnt) > 50:
                current_fire_found = True
                self.cached_fire_rects.append(cv2.boundingRect(cnt))
        
        self.history.append(current_fire_found)
        confirmed_fire = self.history.count(True) >= (self.history.maxlen * 0.6)
        
        return confirmed_fire, self.cached_fire_rects

# --- 4. Main Loop ---
def main():
    USER, PASS, IP = "admin", "", "192.168.0.69"
    encoded_pass = urllib.parse.quote(PASS, safe='')
    rgb_url = f"rtsp://{USER}:{encoded_pass}@{IP}/0/profile2/media.smp"
    th_url = f"rtsp://{USER}:{encoded_pass}@{IP}/1/profile2/media.smp"

    vs_rgb = VideoStream(rgb_url, "RGB").start()
    vs_th = VideoStream(th_url, "Thermal").start()
    
    TARGET_W, TARGET_H = 640, 360
    aligner = PrecisionAligner((TARGET_W, TARGET_H))
    detector = FireDetector()
    
    frame_count = 0
    fps, start_time = 15.0, time.time()

    try:
        while True:
            t_loop = time.time()
            f_rgb = vs_rgb.read()
            f_th = vs_th.read()
            
            if f_rgb is None or f_th is None:
                continue

            r_img = cv2.resize(f_rgb, (TARGET_W, TARGET_H))
            t_raw = cv2.resize(f_th, (TARGET_W, TARGET_H))
            r_gray = cv2.cvtColor(r_img, cv2.COLOR_BGR2GRAY)
            t_gray = cv2.cvtColor(t_raw, cv2.COLOR_BGR2GRAY)

            # 정합 업데이트 (원래 방식대로 30프레임마다)
            if frame_count % 30 == 0:
                aligner.trigger_update(r_gray, t_gray)

            with aligner.lock:
                current_warp = aligner.warp_matrix.copy()
                cc = aligner.current_cc

            # 워핑 적용
            reg_t_gray = cv2.warpAffine(t_gray, current_warp, (TARGET_W, TARGET_H), 
                                        flags=cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP)
            
            # 화재 감지
            is_fire, rects = detector.detect(r_img, reg_t_gray, frame_count)

            # --- 시각화 (원래의 JET 방식 복구) ---
            reg_t_color = cv2.applyColorMap(reg_t_gray, cv2.COLORMAP_JET)
            _, mask = cv2.threshold(reg_t_gray, 30, 255, cv2.THRESH_BINARY)
            
            overlay = cv2.addWeighted(r_img, 0.4, reg_t_color, 0.6, 0)
            display = r_img.copy()
            cv2.copyTo(overlay, mask, display)

            if is_fire:
                cv2.rectangle(display, (0, 0), (TARGET_W, TARGET_H), (0, 0, 255), 10)
                for (x, y, w, h) in rects:
                    cv2.rectangle(display, (x, y), (x + w, y + h), (0, 0, 255), 2)
                cv2.putText(display, "!!! FIRE ALARM !!!", (200, 50), 
                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 3)

            cv2.putText(display, f"FPS: {fps:.1f} | ECC: {cc:.4f}", (10, TARGET_H-10), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

            cv2.imshow("Hanwha Precision Fusion", display)

            frame_count += 1
            fps = 1.0 / (time.time() - t_loop) if (time.time() - t_loop) > 0 else 15
            if cv2.waitKey(1) & 0xFF == ord('q'): break

    finally:
        vs_rgb.stop()
        vs_th.stop()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()