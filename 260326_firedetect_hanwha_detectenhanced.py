import cv2
import numpy as np
import urllib.parse
import os
from collections import deque

os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|max_delay;500000"

# =========================
# Video Stream
# =========================
class VideoStream:
    def __init__(self, url, name):
        self.url = url
        self.name = name
        self.cap = cv2.VideoCapture(self.url, cv2.CAP_FFMPEG)
        self.Q = deque(maxlen=1)
        self.stopped = False
        self.retries = 0

    def start(self):
        import threading
        threading.Thread(target=self.update, daemon=True).start()
        return self

    def update(self):
        import time
        while not self.stopped:
            if not self.cap.isOpened():
                self.reconnect()
                continue
            ret, frame = self.cap.read()
            if ret:
                self.Q.append(frame)
                self.retries = 0
            else:
                self.reconnect()

    def reconnect(self):
        import time
        self.Q.clear()
        wait_time = min(60, 2 ** (self.retries + 1))
        print(f"[!] {self.name} reconnect...")
        time.sleep(wait_time)
        self.cap.release()
        self.cap.open(self.url, cv2.CAP_FFMPEG)
        self.retries += 1

    def read(self):
        return self.Q[-1] if self.Q else None

    def stop(self):
        self.stopped = True
        self.cap.release()


# =========================
# Thermal Fire Detector (강화)
# =========================
class ThermalFireDetector:
    def __init__(self):
        self.history = deque(maxlen=15)

    def detect(self, frame):
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

        # 🔥 RED 영역 확대 (끝부분까지 포함)
        lower_red1 = np.array([0, 40, 40])
        upper_red1 = np.array([10, 255, 255])
        lower_red2 = np.array([170, 40, 40])
        upper_red2 = np.array([180, 255, 255])

        mask1 = cv2.inRange(hsv, lower_red1, upper_red1)
        mask2 = cv2.inRange(hsv, lower_red2, upper_red2)
        red_mask = cv2.bitwise_or(mask1, mask2)

        # =====================
        # Morphology + Dilation (타원형 커널)
        # =====================
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 15))
        red_mask = cv2.morphologyEx(red_mask, cv2.MORPH_CLOSE, kernel)
        red_mask = cv2.dilate(red_mask, kernel, iterations=1)
        red_mask = cv2.morphologyEx(red_mask, cv2.MORPH_OPEN, kernel)

        # =====================
        # Contours 합쳐서 bbox 생성
        # =====================
        contours, _ = cv2.findContours(red_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        all_points = []

        for c in contours:
            x, y, w, h = cv2.boundingRect(c)
            area = w * h
            # 면적 작고 길이/너비도 작은 경우 제외
            if area < 200 and max(w, h) < 20:
                continue
            hull = cv2.convexHull(c)
            all_points.extend(hull.reshape(-1, 2))

        boxes = []
        if all_points:
            all_points = np.array(all_points)
            x, y, w, h = cv2.boundingRect(all_points)
            boxes.append((x, y, w, h))

        # =====================
        # Temporal Filter
        # =====================
        detected = len(boxes) > 0
        self.history.append(detected)
        confirmed = self.history.count(True) > 8

        return confirmed, boxes, red_mask


# =========================
# Main
# =========================
def main():
    USER, PASS, IP = "admin", "5hanwha!", "192.168.0.22"
    encoded_pass = urllib.parse.quote(PASS, safe='')
    th_url = f"rtsp://{USER}:{encoded_pass}@{IP}/0/profile1/media.smp"

    vs = VideoStream(th_url, "Thermal").start()
    detector = ThermalFireDetector()

    TARGET_W, TARGET_H = 640, 480

    while True:
        frame = vs.read()
        if frame is None:
            continue

        frame = cv2.resize(frame, (TARGET_W, TARGET_H))

        fire, boxes, mask = detector.detect(frame)

        # =====================
        # Visualization
        # =====================
        display = frame.copy()  # 원본 영상 그대로
        if fire:
            cv2.rectangle(display, (0, 0), (TARGET_W, TARGET_H), (0, 0, 255), 5)
            for (x, y, w, h) in boxes:
                cv2.rectangle(display, (x, y), (x + w, y + h), (0, 0, 255), 2)
                cv2.putText(display, "화재감지", (x, y - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)

        cv2.imshow("Thermal Fire Detection", display)
        cv2.imshow("Red Mask", mask)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    vs.stop()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()