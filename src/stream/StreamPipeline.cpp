#include "StreamPipeline.h"
#include "../../config/AppConfig.h"
#include "../util/Logger.h"

#include <chrono>
#include <opencv2/opencv.hpp>
#include <string>

#ifdef __linux__
#include <pthread.h>
#endif

static const std::string TAG = "StreamPipeline";
StreamPipeline::StreamPipeline(ICamera &camera, IAiDetector &detector,
                               INetworkSender &sender, FrameRenderer &renderer,
                               FrameSaver &saver,
                               CircularFrameBuffer &frame_buffer,
                               EventRecorder &event_recorder,
                               IImageEnhancer *enhancer)
    : camera_(camera), detector_(detector), sender_(sender),
      renderer_(renderer), saver_(saver), frame_buffer_(frame_buffer),
      event_recorder_(event_recorder),
      processing_queue_(1), // [버퍼 안전] 용량 1: GStreamer 버퍼 보유량 최소화
      frame_queue_(1), enhancer_(enhancer),
      perf_ai_("PERF_AI", "Avg AI Latency", PerformanceMonitor::Unit::MS),
      perf_pre_("PERF_PRE", "Avg Pre-processing", PerformanceMonitor::Unit::US),
      perf_fps_cap_("FPS", "Capture", PerformanceMonitor::Unit::FPS),
      perf_fps_proc_("FPS", "Processing", PerformanceMonitor::Unit::FPS) {
  LOG_INFO(
      TAG,
      "의존성 주입 완료 (버퍼 + 이벤트 녹화 + 저조도 개선 플러그인 포함).");
}

StreamPipeline::~StreamPipeline() { stopStreaming(); }

void StreamPipeline::startStreaming(const std::string &target_ip,
                                    int target_port) {
  if (is_streaming_) {
    LOG_WARN(TAG, "이미 스트리밍 중. 재시작합니다...");
    stopStreaming();
    // [최적화] 하드웨어 자원(libcamera)이 완전히 정리될 시간을 확보
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  LOG_INFO(TAG,
           "AI 스트림 시작: " + target_ip + ":" + std::to_string(target_port));

  // 카메라 파이프라인 구성 (리팩토링 완료)
  std::string capture_pipeline = getCapturePipelineDesc();

  LOG_INFO(TAG, "카메라 파이프라인 초기화: " + capture_pipeline);
  if (!camera_.open(capture_pipeline)) {
    LOG_ERROR(TAG, "카메라 파이프라인 열기 실패! (Pipeline: " +
                       capture_pipeline + ")");
    LOG_ERROR(TAG, "힌트: libcamerasrc가 설치되어 있는지, 카메라 권한이 있는지 "
                   "확인하세요.");
    return;
  }

  // 네트워크 송출 파이프라인 구성 (리팩토링 완료)
  std::string send_pipeline = getSendPipelineDesc(target_ip, target_port);

  LOG_INFO(TAG, "네트워크 송출 파이프라인 초기화: " + send_pipeline);
  network_writer_.open(
      send_pipeline, cv::CAP_GSTREAMER, 0, AppConfig::FPS_TARGET,
      cv::Size(AppConfig::FRAME_WIDTH, AppConfig::FRAME_HEIGHT));

  LOG_INFO(TAG, "네트워크 송출 파이프라인 초기화: " + send_pipeline);
  network_writer_.open(
      send_pipeline, cv::CAP_GSTREAMER, 0, AppConfig::FPS_TARGET,
      cv::Size(AppConfig::FRAME_WIDTH, AppConfig::FRAME_HEIGHT));

  if (!network_writer_.isOpened()) {
    LOG_ERROR(TAG,
              "네트워크 쓰기 열기 실패! (Pipeline: " + send_pipeline + ")");
    camera_.release();
    return;
  }

  // AI 모델 초기화 (Optional Fail-safe 구조)
  if (!detector_.initialize()) {
    LOG_ERROR(TAG, "AI 모델 초기화 실패! 비디오 스트리밍만 진행합니다.");
    is_detector_ready_ = false;
  } else {
    is_detector_ready_ = true;
  }

  is_streaming_ = true;

  // 스레드 시작
  ai_thread_ = std::thread(&StreamPipeline::aiWorkerLoop, this);
  processing_thread_ = std::thread(&StreamPipeline::processingLoop, this);
  camera_thread_ = std::thread(&StreamPipeline::cameraLoop, this);

  // [최적화] 카메라 캡처 스레드의 우선순위를 상향 조정 (Throttling 및
  // RequestWrap 방지) GStreamer의 프레임 소진 부하를 AI 연산 부하로부터
  // 보호합니다.
#ifdef __linux__
  struct sched_param param;
  param.sched_priority = 10; // 적절히 높은 우선순위 부여
  if (pthread_setschedparam(camera_thread_.native_handle(), SCHED_RR, &param) !=
      0) {
    LOG_WARN(TAG,
             "카메라 스레드 우선순위 변경 실패 (root 권한이 필요할 수 있음).");
  }
#endif

  LOG_INFO(TAG, "스트리밍 및 AI 스레드 시작됨.");
}

void StreamPipeline::stopStreaming() {
  std::lock_guard<std::mutex> stop_lock(stop_mutex_);

  // 1. 상태 플래그 변경 및 AI 스레드 깨움
  is_streaming_ = false;
  LOG_INFO(TAG, "스트리밍 중지 시도 (리소스 선해제 방식)...");
  frame_queue_.notify_all();
  processing_queue_.notify_all(); // [안전] 처리 스레드도 깨워서 종료 유도

  // 2. I/O 리소스 선해제 시퀀스 조정 (libcamera InvokeMessage 레이스 방지)
  // [최적화] 즉시 release() 하지 않고, 잠시 대기하여 진행 중인 메시지 처리를
  // 유도합니다.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // 3. I/O 리소스 해제 (read() 대기 중인 카메라 스레드를 물리적으로 unblock
  // 시킵니다.)
  {
    std::lock_guard<std::mutex> lock(camera_mutex_);
    camera_.release();
    if (network_writer_.isOpened())
      network_writer_.release();
  }

  // 4. 각 스레드 종료 대기 (이미 unblock 되었으므로 대기 없이 종료됨)
  if (camera_thread_.joinable()) {
    LOG_DEBUG(TAG, "카메라 스레드 종료 완료 대기...");
    camera_thread_.join();
  }
  if (processing_thread_.joinable()) {
    LOG_DEBUG(TAG, "처리 스레드 종료 완료 대기...");
    processing_thread_.join();
  }
  if (ai_thread_.joinable()) {
    LOG_DEBUG(TAG, "AI 스레드 종료 완료 대기...");
    ai_thread_.join();
  }

  // 5. 나머지 큐 정리
  processing_queue_.clear();
  frame_queue_.clear();

  LOG_INFO(TAG, "모든 스트리밍 리소스 정리 완료.");
}

void StreamPipeline::setControllerRunningFlag(std::atomic<bool> *flag) {
  controller_running_flag_ = flag;
}

// ================================================================
// AI 작업 스레드: 감지 → 트래킹 → 낙상 판정 → 버퍼 이벤트 트리거
// ================================================================
void StreamPipeline::aiWorkerLoop() {
  int last_person_count = -1;

  while (is_streaming_) {
    cv::Mat target_frame;

    // ThreadSafeQueue를 사용하여 안전하게 프레임 수령 (중단 조건 포함)
    if (!frame_queue_.wait_and_pop(target_frame,
                                   [this] { return !is_streaming_; })) {
      LOG_DEBUG("AIWorker", "대기 중 스트리밍 중지 감지");
      break;
    }

    // AI 가 비정상일 경우 수령은 하지만 처리는 스킵 (Fail-safe)
    if (!is_detector_ready_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    if (target_frame.empty()) {
      LOG_WARN("AIWorker", "빈 프레임 수령");
      continue;
    }

    // 1. AI 감지 (PoseEstimator에 위임)
    auto start_time = std::chrono::steady_clock::now();
    auto raw_detections = detector_.detect(target_frame);

    DetectionResult local_result;

    for (auto &det : raw_detections) {
      // 2. 트래킹 ID 할당 (PersonTracker에 위임)
      cv::Point center(det.box.x + det.box.width / 2,
                       det.box.y + det.box.height / 2);
      det.track_id = tracker_.assignId(center);

      // 3. 낙상 판정 (FallDetector에 위임)
      det.is_falling =
          fall_detector_.checkFall(det.box, det.skeleton, det.track_id);

      // 4. 낙상 시: 이미지 저장 + JSON 전송 + 이벤트 녹화 트리거
      if (det.is_falling && !fall_detector_.isAlreadySaved(det.track_id)) {
        // 낙상 이미지 저장
        cv::Mat save_img = target_frame.clone();
        renderer_.drawSingleDetection(save_img, det);
        saver_.saveFallFrame(save_img, det.track_id);
        fall_detector_.markSaved(det.track_id);

        // JSON 알림 서버 전송
        std::string json_msg = "{\"status\": \"falling detected\", \"id\": " +
                               std::to_string(det.track_id) + "}";
        sender_.sendMessage(json_msg);

        // [Phase 2] 이벤트 녹화 트리거 (pre+post 클립 수집 시작)
        event_recorder_.triggerEvent(det.track_id);
      } else if (!det.is_falling) {
        fall_detector_.clearSavedFlag(det.track_id);
      }

      local_result.objects.push_back(det);
    }

    local_result.person_count = (int)local_result.objects.size();

    {
      std::lock_guard<std::mutex> lock(result_mutex_);
      shared_result_ = local_result;
    }

    // 인원 수 변경 시 서버에 알림
    if (local_result.person_count != last_person_count) {
      std::string json_msg =
          "{\"person_count\":" + std::to_string(local_result.person_count) +
          "}";
      sender_.sendMessage(json_msg);
      last_person_count = local_result.person_count;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          end_time - start_time)
                          .count();

    // [성능 모니터링] AI 지연시간 통계 (SOLID 위임)
    perf_ai_.update((double)elapsed_ms);
  }
}

// ================================================================
// 처리 루프: 전처리 → 샘플링 → AI전달 → 시각화 → 네트워크 송출
// ================================================================
void StreamPipeline::processingLoop() {
  if (AppConfig::ENABLE_DISPLAY) {
    cv::namedWindow("RPi_AI_Monitor", cv::WINDOW_AUTOSIZE);
  }

  while (is_streaming_) {
    cv::Mat frame;

    // 처리 큐에서 독립 프레임 수령 (deep copy 완료된 상태)
    if (!processing_queue_.wait_and_pop(frame,
                                        [this] { return !is_streaming_; })) {
      break;
    }

    // 1. 저조도 개선 (성능 측정 포함 - SOLID 위임)
    if (enhancer_) {
      auto pre_start = std::chrono::steady_clock::now();
      enhancer_->enhance(frame, frame);
      auto pre_end = std::chrono::steady_clock::now();
      auto pre_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                              pre_end - pre_start)
                              .count();
      perf_pre_.update((double)pre_duration);
    }

    // [FPS 측정] 처리 루프 FPS (SOLID 위임)
    static auto last_proc_time = std::chrono::steady_clock::now();
    auto proc_now = std::chrono::steady_clock::now();
    double proc_interval_ms =
        (double)std::chrono::duration_cast<std::chrono::milliseconds>(
            proc_now - last_proc_time)
            .count();
    if (proc_interval_ms > 0) {
      perf_fps_proc_.update(1000.0 / proc_interval_ms);
    }
    last_proc_time = proc_now;

    // 2. 샘플링 및 저장/녹화 (EventRecorder)
    static int global_frame_idx = 0;
    int sampling_step = AppConfig::FPS_TARGET / AppConfig::EVENT_SAMPLING_FPS;
    if (sampling_step < 1)
      sampling_step = 1;

    if (global_frame_idx % sampling_step == 0) {
      frame_buffer_.push(frame); // 내부에서 clone() 수행
      event_recorder_.feedFrame(frame);
    }

    // 3. AI 추론 전달 (사용자 요청: 1초당 1회 수준으로 최적화)
    // [보호] AI가 정상 초기화된 경우에만 큐에 삽입
    static int ai_feed_counter = 0;
    if (is_detector_ready_ && ++ai_feed_counter % (AppConfig::FPS_TARGET % 2) == 0) {
      frame_queue_.push(frame.clone());
    }

    // 4. 시각화: 독립 프레임이므로 직접 렌더링 가능 (clone 불필요)
    {
      std::lock_guard<std::mutex> lock(result_mutex_);
      renderer_.drawDetections(frame, shared_result_);
    }

    // 5. 서버로 네트워크 송출
    network_writer_.write(frame);

    // 6. 로컬 모니터 표시 및 종료 키 처리
    if (AppConfig::ENABLE_DISPLAY) {
      cv::imshow("RPi_AI_Monitor", frame);
      if (cv::waitKey(1) == 'q') {
        is_streaming_ = false;
        if (controller_running_flag_) {
          *controller_running_flag_ = false;
        }
      }
    }

    global_frame_idx++;
  }
}

// ================================================================
// 카메라 루프: 캡처 → 즉시 Deep Copy → 처리 큐 push (GStreamer 버퍼 즉시 반환)
//
// [핵심 설계 원칙] GStreamer 버퍼 수명 최소화
//   - raw_frame을 루프 내 지역변수로 선언 → 매 반복마다 소멸
//   - clone()으로 완전 독립 복사본 생성 → GStreamer 참조 즉시 해제
//   - 기존 Quad Buffer Pool 제거 → shallow copy 레이스 컨디션 제거
//
// 비용: clone() 1회/프레임 (640×480×3 ≈ 0.9MB, RPi4 ~0.3ms)
// 효과: libcamerasrc RequestWrap 버퍼 풀 고갈 방지 (3시간+ 안정성)
// ================================================================
void StreamPipeline::cameraLoop() {
  int consecutive_failures = 0; // [진단] 연속 read 실패 카운터

  while (is_streaming_) {
    cv::Mat raw_frame; // [핵심] 지역변수 → 루프 끝에서 자동 소멸
    auto read_start = std::chrono::steady_clock::now();
    bool read_success = false;

    // [중요] camera_.read()와 release() 간의 레이스를 방지하기 위해 락 내부에서
    // 호출합니다.
    {
      std::lock_guard<std::mutex> lock(camera_mutex_);
      if (!is_streaming_)
        break;
      read_success = camera_.read(raw_frame);
    }

    if (!read_success || raw_frame.empty()) {
      if (!is_streaming_)
        break;

      consecutive_failures++;

      // [진단] 연속 실패 시 단계별 경고
      if (consecutive_failures == 30) { // ~1초
        LOG_WARN("CAM_DIAG", "카메라 프레임 수신 불가 (연속 " +
                                 std::to_string(consecutive_failures) +
                                 "회 실패). 카메라 상태를 확인하세요.");
      } else if (consecutive_failures == 300) { // ~10초
        LOG_ERROR("CAM_DIAG", "⚠️ 카메라 장기 미응답 (연속 " +
                                  std::to_string(consecutive_failures) +
                                  "회 실패). 가능한 원인:");
        LOG_ERROR(
            "CAM_DIAG",
            "  1. 이전 프로세스가 카메라 점유 중 → sudo pkill -f SubCameraApp");
        LOG_ERROR("CAM_DIAG",
                  "  2. libcamerasrc 장치 잠금 → sudo fuser -k /dev/video0");
        LOG_ERROR("CAM_DIAG", "  3. 카메라 모듈 물리적 연결 불량");
      } else if (consecutive_failures % 900 == 0) { // ~30초마다 반복 알림
        LOG_ERROR("CAM_DIAG", "카메라 여전히 미응답 (" +
                                  std::to_string(consecutive_failures / 30) +
                                  "초 경과)");
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 프레임 수신 성공 → 카운터 리셋
    if (consecutive_failures > 0) {
      if (consecutive_failures >= 30) {
        LOG_INFO("CAM_DIAG", "✅ 카메라 프레임 수신 복구됨 (" +
                                 std::to_string(consecutive_failures) +
                                 "회 실패 후)");
      }
      consecutive_failures = 0;
    }

    // [핵심] GStreamer 버퍼와 완전 분리된 독립 프레임 생성
    // clone() 후 raw_frame은 루프 반복 시점에 소멸 → GStreamer 버퍼 즉시 반환
    // 기존 buffer_pool_ shallow copy 방식 대비:
    //   - 레이스 컨디션 제거 (processing 스레드와 데이터 공유 없음)
    //   - GStreamer 버퍼 보유 시간: ~33ms → ~1ms 미만으로 단축
    cv::Mat independent_frame = raw_frame.clone();

    // 처리 큐에 독립 프레임 전달 (move로 복사 비용 제거)
    processing_queue_.push(std::move(independent_frame));

    // [FPS 측정] 카메라 캡처 FPS (SOLID 위임)
    static auto last_cap_time = std::chrono::steady_clock::now();
    auto cap_now = std::chrono::steady_clock::now();
    double cap_interval_ms =
        (double)std::chrono::duration_cast<std::chrono::milliseconds>(
            cap_now - last_cap_time)
            .count();
    if (cap_interval_ms > 0) {
      perf_fps_cap_.update(1000.0 / cap_interval_ms);
    }
    last_cap_time = cap_now;

    auto read_end = std::chrono::steady_clock::now();
    auto read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             read_end - read_start)
                             .count();

    // 캡처 루프 지연 감시
    if (read_duration > 150) {
      LOG_WARN("PERF_CAP", "Camera Capture Stall Detected: " +
                               std::to_string(read_duration) + " ms");
    }
  }

  if (AppConfig::ENABLE_DISPLAY) {
    cv::destroyWindow("RPi_AI_Monitor");
  }
}

int StreamPipeline::getPendingEventCount() const {
  return event_recorder_.getPendingEventCount();
}

std::string StreamPipeline::getCapturePipelineDesc() const {
  // 카메라 파이프라인 구성 (RPi 4B 하드웨어 가속 최적화)
  // - libcamerasrc: Raspberry Pi 전용 최신 카메라 소스
  // - v4l2convert: ISP/VPU 하드웨어 스케일러 활용 (CPU 부하 대폭 감소)
  //
  // [핵심] 모든 queue에 leaky=downstream + max-size-buffers를 설정하여
  // 하류 요소(v4l2convert, videoflip, videoconvert) 지연 시 버퍼가
  // 무한 누적되어 libcamerasrc의 RequestWrap 풀을 고갈시키는 것을 방지.
  return "libcamerasrc ! "
         "video/x-raw, width=" +
         std::to_string(AppConfig::CAPTURE_WIDTH) + ", height=" + std::to_string(AppConfig::CAPTURE_HEIGHT) +
         " ! "
         "queue max-size-buffers=2 leaky=downstream ! "
         "v4l2convert ! "
         "video/x-raw, width=" +
         std::to_string(AppConfig::FRAME_WIDTH) + ", height=" + std::to_string(AppConfig::FRAME_HEIGHT) +
         " ! "
         "videoflip method=rotate-180 ! "
         "queue max-size-buffers=2 leaky=downstream ! "
         "videoconvert ! "
         "video/x-raw, format=BGR ! "
         "appsink drop=true max-buffers=1 sync=false";
}

std::string StreamPipeline::getSendPipelineDesc(const std::string &target_ip, int target_port) const {
  // [DIP 최적화] 전송 지연이 캡처 루프를 막지 않도록 queue 추가 및 x264enc zerolatency 설정
  return "appsrc is-live=true format=time do-timestamp=true ! "
         "video/x-raw, format=BGR, width=" +
         std::to_string(AppConfig::FRAME_WIDTH) + ", height=" + std::to_string(AppConfig::FRAME_HEIGHT) +
         ", framerate=" + std::to_string(AppConfig::FPS_TARGET) + "/1 ! "
         "queue max-size-buffers=3 leaky=downstream ! "
         "videoconvert ! video/x-raw, format=I420 ! "
         "x264enc tune=zerolatency bitrate=" +
         std::to_string(AppConfig::BITRATE) +
         " speed-preset=ultrafast ! rtph264pay config-interval=1 !"
         "udpsink host=" +
         target_ip + " port=" + std::to_string(target_port) + " sync=false async=false";
}
