#include "EventRecorder.h"
#include "../../config/AppConfig.h"
#include "../util/Logger.h" // Added include for Logger

#include <chrono>
// Removed #include <iostream> as it's replaced by Logger
#include <string> // Required for std::to_string

static const std::string TAG = "EventRecorder"; // Added TAG definition

EventRecorder::EventRecorder(CircularFrameBuffer &buffer,
                             IStreamTransmitter &transmitter,
                             int post_event_frames)
    : buffer_(buffer), transmitter_(transmitter),
      post_event_frames_(post_event_frames) {
  LOG_INFO(TAG, "초기화 완료 (post-event: " + std::to_string(post_event_frames_) + " 프레임)");
}

EventRecorder::~EventRecorder() {
  if (transmit_thread_.joinable()) {
    transmit_thread_.join();
  }
}

void EventRecorder::triggerEvent(int track_id) {
  // 1. [최적화] 쿨다운 검사: 동일 트랙에 대해 너무 빈번한 트리거 방지
  {
    std::lock_guard<std::mutex> lock(cooldown_mutex_);
    auto now = std::chrono::steady_clock::now();
    if (track_last_event_time_.count(track_id)) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         now - track_last_event_time_[track_id])
                         .count();
      if (elapsed < AppConfig::EVENT_COOLDOWN_SEC) {
        // 아직 쿨다운 중이면 무시
        LOG_DEBUG(TAG, "트랙 ID " + std::to_string(track_id) + " 쿨다운 중. 이벤트 무시.");
        return;
      }
    }
    track_last_event_time_[track_id] = now;
  }

  // 트리거 시점의 pre-event snapshot을 즉시 확보 (시점 정확도 보장)
  std::vector<FramePtr> snapshot = buffer_.snapshot();

  if (is_recording_) {
    // 2. [안정성] 대기 큐 인원 제한 (메모리 폭주 방지)
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (pending_events_.size() >= AppConfig::EVENT_RECORDER_MAX_PENDING) {
      LOG_WARN(TAG, "경고: 대기 큐 가득 참. 이벤트 드랍 (ID: " + std::to_string(track_id) + ")");
      return;
    }

    // 녹화 중이면 큐에 적재 (이벤트 누락 방지)
    pending_events_.push({track_id, std::move(snapshot)});
    LOG_INFO(TAG, "이벤트 큐 적재 (ID: " + std::to_string(track_id) + 
                  ", 대기 중: " + std::to_string(pending_events_.size()) + "건)");
    return;
  }

  // 즉시 녹화 시작
  startRecording(track_id, std::move(snapshot));
}

void EventRecorder::startRecording(int track_id,
                                   std::vector<FramePtr> pre_snapshot) {
  is_recording_ = true;
  current_track_id_ = track_id;
  pre_frames_ = std::move(pre_snapshot);

  LOG_INFO(TAG, "이벤트 트리거됨 (ID: " + std::to_string(track_id) + 
                "), pre-event: " + std::to_string(pre_frames_.size()) + " 프레임");

  // post-event 수집 초기화
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    post_frames_.clear();
    post_frames_.reserve(post_event_frames_);
  }
  post_count_ = 0;
}

void EventRecorder::feedFrame(const cv::Mat &frame) {
  if (!is_recording_)
    return;

  // post-event 프레임 충분히 수집되면 전송 시작
  if (post_count_ >= post_event_frames_) {
    is_recording_ = false;

    // [최적화] 전송할 데이터 로컬 스냅샷 확보 (락 시간 최소화)
    std::vector<FramePtr> pre_to_send;
    std::vector<FramePtr> post_to_send;
    int track_id = -1;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      pre_to_send = std::move(pre_frames_);
      post_to_send = std::move(post_frames_);
      track_id = current_track_id_;
    }

    // [중요] 이전 전송 스레드를 기다리지(join) 않습니다. 
    // 여기서 기다리면 카메라 루프가 멈춰버려 libcamerasrcAssertion 오류가 발생합니다.
    // 기존 스레드 자원을 해제하기 위해 joinable 체크 후 적절히 처리하거나, 
    // 여기서는 간단히 개별 스레드를 분리(detach)하여 비동기로 처리합니다.
    if (transmit_thread_.joinable()) {
      transmit_thread_.detach(); // 기존 스레드가 있다면 분리 (자체 종료 유도)
    }

    transmit_thread_ = std::thread([this, pre_to_send, post_to_send, track_id]() {
      this->recordingWorker(pre_to_send, post_to_send, track_id);
    });

    return;
  }

  // post-event 프레임 수집
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    post_frames_.push_back(std::make_shared<cv::Mat>(frame.clone()));
  }
  post_count_++;
}

bool EventRecorder::isRecording() const { return is_recording_; }

int EventRecorder::getPendingEventCount() const {
  std::lock_guard<std::mutex> lock(pending_mutex_);
  return static_cast<int>(pending_events_.size());
}

void EventRecorder::recordingWorker(std::vector<FramePtr> pre,
                                    std::vector<FramePtr> post, int track_id) {
  LOG_DEBUG(TAG, "전송 작업 시작 (Pre: " + std::to_string(pre.size()) + 
                 ", Post: " + std::to_string(post.size()) + ", ID: " + std::to_string(track_id) + ")");

  // 전체 클립 병합
  std::vector<FramePtr> full_clip;
  full_clip.reserve(pre.size() + post.size());
  full_clip.insert(full_clip.end(), pre.begin(), pre.end());
  full_clip.insert(full_clip.end(), post.begin(), post.end());

  // IStreamTransmitter에게 전송 위임
  transmitter_.transmit(full_clip, track_id);

  LOG_INFO(TAG, "ID " + std::to_string(track_id) + " 전송 완료.");

  // 전송 완료 후 큐에 대기 중인 다음 이벤트 처리
  processNextEvent();
}

void EventRecorder::processNextEvent() {
  std::lock_guard<std::mutex> lock(pending_mutex_);
  if (pending_events_.empty()) {
    return;
  }

  // 큐에서 다음 이벤트를 꺼냄
  PendingEvent next = std::move(pending_events_.front());
  pending_events_.pop();

  LOG_DEBUG(TAG, "대기 이벤트 처리 시작 (ID: " + std::to_string(next.track_id) + 
                 ", 남은 대기: " + std::to_string(pending_events_.size()) + "건)");

  // 즉시 녹화 시작 (pre-event는 트리거 시점에 이미 확보됨)
  startRecording(next.track_id, std::move(next.pre_frames));
}
