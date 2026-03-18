import cv2
import numpy as np
import os
import time
import threading
from collections import deque
import urllib.parse

# Try importing ultralytics. Handle missing package gracefully.
try:
    from ultralytics import YOLO
    YOLO_AVAILABLE = True
except ImportError:
    YOLO_AVAILABLE = False
    print("[WARNING] ultralytics package not found. YOLOv8 features will be disabled.")

os.environ["DISPLAY"] = ":0"
os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|max_delay;500000"

# --- 1. Robust Video Ingestion with Exponential Backoff ---
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
            
            # Grab and retrieve ensures buffer stays fresh
            success, frame = self.cap.read()
            if success:
                self.Q.append(frame)
                self.retries = 0
            else:
                self.reconnect()

    def reconnect(self):
        self.Q.clear()  # Clear stale frames so main loop pauses processing
        wait_time = min(60, 2 ** (self.retries + 1))
        print(f"[!] {self.name} Connection lost. Retrying in {wait_time}s...")
        time.sleep(wait_time)
        self.cap.release()
        self.cap.open(self.url, cv2.CAP_FFMPEG)
        self.retries += 1

    def read(self):
        if len(self.Q) > 0:
            return self.Q[-1]
        return None

    def stop(self):
        self.stopped = True
        if hasattr(self, 'thread') and self.thread.is_alive():
            self.thread.join(timeout=1.0)
        if self.cap:
            self.cap.release()

# --- 2. Registration & Alignment Utils ---
def compute_gradient_magnitude(gray):
    gx = cv2.Scharr(gray, cv2.CV_32F, 1, 0)
    gy = cv2.Scharr(gray, cv2.CV_32F, 0, 1)
    mag = cv2.magnitude(gx, gy)
    return cv2.normalize(mag, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)

def compute_initial_affine(rgb_size, thermal_size, target_size):
    target_w, target_h = target_size
    rgb_w, rgb_h = rgb_size
    th_w, th_h = thermal_size

    rgb_aspect = rgb_w / rgb_h
    th_aspect = th_w / th_h

    # Relative scale with clamping
    sx = float(np.clip(th_aspect / rgb_aspect, 0.3, 3.0))
    sy = float(np.clip(rgb_aspect / th_aspect, 0.3, 3.0))

    tx = (1.0 - sx) * target_w * 0.5
    ty = (1.0 - sy) * target_h * 0.5

    return np.array([
        [sx,  0.0, tx],
        [0.0, sy,  ty]
    ], dtype=np.float32)

class PrecisionAligner:
    def __init__(self, target_size, initial_H=None):
        self.warp_matrix = initial_H.copy() if initial_H is not None else np.eye(2, 3, dtype=np.float32)
        self.is_processing = False
        self.lock = threading.Lock()
        self.current_cc = 1.0  # Start high to avoid immediate panic trigger
        self.target_size = target_size
        
        # 3-Level Pyramid for escaping local optima
        w, h = target_size
        self.pyramid_levels = [
            ((w // 4, h // 4), (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 30, 1e-3)),
            ((w // 2, h // 2), (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 20, 1e-4)),
            ((w, h), (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 15, 1e-5)),
        ]

    def _scale_matrix_to(self, H, from_size, to_size):
        sx, sy = to_size[0] / from_size[0], to_size[1] / from_size[1]
        H_out = H.copy()
        H_out[0, 2] *= sx
        H_out[1, 2] *= sy
        return H_out

    def trigger_update(self, rgb_gray, thermal_gray):
        if not self.is_processing:
            self.thread = threading.Thread(target=self._update_matrix, args=(rgb_gray, thermal_gray), daemon=True)
            self.thread.start()

    def stop(self):
        if hasattr(self, 'thread') and self.thread.is_alive():
            self.thread.join(timeout=1.0)

    def _update_matrix(self, rgb_gray, thermal_gray):
        self.is_processing = True
        try:
            with self.lock:
                H_current = self.warp_matrix.copy()
            best_cc, best_H = -1.0, H_current.copy()

            for level_size, criteria in self.pyramid_levels:
                rgb_resized = cv2.resize(rgb_gray, level_size)
                th_resized = cv2.resize(thermal_gray, level_size)

                rgb_blur = cv2.GaussianBlur(rgb_resized, (5, 5), 0)
                th_blur = cv2.GaussianBlur(th_resized, (5, 5), 0)

                grad_rgb = compute_gradient_magnitude(rgb_blur)
                grad_th = compute_gradient_magnitude(th_blur)

                H_level = self._scale_matrix_to(H_current, self.target_size, level_size)
                try:
                    cc, new_H_level = cv2.findTransformECC(
                        grad_rgb.astype(np.float32), grad_th.astype(np.float32),
                        H_level, cv2.MOTION_AFFINE, criteria
                    )
                    H_current = self._scale_matrix_to(new_H_level, level_size, self.target_size)
                    best_cc, best_H = cc, H_current.copy()
                except cv2.error:
                    continue

            with self.lock:
                if best_cc > 0.1:
                    self.warp_matrix = best_H
                self.current_cc = best_cc if best_cc > 0 else 0.0
        except Exception as e:
            with self.lock:
                self.current_cc = 0.0
                print(f"[Aligner] Error: {e}")
        finally:
            self.is_processing = False

# --- 3. Candidate-Based Fire Detection Logic ---
class FireDetector:
    def __init__(self, history_len=15):
        self.history = deque(maxlen=history_len)
        if YOLO_AVAILABLE:
            model_path = 'fire_model.pt' if os.path.exists('fire_model.pt') else 'yolov8n.pt'
            self.yolo_model = YOLO(model_path)
            print(f"[V] Loaded YOLO Model from {model_path}")
        else:
            self.yolo_model = None
            
        self.cached_fire_rects = []
        self.patch_size = 128

    def detect(self, fused_rgb, thermal_gray, frame_count, fps=15):
        # 1. Thermal Hotspot Mask (Isolate high temperatures)
        _, hot_mask = cv2.threshold(thermal_gray, 180, 255, cv2.THRESH_BINARY)
        
        # 2. RGB Colour Mask (Isolate Red/Orange flames)
        hsv = cv2.cvtColor(fused_rgb, cv2.COLOR_BGR2HSV)
        color_mask = cv2.inRange(hsv, (0, 50, 150), (35, 255, 255))
        
        # Physical spatial intersection
        intersection = cv2.bitwise_and(hot_mask, color_mask)
        current_yolo_status = False

        # 3. CPU Optimization: Candidate-Based ROI Inference
        # Instead of feeding 640x360 into YOLO, trigger Top-K patches every 1 second
        trigger_interval = max(1, int(fps))
        if frame_count % trigger_interval == 0 and self.yolo_model is not None:
            contours, _ = cv2.findContours(hot_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            hotspots = []
            
            for cnt in contours:
                x, y, w, h = cv2.boundingRect(cnt)
                roi = thermal_gray[y:y+h, x:x+w]
                if roi.size > 0:
                    _, max_val, _, max_loc = cv2.minMaxLoc(roi)
                    cx, cy = x + max_loc[0], y + max_loc[1]
                    hotspots.append({'max_val': max_val, 'cx': cx, 'cy': cy, 'bbox': (x, y, w, h)})
            
            # Extract Top-10 highest-temperature coordinates
            hotspots.sort(key=lambda item: item['max_val'], reverse=True)
            top_10 = hotspots[:10]
            
            self.cached_fire_rects = []
            for hs in top_10:
                cx, cy = hs['cx'], hs['cy']
                px1 = max(0, cx - self.patch_size // 2)
                py1 = max(0, cy - self.patch_size // 2)
                px2 = min(fused_rgb.shape[1], cx + self.patch_size // 2)
                py2 = min(fused_rgb.shape[0], cy + self.patch_size // 2)
                
                patch = fused_rgb[py1:py2, px1:px2]
                if patch.shape[0] < 32 or patch.shape[1] < 32:
                    continue
                
                # YOLO Inference STRICTLY on localised 128x128 patches
                results = self.yolo_model.predict(patch, verbose=False, imgsz=self.patch_size)
                for r in results:
                    for box in r.boxes:
                        if float(box.conf[0]) > 0.7:  # Validation criteria
                            self.cached_fire_rects.append(hs['bbox'])
                            break

        if len(self.cached_fire_rects) > 0:
            current_yolo_status = True

        # 4. Temporal Consistency Filtering
        # Requires overlap between modalities and positive neural net confirmation (if active)
        current_area = np.count_nonzero(intersection)
        valid_frame = (current_area > 30) and (current_yolo_status if self.yolo_model else True)
        
        self.history.append(valid_frame)
        
        # >60% consistency in the 15-frame buffer implies a confirmed sustained fire
        confirmed_fire = self.history.count(True) >= (self.history.maxlen * 0.6)
        
        return confirmed_fire, self.cached_fire_rects

# --- 4. Main Fusion Application ---
def enhance_thermal(thermal_gray, clip_limit=3.0):
    normalized = cv2.normalize(thermal_gray, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)
    clahe = cv2.createCLAHE(clipLimit=clip_limit, tileGridSize=(8, 8))
    return clahe.apply(normalized)

def main():
    USER, PASS, IP1, IP2 = "admin", "5hanwha!", "192.168.0.112", "192.168.0.112"
    encoded_pass = urllib.parse.quote(PASS, safe='')
    rgb_url = f"rtsp://{USER}:{encoded_pass}@{IP1}/0/profile2/media.smp"
    th_url = f"rtsp://{USER}:{encoded_pass}@{IP2}/1/profile2/media.smp"

    print("[*] Connecting to Dual RTSP threads...")
    vs_rgb = VideoStream(rgb_url, "RGB").start()
    vs_th = VideoStream(th_url, "Thermal").start()
    
    # Allow buffer to fill safely
    time.sleep(2.0)
    
    TARGET_W, TARGET_H = 640, 360
    
    # Establish Native Dimensions for Accurate Initial Alignment
    f_r = vs_rgb.read()
    f_t = vs_th.read()
    rgb_native = (f_r.shape[1], f_r.shape[0]) if f_r is not None else (1920, 1080)
    th_native = (f_t.shape[1], f_t.shape[0]) if f_t is not None else (1280, 960)
    
    H_init = compute_initial_affine(rgb_native, th_native, (TARGET_W, TARGET_H))
    
    aligner = PrecisionAligner((TARGET_W, TARGET_H), initial_H=H_init)
    detector = FireDetector(history_len=15)
    
    frame_count = 0
    fps, start_fps_time = 15.0, time.time()
    
    print("[*] Initiating real-time Inference Loop...")
    try:
        while True:
            frame_rgb = vs_rgb.read()
            frame_th = vs_th.read()
            
            if frame_rgb is None or frame_th is None:
                # Sleep briefly to avoid slamming CPU when awaiting exponential backoff reconnection
                time.sleep(0.01)
                continue
                
            r_img = cv2.resize(frame_rgb, (TARGET_W, TARGET_H))
            t_raw = cv2.resize(frame_th, (TARGET_W, TARGET_H))
            
            r_gray = cv2.cvtColor(r_img, cv2.COLOR_BGR2GRAY)
            t_gray = cv2.cvtColor(t_raw, cv2.COLOR_BGR2GRAY)
            
            # --- Event-Driven ECC ---
            with aligner.lock:
                current_warp = aligner.warp_matrix.copy()
                current_cc = aligner.current_cc
                is_processing = aligner.is_processing
            
            # Default 15 frames OR immediate trigger if correlation score significantly drops
            if not is_processing:
                if frame_count % 15 == 0 or current_cc < 0.3:
                    aligner.trigger_update(r_gray, t_gray)

            # Apply warp geometry
            reg_t_gray = cv2.warpAffine(t_gray, current_warp, (TARGET_W, TARGET_H), 
                                        flags=cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP)
            
            # --- Dual-Modality Detection ---
            is_fire, fire_rects = detector.detect(r_img, reg_t_gray, frame_count, fps=fps)
            
            # --- Visualisation & Blending ---
            enhanced_t = enhance_thermal(reg_t_gray, clip_limit=3.0)
            reg_t_color = cv2.applyColorMap(enhanced_t, cv2.COLORMAP_INFERNO)
            
            # Blend high temperature areas directly onto RGB
            _, blend_mask = cv2.threshold(reg_t_gray, 50, 255, cv2.THRESH_BINARY)
            overlay = cv2.addWeighted(r_img, 0.4, reg_t_color, 0.6, 0)
            display = r_img.copy()
            cv2.copyTo(overlay, blend_mask, display)
            
            # Annotation Formatting
            if is_fire:
                cv2.rectangle(display, (0, 0), (TARGET_W, TARGET_H), (0, 0, 255), 6)
                for (x, y, w, h) in fire_rects:
                    cv2.rectangle(display, (x, y), (x + w, y + h), (0, 0, 255), 2)
                    cv2.putText(display, "FIRE", (x, y - 5), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)
                cv2.putText(display, "CRITICAL FIRE ALARM", (TARGET_W//2 - 120, 50), 
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
            
            cv2.putText(display, f"FPS: {fps:.1f} | CC: {current_cc:.2f}", (10, 20), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

            cv2.imshow("RPi 4B - Fusion Fire Detection", display)

            # Telemetry
            frame_count += 1
            elapsed = time.time() - start_fps_time
            if elapsed > 1.0:
                fps = frame_count / elapsed
                frame_count = 0
                start_fps_time = time.time()
                
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        print("\n[*] Caught KeyboardInterrupt. Shutting down gracefully...")
    finally:
        # Graceful Teardown
        vs_rgb.stop()
        vs_th.stop()
        aligner.stop()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
