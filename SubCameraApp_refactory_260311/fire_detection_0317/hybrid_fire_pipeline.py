import cv2
import numpy as np
import os
import threading
import time
import socket
import urllib.parse
from collections import deque

# Load Ultralytics gracefully
try:
    from ultralytics import YOLO
    YOLO_AVAILABLE = True
except ImportError:
    YOLO_AVAILABLE = False
    print("[WARNING] ultralytics package not found. YOLOv8 features will be disabled.")

os.environ["DISPLAY"] = ":0"
os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|max_delay;500000"

# --- 1. Robust Video Ingestion ---
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

# --- 2. Precision Aligner ---
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
            edge_t = cv2.Canny(cv2.normalize(img_t, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U), 30, 100)
            criteria = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 35, 1e-5)
            with self.lock: 
                start_m = self.warp_matrix.copy()
            cc, new_m = cv2.findTransformECC(edge_r, edge_t, start_m, cv2.MOTION_AFFINE, criteria)
            with self.lock:
                self.warp_matrix, self.current_cc, self.is_aligned = new_m, cc, (cc >= 0.3)
        except: 
            pass
        finally: 
            self.is_processing = False

# --- 3. Robust Hybrid Fire Detector ---
class HybridFireDetector:
    def __init__(self, history_len=15):
        self.history = deque(maxlen=history_len)
        self.patch_size = 320
        self.half_size = self.patch_size // 2
        
        if YOLO_AVAILABLE:
            # Fallback path if custom model not found
            model_path = 'fire_model.pt' if os.path.exists('fire_model.pt') else 'yolov8n.pt'
            self.yolo_model = YOLO(model_path)
            print(f"[V] Loaded YOLO Model from {model_path}")
        else:
            self.yolo_model = None

    def detect(self, fused_rgb, thermal_gray, is_active=False):
        # 1. Rule-Based ROI Extraction
        t_val = 180 if is_active else 210
        a_val = 40 if is_active else 100

        _, t_mask = cv2.threshold(thermal_gray, t_val, 255, cv2.THRESH_BINARY)
        hsv = cv2.cvtColor(fused_rgb, cv2.COLOR_BGR2HSV)
        rgb_mask = cv2.inRange(hsv, np.array([0, 100, 100]), np.array([25, 255, 255]))
        combined = cv2.bitwise_and(t_mask, rgb_mask)
        
        contours, _ = cv2.findContours(combined, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        candidates = []
        for c in contours:
            area = cv2.contourArea(c)
            if area > a_val:
                rect = cv2.boundingRect(c)
                candidates.append((area, rect))

        # Constraint 1: Memory & Resource Management (Top-K ROI)
        candidates.sort(key=lambda x: x[0], reverse=True)
        top_candidates = candidates[:10]  # Limit to Top 10 worst-case per frame
        
        candidate_rects = [c[1] for c in top_candidates]
        confirmed_rects = []

        if not candidate_rects or self.yolo_model is None:
            return False, candidate_rects, confirmed_rects

        # 2. Extract 320x320 Patches with Robust Padding
        H, W = fused_rgb.shape[:2]
        valid_patches = []
        valid_offsets = []

        for rect in candidate_rects:
            rx, ry, rw, rh = rect
            cx, cy = rx + rw // 2, ry + rh // 2
            
            # Identify bounds
            src_x_start = max(0, cx - self.half_size)
            src_x_end = min(W, cx + self.half_size)
            src_y_start = max(0, cy - self.half_size)
            src_y_end = min(H, cy + self.half_size)

            # Identify padding sizes if window exceeds global image bounds
            left_pad = max(0, self.half_size - cx)
            right_pad = max(0, cx + self.half_size - W)
            top_pad = max(0, self.half_size - cy)
            bottom_pad = max(0, cy + self.half_size - H)

            roi_img = fused_rgb[src_y_start:src_y_end, src_x_start:src_x_end]

            if top_pad > 0 or bottom_pad > 0 or left_pad > 0 or right_pad > 0:
                # Add black padding to ensure exactly 320x320 without OutOfBounds panics
                patch = cv2.copyMakeBorder(
                    roi_img, 
                    top_pad, bottom_pad, left_pad, right_pad, 
                    cv2.BORDER_CONSTANT, value=[0, 0, 0]
                )
            else:
                patch = roi_img.copy()

            # Constraint 3: Defensive Programming
            if isinstance(patch, np.ndarray) and patch.shape == (self.patch_size, self.patch_size, 3):
                valid_patches.append(patch)
                # Save offsets for Mapping local to global
                valid_offsets.append((cx - self.half_size, cy - self.half_size))

        # Constraint 3: Perform batched verification if valid arrays exist
        if len(valid_patches) > 0:
            try:
                # Efficient Batch Inference
                results = self.yolo_model.predict(
                    valid_patches, 
                    imgsz=self.patch_size, 
                    batch=len(valid_patches), 
                    verbose=False
                )

                # Constraint 2: Accurate Coordinate Mapping (Patch to Global)
                for i, r in enumerate(results):
                    off_x, off_y = valid_offsets[i]
                    for box in r.boxes:
                        if float(box.conf[0]) > 0.6:  # Verification Validation Criteria
                            bx1, by1, bx2, by2 = map(int, box.xyxy[0])
                            
                            # Map back to Global Frame Coordinates
                            gx1 = bx1 + off_x
                            gy1 = by1 + off_y
                            gx2 = bx2 + off_x
                            gy2 = by2 + off_y
                            
                            # Clamp values to screen bounds in case prediction slightly bleeds
                            gx1 = max(0, min(W, gx1))
                            gy1 = max(0, min(H, gy1))
                            gx2 = max(0, min(W, gx2))
                            gy2 = max(0, min(H, gy2))
                            
                            gw, gh = gx2 - gx1, gy2 - gy1
                            if gw > 0 and gh > 0:
                                confirmed_rects.append((gx1, gy1, gw, gh))

            except Exception as e:
                print(f"[ERROR] Batch Inference Runtime Exception: {e}")
            finally:
                # Thread Safety & Optimizer Memory Usage
                valid_patches.clear()
                valid_offsets.clear()

        # Update historical valid confirmations
        valid_frame = len(confirmed_rects) > 0
        self.history.append(valid_frame)

        # Confirm Fire Status if 60% of history includes confirmations
        confirmed_fire = False
        if len(self.history) == self.history.maxlen:
            confirmed_fire = self.history.count(True) >= int(self.history.maxlen * 0.6)

        return confirmed_fire, candidate_rects, confirmed_rects


# --- 4. Fire Tracker Logic ---
class FireTracker:
    def __init__(self, dist_threshold=60, max_lost=15):
        self.next_id = 0
        self.objs = {}
        self.rects = {}
        self.lost = {}
        self.start = {}
        self.conf = {}
        self.dist_thresh = dist_threshold
        self.max_lost = max_lost

    def update(self, current_rects):
        cur_data = []
        for r in current_rects:
            cx, cy = int(r[0] + r[2]/2), int(r[1] + r[3]/2)
            cur_data.append(((cx, cy), r))

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

# --- 4.5. Server Connection Manager ---
class ServerNetworkManager:
    """
    Emulates the C++ SubCameraApp's NetworkFacade: 
    1. Broadcasts UDP Beacon on port 10001 (SUB_PI_ALIVE)
    2. Listens for TCP commands on port 5000
    3. Handles 'START_STREAM:<port>' command to dynamically configure streaming
    """
    def __init__(self, tcp_port=5000, udp_beacon_port=10001):
        self.tcp_port = tcp_port
        self.udp_beacon_port = udp_beacon_port
        self.beacon_msg = b"SUB_PI_ALIVE"
        self.is_running = False
        self.target_ip_port = None
        self.lock = threading.Lock()
        
    def start(self):
        self.is_running = True
        self.beacon_thread = threading.Thread(target=self._beacon_loop, daemon=True)
        self.command_thread = threading.Thread(target=self._command_loop, daemon=True)
        self.beacon_thread.start()
        self.command_thread.start()
        
    def _beacon_loop(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        while self.is_running:
            with self.lock:
                is_connected = (self.target_ip_port is not None)
            if not is_connected:
                try:
                    sock.sendto(self.beacon_msg, ('<broadcast>', self.udp_beacon_port))
                except Exception:
                    pass
            time.sleep(1.0)
        sock.close()

    def _command_loop(self):
        server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_sock.bind(('0.0.0.0', self.tcp_port))
        server_sock.listen(3)
        print(f"[*] Command TCP Server listening on port {self.tcp_port}")
        
        server_sock.settimeout(1.0)
        while self.is_running:
            try:
                client, addr = server_sock.accept()
            except socket.timeout:
                continue
            except Exception:
                break
                
            print(f"[*] TCP Client connected from {addr[0]}")
            client.settimeout(1.0)
            while self.is_running:
                try:
                    data = client.recv(2048)
                    if not data:
                        break
                    
                    # CommandServer hybrid parsing logic
                    if data[0] > 0x0A: # Text protocol
                        cmd = data.decode('utf-8', errors='ignore')
                        if "START_STREAM:" in cmd:
                            try:
                                parts = cmd.split("START_STREAM:")
                                port_str = "".join(c for c in parts[1] if c.isdigit())
                                port = int(port_str) if port_str else 15001
                                with self.lock:
                                    self.target_ip_port = (addr[0], port)
                                print(f"[*] Received START_STREAM for {addr[0]}:{port}")
                            except Exception as e:
                                print(f"[!] Target Port parsing failed: {e}")
                    else: # Binary packet header (Type + Len + Body)
                        if len(data) >= 5:
                            body_len = int.from_bytes(data[1:5], byteorder='big')
                            if len(data) >= 5 + body_len:
                                body = data[5:5+body_len].decode('utf-8', errors='ignore')
                                if "START_STREAM:" in body:
                                    try:
                                        parts = body.split("START_STREAM:")
                                        port_str = "".join(c for c in parts[1] if c.isdigit())
                                        port = int(port_str) if port_str else 15001
                                        with self.lock:
                                            self.target_ip_port = (addr[0], port)
                                        print(f"[*] Received START_STREAM (Binary) for {addr[0]}:{port}")
                                    except Exception as e:
                                        pass
                except socket.timeout:
                    continue
                except Exception:
                    break
                    
            print(f"[*] TCP Client {addr[0]} disconnected.")
            client.close()
            with self.lock:
                self.target_ip_port = None
        server_sock.close()

    def get_target(self):
        with self.lock:
            return self.target_ip_port
            
    def stop(self):
        self.is_running = False

# --- 4.6. Video Streamer to Server ---
def create_stream_writer(target_ip, target_port, width=640, height=360, fps=15):
    """
    Creates a GStreamer VideoWriter to send video frames to a UDP server
    using the exact pipeline format from SubCameraApp (C++).
    """
    # Note: FourCC parameter is 0 for GStreamer pipelines in OpenCV
    pipeline = (
        f"appsrc ! video/x-raw, format=BGR, width={width}, height={height}, framerate={fps}/1 ! "
        f"videoconvert ! video/x-raw, format=I420 ! "
        f"x264enc tune=zerolatency bitrate=4000 speed-preset=ultrafast ! "
        f"rtph264pay ! udpsink host={target_ip} port={target_port} sync=false async=false"
    )
    
    writer = cv2.VideoWriter(pipeline, cv2.CAP_GSTREAMER, 0, fps, (width, height))
    if not writer.isOpened():
        print(f"[!] Warning: Failed to open GStreamer VideoWriter for {target_ip}:{target_port}. Is GStreamer installed?")
        return None
        
    print(f"[*] Started UDP Video Stream to {target_ip}:{target_port}")
    return writer

# --- 5. Main Application Logic ---
def process_hybrid_fusion():
    # Setup RTSP Info
    USER, PASS, IP1, IP2 = "admin", "5hanwha!", "192.168.0.112", "192.168.0.112"
    encoded_pass = urllib.parse.quote(PASS, safe='')
    rgb_url = f"rtsp://{USER}:{encoded_pass}@{IP1}/0/profile2/media.smp"
    th_url = f"rtsp://{USER}:{encoded_pass}@{IP2}/1/profile2/media.smp"

    print("[*] Connecting to Dual RTSP Streams...")
    vs_rgb = VideoStream(rgb_url, "RGB").start()
    vs_th = VideoStream(th_url, "Thermal").start()
    
    # Delay intentionally to allow buffer fill securely
    time.sleep(2.0)

    TARGET_W, TARGET_H = 640, 360
    
    aligner = PrecisionAligner()
    detector = HybridFireDetector(history_len=15)
    tracker = FireTracker()
    
    # Initialize Server Network Manager (Handles Beacon and ComamndServer)
    net_manager = ServerNetworkManager(tcp_port=5000, udp_beacon_port=10001)
    net_manager.start()
    
    stream_writer = None
    current_target = None

    frame_count = 0
    is_active = False
    
    # FPS Measurement
    fps_start_t = time.time()
    fps = 15.0

    print("[*] Initiating Fast Hybrid Inference Loop...")
    try:
        while True:
            loop_start_t = time.time()

            frame_rgb = vs_rgb.read()
            frame_th = vs_th.read()
            
            if frame_rgb is None or frame_th is None:
                time.sleep(0.01)
                continue

            r_img = cv2.resize(frame_rgb, (TARGET_W, TARGET_H))
            t_raw = cv2.resize(frame_th, (TARGET_W, TARGET_H))
            
            r_gray = cv2.cvtColor(r_img, cv2.COLOR_BGR2GRAY)
            t_gray = cv2.cvtColor(t_raw, cv2.COLOR_BGR2GRAY)

            # 1. ECC Registration Alignment Layer
            interval = 900 if aligner.is_aligned else 60
            if frame_count % interval == 0 and not aligner.is_processing:
                threading.Thread(
                    target=aligner.update_matrix, 
                    args=(r_img.copy(), t_gray.copy()), 
                    daemon=True
                ).start()

            with aligner.lock:
                current_warp = aligner.warp_matrix.copy()

            # Align Thermal to RGB coordinates
            reg_t_gray = cv2.warpAffine(
                t_gray, current_warp, (TARGET_W, TARGET_H), 
                flags=cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP
            )
            
            # 2. Hybrid Pipeline Processing Layer
            is_fire, candidate_rects, confirmed_rects = detector.detect(r_img, reg_t_gray, is_active)
            
            # Check for network target stream command changes
            new_target = net_manager.get_target()
            if new_target != current_target:
                if stream_writer is not None:
                    print("[*] Releasing previous stream writer.")
                    stream_writer.release()
                    stream_writer = None
                
                current_target = new_target
                if current_target is not None:
                    try:
                        stream_writer = create_stream_writer(
                            target_ip=current_target[0], 
                            target_port=current_target[1], 
                            width=TARGET_W, height=TARGET_H, fps=15
                        )
                    except Exception as e:
                        print(f"[!] Error creating stream writer: {e}")

            # Feed Confirmed Validations through the tracker
            tracked_rects = tracker.update(confirmed_rects)
            
            # Reduce rule-based thresholds if tracking active components
            is_active = len(tracked_rects) > 0 or len(candidate_rects) > 0

            # 3. Enhanced Analytics UI rendering Layer
            normalized_t = cv2.normalize(reg_t_gray, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)
            clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
            enhanced_t = clahe.apply(normalized_t)
            
            reg_t_color = cv2.applyColorMap(enhanced_t, cv2.COLORMAP_INFERNO)
            display = cv2.addWeighted(r_img, 0.6, reg_t_color, 0.4, 0)

            # Draw rule-based Candidate ROIs
            for (rx, ry, rw, rh) in candidate_rects:
                cv2.rectangle(display, (rx, ry), (rx+rw, ry+rh), (0, 165, 255), 1)

            # Draw deeply validated tracked objects
            for oid, (x, y, w, h) in tracked_rects.items():
                duration = time.time() - tracker.start[oid]
                is_conf = duration >= 3.0
                
                if is_conf and not tracker.conf[oid]:
                    tracker.conf[oid] = True
                    print(f"[ALARM] AI-Verified Sustained FIRE CONFIRMED, ID: {oid}")

                color = (0, 0, 255) if is_conf else (0, 255, 255)
                lw = 3 if is_conf else 2
                
                cv2.rectangle(display, (x, y), (x+w, y+h), color, lw)
                cv2.putText(
                    display, f"ID:{oid} ({duration:.1f}s)", 
                    (x, y - 7), cv2.FONT_HERSHEY_SIMPLEX, 
                    0.6, color, max(1, lw - 1)
                )

            # Overlay Statistics Panel
            cv2.rectangle(display, (0, 0), (display.shape[1], 85), (0, 0, 0), -1)
            cv2.addWeighted(display, 0.7, display, 0.3, 0, display)
            
            font = cv2.FONT_HERSHEY_SIMPLEX
            align_col = (0, 255, 0) if aligner.is_aligned else (0, 0, 255)
            
            cv2.putText(display, f"FRAME: {frame_count:04d}  FPS: {fps:.1f}", (15, 30), font, 0.6, (255, 255, 255), 2)
            cv2.putText(
                display, f"ECC: {aligner.current_cc:.4f}  STATE: {'ALIGNED' if aligner.is_aligned else 'SEARCH'}", 
                (15, 65), font, 0.6, align_col, 2
            )

            cv2.imshow("Hybrid Fire Pipeline", display)
            
            # Send the visualized frame to the server
            if stream_writer is not None:
                stream_writer.write(display)

            frame_count += 1
            
            # FPS Calculation Safety Net
            elapsed = time.time() - loop_start_t
            if elapsed > 0:
                # Add tiny moving average smoothing
                fps = (fps * 0.8) + ((1.0 / elapsed) * 0.2)

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        print("\n[*] Shutting down Gracefully by User Command...")
    finally:
        vs_rgb.stop()
        vs_th.stop()
        net_manager.stop()
        if stream_writer is not None:
            stream_writer.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    process_hybrid_fusion()
