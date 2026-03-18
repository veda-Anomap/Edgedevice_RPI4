import cv2
import numpy as np
import os
import threading
import time
import urllib.parse
from collections import deque

# 디스플레이 및 RTSP 설정
os.environ["DISPLAY"] = ":0"
os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|max_delay;500000"

class FireTracker:
    def __init__(self, dist_threshold=60, max_lost=15):
        self.next_id = 0
        self.objs = {}      # {id: centroid}
        self.rects = {}     # {id: rect}
        self.lost = {}      # {id: missed_frames}
        self.start = {}     # {id: first_detected_time}
        self.conf = {}      # {id: bool_is_confirmed}
        self.dist_thresh = dist_threshold
        self.max_lost = max_lost

    def update(self, current_rects):
        # 1. 현재 프레임의 중심점 계산
        cur_data = []
        for r in current_rects:
            cx, cy = int(r[0] + r[2]/2), int(r[1] + r[3]/2)
            cur_data.append(((cx, cy), r))

        # 2. 기존 객체 매칭
        used_idx = set()
        for oid, prev_c in list(self.objs.items()):
            found = False
            for i, (cur_c, rect) in enumerate(cur_data):
                if i not in used_idx:
                    dist = np.linalg.norm(np.array(prev_c) - np.array(cur_c))
                    if dist < self.dist_thresh:
                        self.objs[oid] = cur_c
                        self.rects[oid] = rect
                        self.lost[oid] = 0
                        used_idx.add(i)
                        found = True
                        break
            if not found:
                self.lost[oid] += 1
                if self.lost[oid] > self.max_lost:
                    self._remove(oid)

        # 3. 신규 객체 등록
        for i, (cur_c, rect) in enumerate(cur_data):
            if i not in used_idx:
                self.objs[self.next_id] = cur_c
                self.rects[self.next_id] = rect
                self.lost[self.next_id] = 0
                self.start[self.next_id] = time.time()
                self.conf[self.next_id] = False
                self.next_id += 1
        
        return self.rects

    def _remove(self, oid):
        for d in [self.objs, self.rects, self.lost, self.start, self.conf]:
            d.pop(oid, None)

class FireDetector:
    def __init__(self, history_len=15):
        self.history = deque(maxlen=history_len)

    def detect(self, r_img, t_gray, is_active=False):
        t_val = 180 if is_active else 210
        a_val = 40 if is_active else 100

        _, t_mask = cv2.threshold(t_gray, t_val, 255, cv2.THRESH_BINARY)
        hsv = cv2.cvtColor(r_img, cv2.COLOR_BGR2HSV)
        rgb_mask = cv2.inRange(hsv, np.array([0, 100, 100]), np.array([25, 255, 255]))
        combined = cv2.bitwise_and(t_mask, rgb_mask)
        
        contours, _ = cv2.findContours(combined, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        rects = [cv2.boundingRect(c) for c in contours if cv2.contourArea(c) > a_val]
        
        total_area = sum(r[2]*r[3] for r in rects)
        self.history.append(total_area)
        
        is_fire = False
        if len(self.history) == self.history.maxlen:
            if np.mean(self.history) > a_val and np.std(self.history) > 3:
                is_fire = True
        return is_fire, rects

class VideoStream:
    def __init__(self, url, name):
        self.name = name
        self.cap = cv2.VideoCapture(url, cv2.CAP_FFMPEG)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.frame = None
        self.stopped = False
        
        start_t = time.time()
        while not self.cap.isOpened() and time.time() - start_t < 5:
            time.sleep(0.5)
        if self.cap.isOpened(): print(f"[V] {name} Connected")

    def start(self):
        threading.Thread(target=self.update, daemon=True).start()
        return self

    def update(self):
        while not self.stopped:
            if self.cap.grab():
                ret, frame = self.cap.retrieve()
                if ret: self.frame = frame
            else: time.sleep(0.01)

    def read(self): return self.frame
    def stop(self):
        self.stopped = True
        self.cap.release()

class PrecisionAligner:
    def __init__(self):
        self.warp_matrix = np.eye(2, 3, dtype=np.float32)
        self.is_processing = False
        self.lock = threading.Lock()
        self.current_cc = 0.0
        self.is_aligned = False

    def update_matrix(self, img_r, img_t):
        self.is_processing = True
        try:
            edge_r = cv2.Canny(cv2.cvtColor(img_r, cv2.COLOR_BGR2GRAY), 50, 150)
            edge_t = cv2.Canny(cv2.normalize(img_t, None, 0, 255, cv2.NORM_MINMAX), 30, 100)
            criteria = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 35, 1e-5)
            with self.lock: start_m = self.warp_matrix.copy()
            cc, new_m = cv2.findTransformECC(edge_r, edge_t, start_m, cv2.MOTION_AFFINE, criteria)
            with self.lock:
                self.warp_matrix, self.current_cc, self.is_aligned = new_m, cc, (cc >= 0.3)
        except: pass
        finally: self.is_processing = False

def draw_info(img, frame_count, cc, fps, next_align_in, is_aligned):
    overlay = img.copy()
    cv2.rectangle(overlay, (0, 0), (img.shape[1], 85), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.7, img, 0.3, 0, img)
    font = cv2.FONT_HERSHEY_SIMPLEX
    color = (0, 255, 0) if is_aligned else (0, 0, 255)
    cv2.putText(img, f"FRAME: {frame_count:04d}  FPS: {fps:.1f}", (15, 30), font, 0.5, (255, 255, 255), 1)
    cv2.putText(img, f"ECC: {cc:.4f}  STATUS: {'ALIGNED' if is_aligned else 'SEARCH'}", (15, 65), font, 0.6, color, 2)
    return img

def process_ultra_fast_fusion():
    USER, PASS, IP = "admin", "5hanwha!", "192.168.0.112"
    encoded_pass = urllib.parse.quote(PASS, safe='')
    rgb_url = f"rtsp://{USER}:{encoded_pass}@{IP}/0/profile2/media.smp"
    th_url = f"rtsp://{USER}:{encoded_pass}@{IP}/1/profile2/media.smp"

    vs_rgb = VideoStream(rgb_url, "RGB").start()
    vs_th = VideoStream(th_url, "Thermal").start()
    
    aligner, detector, tracker = PrecisionAligner(), FireDetector(), FireTracker()
    frame_count, is_active = 0, False

    while True:
        start_t = time.time()
        img_r, img_t = vs_rgb.read(), vs_th.read()
        if img_r is None or img_t is None: continue

        img_r = cv2.resize(img_r, (640, 360))
        img_t_gray = cv2.cvtColor(cv2.resize(img_t, (640, 360)), cv2.COLOR_BGR2GRAY)

        # 1. ECC 정합
        interval = 900 if aligner.is_aligned else 60
        if frame_count % interval == 0 and not aligner.is_processing:
            threading.Thread(target=aligner.update_matrix, args=(img_r.copy(), img_t_gray.copy()), daemon=True).start()

        # 2. 워핑 및 감지
        with aligner.lock:
            reg_t = cv2.warpAffine(img_t_gray, aligner.warp_matrix, (640, 360), flags=cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP)
        
        is_fire_logic, rects = detector.detect(img_r, reg_t, is_active)
        tracked_rects = tracker.update(rects if is_fire_logic else [])
        is_active = len(tracked_rects) > 0

        # 3. 시각화
        reg_t_color = cv2.applyColorMap(reg_t, cv2.COLORMAP_JET)
        fused = cv2.addWeighted(img_r, 0.5, reg_t_color, 0.5, 0)
        
        for oid, (x, y, w, h) in tracked_rects.items():
            duration = time.time() - tracker.start[oid]
            is_conf = duration >= 5.0
            if is_conf and not tracker.conf[oid]:
                tracker.conf[oid] = True
                print(f"[ALARM] ID:{oid} FIRE CONFIRMED!")

            color = (0, 0, 255) if is_conf else (0, 255, 255)
            cv2.rectangle(fused, (x, y), (x+w, y+h), color, 2)
            cv2.putText(fused, f"ID:{oid} ({duration:.1f}s)", (x, y-7), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

        fps = 1 / (time.time() - start_t + 1e-6)
        final = draw_info(fused, frame_count, aligner.current_cc, fps, 0, aligner.is_aligned)
        
        cv2.imshow("Fire Detection System", final)
        frame_count += 1
        if cv2.waitKey(1) & 0xFF == ord('q'): break

    vs_rgb.stop(); vs_th.stop(); cv2.destroyAllWindows()

if __name__ == "__main__":
    process_ultra_fast_fusion()