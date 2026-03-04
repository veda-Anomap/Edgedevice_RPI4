#include "StreamPipeline.h"
#include "../../config/AppConfig.h"

#include <chrono>
#include <iostream>

StreamPipeline::StreamPipeline(ICamera &camera, IAiDetector &detector,
                               INetworkSender &sender, FrameRenderer &renderer,
                               FrameSaver &saver,
                               CircularFrameBuffer &frame_buffer,
                               EventRecorder &event_recorder)
    : camera_(camera), detector_(detector), sender_(sender),
      renderer_(renderer), saver_(saver), frame_buffer_(frame_buffer),
      event_recorder_(event_recorder), frame_queue_(1) {
  std::cout << "[StreamPipeline] 의존성 주입 완료 (버퍼 + 이벤트 녹화 포함)."
            << std::endl;
}

StreamPipeline::~StreamPipeline() { stopStreaming(); }

void StreamPipeline::startStreaming(const std::string &target_ip,
                                    int target_port) {
  if (is_streaming_) {
    std::cout << "[StreamPipeline] 이미 스트리밍 중. 재시작합니다..."
              << std::endl;
    stopStreaming();
  }

  std::cout << "[StreamPipeline] AI 스트림 시작: " << target_ip << ":"
            << target_port << std::endl;

  // 카메라 파이프라인 구성
  std::string capture_pipeline =
      "libcamerasrc ! "
      "video/x-raw, width=" +
      std::to_string(AppConfig::CAPTURE_WIDTH) +
      ", height=" + std::to_string(AppConfig::CAPTURE_HEIGHT) +
      ", framerate=" + std::to_string(AppConfig::FPS_TARGET) +
      "/1 ! "
      "v4l2convert ! "
      "videoscale ! "
      "video/x-raw, width=" +
      std::to_string(AppConfig::FRAME_WIDTH) +
      ", height=" + std::to_string(AppConfig::FRAME_HEIGHT) +
      " ! "
      "videoconvert ! "
      "video/x-raw, format=BGR ! "
      "queue max-size-buffers=3 leaky=downstream ! "
      "appsink drop=true max-buffers=1 sync=false";

  if (!camera_.open(capture_pipeline)) {
    std::cerr << "[StreamPipeline] 카메라 파이프라인 열기 실패!" << std::endl;
    return;
  }

  // 네트워크 송출 파이프라인
  std::string send_pipeline =
      "appsrc ! video/x-raw, format=BGR, width=" +
      std::to_string(AppConfig::FRAME_WIDTH) +
      ", height=" + std::to_string(AppConfig::FRAME_HEIGHT) +
      ", framerate=" + std::to_string(AppConfig::FPS_TARGET) +
      "/1 ! "
      "queue max-size-buffers=30 ! "
      "videoconvert ! video/x-raw, format=I420 ! "
      "x264enc tune=zerolatency bitrate=" +
      std::to_string(AppConfig::BITRATE) +
      " speed-preset=ultrafast ! rtph264pay ! "
      "udpsink host=" +
      target_ip + " port=" + std::to_string(target_port) +
      " sync=false async=false";

  network_writer_.open(
      send_pipeline, cv::CAP_GSTREAMER, 0, AppConfig::FPS_TARGET,
      cv::Size(AppConfig::FRAME_WIDTH, AppConfig::FRAME_HEIGHT));

  if (!network_writer_.isOpened()) {
    std::cerr << "[StreamPipeline] 네트워크 쓰기 열기 실패!" << std::endl;
    camera_.release();
    return;
  }

  // AI 모델 초기화
  if (!detector_.initialize()) {
    std::cerr << "[StreamPipeline] AI 모델 초기화 실패!" << std::endl;
    camera_.release();
    network_writer_.release();
    return;
  }

  is_streaming_ = true;

  // 스레드 시작
  ai_thread_ = std::thread(&StreamPipeline::aiWorkerLoop, this);
  camera_thread_ = std::thread(&StreamPipeline::cameraLoop, this);

  std::cout << "[StreamPipeline] 스트리밍 및 AI 스레드 시작됨." << std::endl;
}

void StreamPipeline::stopStreaming() {
  std::lock_guard<std::mutex> stop_lock(stop_mutex_);

  if (!is_streaming_)
    return;

  is_streaming_ = false;
  std::cout << "[StreamPipeline] 스트리밍 중지 시작..." << std::endl;

  // 1. AI 스레드를 깨움 (ThreadSafeQueue 알림)
  frame_queue_.notify_all();

  // 2. 카메라 스레드가 read() 루프를 빠져나올 시간을 확보 (최대 ~33ms)
  //    release()를 먼저 호출하면 libcamerasrc 내부 RequestWrap 큐 레이스 발생
  if (camera_thread_.joinable())
    camera_thread_.join();
  if (ai_thread_.joinable())
    ai_thread_.join();

  // 3. 스레드가 모두 종료된 후 안전하게 I/O 리소스 해제
  camera_.release();
  if (network_writer_.isOpened())
    network_writer_.release();

  // 4. 큐 및 버퍼 정리
  frame_queue_.clear();

  std::cout << "[StreamPipeline] 스트리밍 중지 완료." << std::endl;
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
      std::cout << "[AI] 대기 중 스트리밍 중지 감지" << std::endl;
      break;
    }

    if (target_frame.empty()) {
      std::cout << "[AI] 경고: 빈 프레임 수령" << std::endl;
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
    std::cout << "[AI Cycle] 실행 시간: " << elapsed_ms << " ms" << std::endl;
  }
}

// ================================================================
// 카메라 루프: 캡처 → 버퍼 push → AI 공유 → 시각화 → 네트워크 송출
// ================================================================
void StreamPipeline::cameraLoop() {
  cv::Mat frame;
  cv::namedWindow("RPi_AI_Monitor", cv::WINDOW_AUTOSIZE);

  while (is_streaming_) {
    if (!camera_.read(frame) || frame.empty()) {
      if (!is_streaming_)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // read() 성공 후에도 종료 신호 확인 (release 전에 루프 탈출 보장)
    if (!is_streaming_)
      break;

    // [최적화] 샘플링 레이트 계산
    static int global_frame_idx = 0;
    int sampling_step = AppConfig::FPS_TARGET / AppConfig::EVENT_SAMPLING_FPS;
    if (sampling_step < 1)
      sampling_step = 1;

    // [Phase 2] 샘플링된 프레임만 버퍼 및 레코더에 전달 (메모리 사용량 및 복사
    // 부하 감소)
    if (global_frame_idx % sampling_step == 0) {
      // 순환 버퍼 (pre-event)
      frame_buffer_.push(frame);

      // 이벤트 레코더 (post-event)
      // feedFrame 내부에서는 이미 count를 기반으로 중복 샘플링 방지 로직이
      // 있으나 여기서 한 번 더 걸러주면 feedFrame 호출 부하도 줄어듦
      event_recorder_.feedFrame(frame);
    }
    global_frame_idx++;

    // AI 스레드로 프레임 전달 (최적화: clone 제거, 얕은 복사로 전달)
    // AI 스레드는 최신 프레임을 최대한 빨리 처리해야 하므로 샘플링 없이 전달
    frame_queue_.push(frame);

    // 시각화용 복사본 생성 (원본 보호를 위해 1회만 clone)
    cv::Mat display_frame = frame.clone();
    {
      std::lock_guard<std::mutex> lock(result_mutex_);
      renderer_.drawDetections(display_frame, shared_result_);
    }

    // 서버로 네트워크 송출
    network_writer_.write(display_frame);

    // 로컬 모니터 표시
    cv::imshow("RPi_AI_Monitor", display_frame);

    if (cv::waitKey(1) == 'q') {
      is_streaming_ = false;
      break;
    }
  }

  cv::destroyWindow("RPi_AI_Monitor");
}
