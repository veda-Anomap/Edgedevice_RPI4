#include "EventRecorder.h"
#include "../../config/AppConfig.h"
#include <chrono>
#include <iostream>

EventRecorder::EventRecorder(CircularFrameBuffer &buffer,
                             IStreamTransmitter &transmitter,
                             int post_event_frames)
    : buffer_(buffer), transmitter_(transmitter),
      post_event_frames_(post_event_frames) {
  std::cout << "[EventRecorder] 초기화 완료 (post-event: " << post_event_frames_
            << " 프레임)" << std::endl;
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
      std::cerr << "[EventRecorder] 경고: 대기 큐 가득 참. 이벤트 드랍 (ID: "
                << track_id << ")" << std::endl;
      return;
    }

    // 녹화 중이면 큐에 적재 (이벤트 누락 방지)
    pending_events_.push({track_id, std::move(snapshot)});
    std::cout << "[EventRecorder] 이벤트 큐에 적재 (ID: " << track_id
              << ", 대기 중: " << pending_events_.size() << "건)" << std::endl;
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

  std::cout << "[EventRecorder] 이벤트 트리거됨 (ID: " << track_id
            << "), pre-event: " << pre_frames_.size() << " 프레임" << std::endl;

  // post-event 수집 초기화
  {
    std::lock_guard<std::mutex> lock(post_mutex_);
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

    // 전송할 데이터 로컬 스케줄링 (이동)
    std::vector<FramePtr> pre_to_send = std::move(pre_frames_);
    std::vector<FramePtr> post_to_send;
    {
      std::lock_guard<std::mutex> lock(post_mutex_);
      post_to_send = std::move(post_frames_);
    }

    if (transmit_thread_.joinable()) {
      transmit_thread_.join();
    }

    // 전송 스레드 시작
    transmit_thread_ = std::thread(&EventRecorder::recordingWorker, this,
                                   std::move(pre_to_send),
                                   std::move(post_to_send), current_track_id_);
    return;
  }

  // post-event 프레임 수집 (feedFrame은 이미 1 FPS로 호출되므로 매번 저장)
  {
    std::lock_guard<std::mutex> lock(post_mutex_);
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
  std::cout << "[EventRecorder] 전송 작업 시작 (Pre: " << pre.size()
            << ", Post: " << post.size() << ", ID: " << track_id << ")"
            << std::endl;

  // 전체 클립 병합
  std::vector<FramePtr> full_clip;
  full_clip.reserve(pre.size() + post.size());
  full_clip.insert(full_clip.end(), pre.begin(), pre.end());
  full_clip.insert(full_clip.end(), post.begin(), post.end());

  // IStreamTransmitter에게 전송 위임
  transmitter_.transmit(full_clip, track_id);

  std::cout << "[EventRecorder] ID " << track_id << " 전송 완료." << std::endl;

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

  std::cout << "[EventRecorder] 대기 이벤트 처리 시작 (ID: " << next.track_id
            << ", 남은 대기: " << pending_events_.size() << "건)" << std::endl;

  // 즉시 녹화 시작 (pre-event는 트리거 시점에 이미 확보됨)
  startRecording(next.track_id, std::move(next.pre_frames));
}
