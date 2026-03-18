import cv2
import numpy as np
import os
import threading
import time
import urllib.parse
import cv2
import numpy as np
from collections import deque
        
# 디스플레이 설정
os.environ["DISPLAY"] = ":0"
# RTSP 전송 및 지연 최적화 (TCP 강제)
os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp|max_delay;500000"

class FireDetector:
    def __init__(self, history_len=10):
        # 불꽃의 동적 움직임을 추적하기 위한 큐
        self.area_history = deque(maxlen=history_len)
        self.fire_detected = False

    def detect(self, r_img, t_gray):
        # 1. Thermal 임계값 필터링 (고온부 추출)
        # 200 이상은 매우 뜨거운 영역
        _, t_mask = cv2.threshold(t_gray, 200, 255, cv2.THRESH_BINARY)
        
        # 노이즈 제거 (Opening 연산)
        kernel = np.ones((3, 3), np.uint8)
        t_mask = cv2.morphologyEx(t_mask, cv2.MORPH_OPEN, kernel)

        # 2. RGB 채도/색상 확인 (주황/적색 계열)
        hsv = cv2.cvtColor(r_img, cv2.COLOR_BGR2HSV)
        # 불꽃의 전형적인 HSV 범위 (빨강~주황)
        lower_fire = np.array([0, 100, 100])
        upper_fire = np.array([25, 255, 255])
        rgb_mask = cv2.inRange(hsv, lower_fire, upper_fire)

        # 3. 두 모달리티 결합 (교집합)
        combined_mask = cv2.bitwise_and(t_mask, rgb_mask)
        
        # 4. 동적 분석 (면적 변화율 계산)
        contours, _ = cv2.findContours(combined_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        current_area = 0
        fire_rects = []

        for cnt in contours:
            area = cv2.contourArea(cnt)
            if area > 50:  # 너무 작은 노이즈 무시
                current_area += area
                fire_rects.append(cv2.boundingRect(cnt))

        self.area_history.append(current_area)

        # 면적이 일정 이상이고, 시간에 따라 크기가 미세하게 변하면(flickering) 화재로 간주
        if len(self.area_history) == self.area_history.maxlen:
            std_dev = np.std(self.area_history)
            # 평균 면적이 크고 변화량(표준편차)이 0보다 크면 움직이는 불꽃일 확률 높음
            if np.mean(self.area_history) > 100 and std_dev > 5:
                self.fire_detected = True
            else:
                self.fire_detected = False
        
        return self.fire_detected, fire_rects

class VideoStream:
    def __init__(self, url, name):
        self.name = name
        self.cap = cv2.VideoCapture(url, cv2.CAP_FFMPEG)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.frame = None
        self.ret = False
        self.stopped = False

        start_time = time.time()
        while not self.cap.isOpened():
            if time.time() - start_time > 5:
                print(f"[X] {self.name} 스트림 연결 실패")
                return
            time.sleep(0.5)
            self.cap.open(url, cv2.CAP_FFMPEG)
        
        print(f"[V] {self.name} 스트림 연결 성공")
        self.ret, self.frame = self.cap.read()

    def start(self):
        if self.cap.isOpened():
            threading.Thread(target=self.update, args=(), daemon=True).start()
        return self

    def update(self):
        while not self.stopped:
            # grab()으로 최신 패킷 확보, retrieve()로 디코딩
            if self.cap.grab():
                success, frame = self.cap.retrieve()
                if success:
                    self.frame = frame
            else:
                time.sleep(0.01)

    def read(self):
        return self.frame

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
            # 에지 기반 정합 (왜곡 방지를 위한 Canny 필터링)
            edge_r = cv2.Canny(cv2.cvtColor(img_r, cv2.COLOR_BGR2GRAY), 50, 150)
            edge_t = cv2.Canny(cv2.normalize(img_t, None, 0, 255, cv2.NORM_MINMAX), 30, 100)
            
            criteria = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 35, 1e-5)
            
            with self.lock:
                start_matrix = self.warp_matrix.copy()
            
            # ECC 알고리즘 수행
            cc, new_matrix = cv2.findTransformECC(edge_r, edge_t, start_matrix, 
                                                cv2.MOTION_AFFINE, criteria)
            
            with self.lock:
                self.warp_matrix = new_matrix
                self.current_cc = cc
                self.is_aligned = (cc >= 0.3)
        except cv2.error:
            with self.lock:
                self.current_cc = -1.0
                self.is_aligned = False
        finally:
            self.is_processing = False

def draw_info(img, frame_count, cc, fps, next_align_in, is_aligned):
    # 1. 텍스트를 그릴 바탕(검정색 상단 바) 생성
    # 상단 80픽셀 정도를 반투명 검정색으로 덮습니다.
    overlay = img.copy()
    cv2.rectangle(overlay, (0, 0), (img.shape[1], 85), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.7, img, 0.3, 0, img)

    font = cv2.FONT_HERSHEY_SIMPLEX
    status_color = (0, 255, 0) if is_aligned else (0, 0, 255)
    
    # 2. 텍스트 좌표 및 크기 재설정 (640x360 해상도 최적화)
    # 가독성을 위해 폰트 두께(thickness)를 2로 올립니다.
    cv2.putText(img, f"FRAME: {frame_count:04d}", (15, 30), font, 0.5, (255, 255, 255), 1)
    cv2.putText(img, f"FPS: {fps:.1f}", (160, 30), font, 0.5, (0, 255, 255), 1)
    
    status_text = "ALIGNED" if is_aligned else "SEARCHING"
    cv2.putText(img, f"STATUS: {status_text}", (300, 30), font, 0.5, status_color, 2)
    
    # ECC SCORE 강조
    cv2.putText(img, f"ECC SCORE: {cc:.4f}", (15, 65), font, 0.7, status_color, 2)
    
    align_text = "STABLE" if is_aligned else f"RE-ALIGN IN: {next_align_in}f"
    cv2.putText(img, align_text, (300, 65), font, 0.5, (200, 200, 200), 1)
    
    return img

def process_ultra_fast_fusion():
    # 카메라 접속 정보
    USER, PASS, IP1, IP2 = "admin", "5hanwha!", "192.168.0.112", "192.168.0.112" 
    encoded_pass = urllib.parse.quote(PASS, safe='')
    
    rgb_url = f"rtsp://{USER}:{encoded_pass}@{IP1}/0/profile2/media.smp"
    th_url = f"rtsp://{USER}:{encoded_pass}@{IP2}/1/profile2/media.smp"

    vs_rgb = VideoStream(rgb_url, "RGB").start()
    vs_th = VideoStream(th_url, "Thermal").start()
    
    TARGET_W, TARGET_H = 640, 360
    aligner = PrecisionAligner()
    # [추가] 화재 감지기 객체 생성
    detector = FireDetector(history_len=15) 
    frame_count = 0

    while True:
        start_time = time.time()
        frame_rgb = vs_rgb.read()
        frame_th = vs_th.read()
        
        if frame_rgb is None or frame_th is None:
            time.sleep(0.01)
            continue

        r_img = cv2.resize(frame_rgb, (TARGET_W, TARGET_H))
        t_img_raw = cv2.resize(frame_th, (TARGET_W, TARGET_H))
        t_img_gray = cv2.cvtColor(t_img_raw, cv2.COLOR_BGR2GRAY)

        # 1. 정합 업데이트 (기존 동일)
        interval = 900 if aligner.is_aligned else 30
        if frame_count % interval == 0 and not aligner.is_processing:
            threading.Thread(target=aligner.update_matrix, 
                             args=(r_img.copy(), t_img_gray.copy()), 
                             daemon=True).start()

        with aligner.lock:
            current_warp = aligner.warp_matrix.copy()
            is_aligned = aligner.is_aligned
            current_cc = aligner.current_cc

        # 2. 워핑 및 합성 (기존 동일)
        try:
            reg_t_gray = cv2.warpAffine(t_img_gray, current_warp, (TARGET_W, TARGET_H), 
                                        flags=cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP)
            
            # [추가] 화재 감지 수행 (정합된 열화상과 RGB 사용)
            is_fire, fire_rects = detector.detect(r_img, reg_t_gray)

            reg_t_color = cv2.applyColorMap(reg_t_gray, cv2.COLORMAP_JET)
            _, mask_uint8 = cv2.threshold(reg_t_gray, 30, 255, cv2.THRESH_BINARY)
            overlay_img = cv2.addWeighted(r_img, 0.4, reg_t_color, 0.6, 0)
            
            fused = r_img.copy()
            cv2.copyTo(overlay_img, mask_uint8, fused)

            # 3. 정보 오버레이 및 화재 구역 강조 표시
            fps = 1 / (time.time() - start_time) if (time.time() - start_time) > 0 else 0
            final_display = draw_info(fused, frame_count, current_cc, fps, 
                                    interval - (frame_count % interval), is_aligned)
            
            # [추가] 화재 발생 시 시각적 효과
            if is_fire:
                # 1. 화면 전체에 빨간색 테두리 경고
                cv2.rectangle(final_display, (0, 0), (TARGET_W, TARGET_H), (0, 0, 255), 10)
                # 2. 감지된 구역 사각형 그리기 및 라벨링
                for (x, y, w, h) in fire_rects:
                    cv2.rectangle(final_display, (x, y), (x + w, y + h), (0, 0, 255), 2)
                    cv2.putText(final_display, "FIRE DETECTED", (x, y - 10), 
                                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 2)
                # 3. 상단 상태바에 추가 경고 문구
                cv2.putText(final_display, "!!! CRITICAL ALARM !!!", (220, 110), 
                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 3)

            cv2.imshow("Hanwha Precision Fusion & Fire Detection", final_display)
                                
        except Exception as e:
            print(f"Blending/Detection Error: {e}")

        frame_count += 1
        if cv2.waitKey(1) & 0xFF == ord('q'): break

    vs_rgb.stop()
    vs_th.stop()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    process_ultra_fast_fusion()