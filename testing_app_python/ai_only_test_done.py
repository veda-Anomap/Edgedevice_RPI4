import sys
import cv2
import time
import numpy as np
import threading
from collections import deque
import tensorflow as tf
import os

from PyQt5.QtWidgets import *
from PyQt5.QtCore import *
from PyQt5.QtGui import *

# 낙상 룰 시뮬레이터 임포트
from pipeline.fall_rule_simulator import FallRuleSimulator, FallRuleParams

# =========================
# Thread-safe Frame Queue
# =========================
class FrameBuffer:
    def __init__(self):
        self.lock = threading.Lock()
        self.frame = None

    def set(self, frame):
        with self.lock:
            self.frame = frame.copy()

    def get(self):
        with self.lock:
            return None if self.frame is None else self.frame.copy()


# =========================
# TFLite Inference Thread
# =========================
class TFLiteThread(QThread):
    result_signal = pyqtSignal(object, float)

    def __init__(self, model_path):
        super().__init__()
        self.buffer = FrameBuffer()
        self.running = True

        print("[AI] Load TFLite...")
        self.interpreter = tf.lite.Interpreter(model_path=model_path)
        self.interpreter.allocate_tensors()

        self.input_details = self.interpreter.get_input_details()
        self.output_details = self.interpreter.get_output_details()

        self.input_shape = self.input_details[0]['shape']
        self.h, self.w = self.input_shape[1], self.input_shape[2]
        
        # AppConfig::KPT_SKELETON 동기화 (1-indexed -> 0-indexed)
        self.SKELETON = [
            (15,13),(13,11),(16,14),(14,12),(11,12),(5,11),(6,12),
            (5,6),(5,7),(6,8),(7,9),(8,10),(1,2),(0,1),(0,2),(1,3),(2,4),(3,5),(4,6)
        ]

    def set_frame(self, frame):
        self.buffer.set(frame)

    def preprocess(self, frame):
        if frame is None or frame.size == 0: return None # 안전장치
        img = cv2.resize(frame, (self.w, self.h))
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = img.astype(np.float32) / 255.0
        return np.expand_dims(img, axis=0)

    def run(self):
        while self.running:
            frame = self.buffer.get()

            if frame is None:
                self.msleep(10)
                continue

            try:
                input_data = self.preprocess(frame)

                t0 = time.time()

                self.interpreter.set_tensor(self.input_details[0]['index'], input_data)
                self.interpreter.invoke()

                # [동기화] AppConfig.h 기반 출력 파싱 (STIDE 57, NMS Baked-in)
                # Output shape: [1, 100, 57]
                output = self.interpreter.get_tensor(self.output_details[0]['index'])[0]
                
                detections = []
                h_orig, w_orig = frame.shape[:2]
                
                for row in output:
                    conf = row[4] # IDX_CONF = 4
                    if conf > 0.5: # AppConfig::CONFIDENCE_THRESHOLD = 0.5
                        # IDX_X1=0, IDX_Y1=1, IDX_X2=2, IDX_Y2=3 (기규화된 좌표)
                        x1 = int(row[0] * w_orig)
                        y1 = int(row[1] * h_orig)
                        x2 = int(row[2] * w_orig)
                        y2 = int(row[3] * h_orig)
                        bw, bh = x2 - x1, y2 - y1
                        
                        # Pose (IDX_KPT_START = 6)
                        kpts = []
                        for i in range(17):
                            # x, y, v 순서 (v는 conf이나 여기서는 x, y만 사용)
                            kx = int(row[6 + i*3] * w_orig)
                            ky = int(row[6 + i*3 + 1] * h_orig)
                            kpts.append((kx, ky))
                        
                        detections.append({'bbox': (x1, y1, bw, bh), 'kpts': kpts})

                latency = (time.time() - t0) * 1000
                self.result_signal.emit(detections, latency)

            except Exception as e:
                print("[AI ERROR]", e)

            self.msleep(1)


# =========================
# Main App
# =========================
class App(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("🔥 TFLite 멀티 소스 + 낙상 감지 테스트 앱")
        self.resize(1100, 850)

        self.cap = None
        self.ai_thread = None
        self.detections = []
        self.static_frame = None
        self.source_path = ""
        
        # 낙상 감지 시뮬레이터 초기화
        self.fall_sim = FallRuleSimulator()

        # UI Components
        self.label = QLabel("소스를 선택하고 [소스 시작]을 누르세요")
        self.label.setAlignment(Qt.AlignCenter)
        self.label.setStyleSheet("background: #000; color: #888; font-size: 18px;")

        self.btn_start = QPushButton("▶ 소스 시작")
        self.btn_start.clicked.connect(self.start_source)
        
        self.btn_stop = QPushButton("⏹ 정지")
        self.btn_stop.clicked.connect(self.stop_source)

        self.check_fall = QCheckBox("⚠️ 낙상 감지 활성화")
        self.check_fall.setChecked(True)
        self.check_fall.setStyleSheet("color: #ff5555; font-weight: bold;")

        self.status = QLabel("Latency: 0ms | Objects: 0")
        
        self.combo_mode = QComboBox()
        self.combo_mode.addItems(["📷 실시간 카메라 (DSHOW)", "📽️ 동영상 파일 (*.mp4, *.avi)", "🖼️ 단일 이미지 (*.jpg, *.png)"])
        self.combo_mode.currentIndexChanged.connect(self.on_mode_changed)

        self.spin_cam = QSpinBox()
        self.spin_cam.setRange(0, 10)
        self.spin_cam.setValue(1) 

        self.btn_file = QPushButton("📁 파일 선택...")
        self.btn_file.clicked.connect(self.select_file)
        self.btn_file.setVisible(False)

        # Layout
        layout = QVBoxLayout()
        layout.addWidget(self.label, 1)
        
        ctrl_lay = QHBoxLayout()
        ctrl_lay.addWidget(QLabel("모드:"))
        ctrl_lay.addWidget(self.combo_mode)
        ctrl_lay.addWidget(self.spin_cam)
        ctrl_lay.addWidget(self.btn_file)
        ctrl_lay.addWidget(self.check_fall)
        ctrl_lay.addWidget(self.btn_start)
        ctrl_lay.addWidget(self.btn_stop)
        
        layout.addLayout(ctrl_lay)
        layout.addWidget(self.status)

        w = QWidget()
        w.setLayout(layout)
        self.setCentralWidget(w)

        self.timer = QTimer()
        self.timer.timeout.connect(self.update_frame)
        self.last_h = 480

        # [추가] 낙상 이벤트 관리 변수
        self.fall_capture_time = 0      # 낙상 발생 시점 기록 (3초 유지용)
        self.last_fall_detections = []  # 낙상 당시의 BBOX/KPT 데이터 저장
        self.is_fall_saving = False     # 한 번의 낙상에 한 장만 찍히도록 제어
        self.current_raw_frame = None   # 박스 그려지기 전의 깨끗한 원본 저장용

        if not os.path.exists("captures"):
            os.makedirs("captures")     # 저장 폴더가 없으면 생성

    def on_mode_changed(self, idx):
        is_cam = (idx == 0)
        self.spin_cam.setVisible(is_cam)
        self.btn_file.setVisible(not is_cam)
        self.stop_source()

    def select_file(self):
        mode = self.combo_mode.currentIndex()
        filter = "Video (*.mp4 *.avi *.mkv)" if mode == 1 else "Images (*.jpg *.png *.bmp)"
        path, _ = QFileDialog.getOpenFileName(self, "파일 선택", "", filter)
        if path:
            self.source_path = path
            self.status.setText(f"선택됨: {os.path.basename(path)}")

    def start_source(self):
        self.stop_source()
        mode = self.combo_mode.currentIndex()

        if mode == 0: # Camera
            idx = self.spin_cam.value()
            self.cap = cv2.VideoCapture(idx, cv2.CAP_DSHOW)
            if self.cap and self.cap.isOpened():
                self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
                self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
                self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
            else:
                self.status.setText("카메라 오픈 실패")
                return
        elif mode == 1: # Video
            if not self.source_path: self.select_file()
            if not self.source_path: return
            self.cap = cv2.VideoCapture(self.source_path)
            if not self.cap or not self.cap.isOpened():
                self.status.setText("비디오 오픈 실패")
                return
        elif mode == 2: # Image
            if not self.source_path: self.select_file()
            if not self.source_path: return
            img = cv2.imread(self.source_path)
            if img is not None:
                self.static_frame = img
            else:
                self.status.setText("이미지 로드 실패")
                return

        # Start AI Thread
        import os
        base_dir = os.path.dirname(os.path.abspath(__file__))
        model_path = os.path.join(base_dir, "yolo26n-pose_int8.tflite")
        if os.path.exists(model_path):
            self.ai_thread = TFLiteThread(model_path)
            self.ai_thread.result_signal.connect(self.on_result)
            self.ai_thread.start()
        else:
            self.status.setText("모델 파일을 찾을 수 없습니다!")

        self.timer.start(30)
        self.btn_start.setEnabled(False)

    def stop_source(self):
        self.timer.stop()
        if self.ai_thread:
            self.ai_thread.running = False
            self.ai_thread.wait()
            self.ai_thread = None
        if self.cap:
            self.cap.release()
            self.cap = None
        self.static_frame = None
        self.detections = []
        self.fall_sim.reset() # 이력 초기화
        self.btn_start.setEnabled(True)
        self.label.setPixmap(QPixmap()) 
        self.label.setText("정지됨")

    def update_frame(self):
        frame = None
        if self.static_frame is not None:
            frame = self.static_frame.copy()
        elif self.cap and self.cap.isOpened():
            ret, f = self.cap.read()
            if not ret:
                # Video Loop
                if self.combo_mode.currentIndex() == 1:
                    self.cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                    return
                return
            frame = f
        
        if frame is None: return
        self.last_h = frame.shape[0]

        # [추가] 박스 그리기 전의 원본 프레임을 별도로 복사해둠
        self.current_raw_frame = frame.copy() 
        self.last_h = frame.shape[0]

        # AI 추론 요청
        if self.ai_thread:
            self.ai_thread.set_frame(frame)

        # 그리기 및 출력
        drawn = self.draw_detections(frame)
        self.show_frame(drawn)

    def show_frame(self, frame):
        h, w, ch = frame.shape
        # 비율 유지를 위한 크기 조정
        disp_w = self.label.width()
        disp_h = self.label.height()
        
        # NumPy array -> QImage
        img = QImage(frame.data, w, h, ch * w, QImage.Format_BGR888).copy()
        pix = QPixmap.fromImage(img).scaled(disp_w, disp_h, Qt.KeepAspectRatio, Qt.SmoothTransformation)
        self.label.setPixmap(pix)

    def on_result(self, detections, latency):
        if not self.label.pixmap(): return 
        
        processed = []
        any_new_fall = False

        for det in detections:
            bbox = det['bbox']
            kpts = det['kpts']
            
            is_falling = False
            state = "UNKNOWN"
            
            if self.check_fall.isChecked():
                res = self.fall_sim.check_fall(bbox, kpts, img_h=self.last_h)
                is_falling = res.is_falling
                state = res.current_state
            
            det['is_falling'] = is_falling
            det['state'] = state
            
            if is_falling:
                any_new_fall = True 

            processed.append(det)
        
        # --- [로직 추가] 낙상 이벤트 처리 ---
        if any_new_fall:
            self.fall_capture_time = time.time() # 3초 타이머 시작
            self.last_fall_detections = [d for d in processed if d['is_falling']]
            
            if not self.is_fall_saving:
                self.save_fall_frame() # 스크린샷 저장 실행
                self.is_fall_saving = True
        else:
            # 3초가 지나면 다시 저장 가능 상태로 해제
            if time.time() - self.fall_capture_time > 3.0:
                self.is_fall_saving = False

        self.status.setText(f"Latency: {latency:.1f} ms | Objects: {len(detections)}")
        self.detections = processed

    def draw_detections(self, frame):
        canvas = frame.copy()
        current_time = time.time() # 시간 변수 선언
        
        # 3초 유지 여부 확인
        is_locked = (current_time - self.fall_capture_time < 3.0)
        
        # 1. 만약 3초 이내라면 저장된 낙상 BBOX를 붉은색으로 강제 표시
        if is_locked and self.last_fall_detections:
            cv2.putText(canvas, "!!! FALL EVENT LOCKED !!!", (50, 80), 
                        cv2.FONT_HERSHEY_DUPLEX, 1.5, (0, 0, 255), 3)
            
            for det in self.last_fall_detections:
                x, y, w, h = det['bbox']
                cv2.rectangle(canvas, (x, y), (x+w, y+h), (0, 0, 255), 4) # 빨간 박스
                cv2.putText(canvas, "FALLING", (x, y-10), 
                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)

        # 2. 실시간 모든 감지 객체 그리기
        for det in self.detections:
            # 위에서 이미 그린 낙상 객체와 중복 표시 방지 (is_locked일 때만)
            if is_locked and det.get('is_falling'): continue
            
            x, y, w, h = det['bbox']
            is_falling = det.get('is_falling', False)
            state = det.get('state', 'STANDING')
            
            color = (0, 0, 255) if is_falling else (0, 255, 0)
            cv2.rectangle(canvas, (x, y), (x+w, y+h), color, 2)
            cv2.putText(canvas, state, (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
            
            # 키포인트/스켈레톤 그리기
            kpts = det['kpts']
            if hasattr(self.ai_thread, 'SKELETON'):
                sk_color = (0, 165, 255) if is_falling else (255, 255, 0)
                for p1, p2 in self.ai_thread.SKELETON:
                    if kpts[p1][0] > 0 and kpts[p2][0] > 0:
                        cv2.line(canvas, kpts[p1], kpts[p2], sk_color, 2)
        
        return canvas

    def save_fall_frame(self):
        """[수정] 낙상 순간의 원본 이미지에 텍스트를 합성하여 파일로 저장"""
        if self.current_raw_frame is not None:
            from datetime import datetime
            now = datetime.now()
            now_str = now.strftime("%Y-%m-%d %H:%M:%S")
            file_name_str = now.strftime("%Y%m%d_%H%M%S")
            
            # 캡처용 복사본 생성 (원본 보존을 위해)
            save_img = self.current_raw_frame.copy()
            
            # 이미지 하단에 검은색 바를 그려 텍스트 가독성 높이기 (선택 사항)
            h, w = save_img.shape[:2]
            cv2.rectangle(save_img, (0, h - 50), (w, h), (0, 0, 0), -1)
            
            # 시간 및 경고 문구 합성
            info_text = f"FALL DETECTED - {now_str}"
            cv2.putText(save_img, info_text, (20, h - 15), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
            
            # 파일 저장
            file_path = f"captures/fall_{file_name_str}.jpg"
            cv2.imwrite(file_path, save_img)
            print(f">>> [ALERT] 낙상 캡처 저장 완료: {file_path}")
            

    def closeEvent(self, e):
        self.stop_source()
        e.accept()



if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = App()
    win.show()
    sys.exit(app.exec_())