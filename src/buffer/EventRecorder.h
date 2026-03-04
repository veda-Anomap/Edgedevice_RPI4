#ifndef EVENT_RECORDER_H
#define EVENT_RECORDER_H

#include "../transmitter/IStreamTransmitter.h"
#include "CircularFrameBuffer.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <queue>
#include <thread>
#include <vector>

// ================================================================
// EventRecorder: Pre/Post 이벤트 클립 수집기 (SRP)
//
// 동작 흐름:
//   1. triggerEvent() 호출 시 → CircularFrameBuffer에서 과거 N초 snapshot
//   2. 이후 M초 동안 feedFrame()으로 post-event 프레임 수집
//   3. 클립 완성 → IStreamTransmitter::transmit()으로 서버에 전송
//
// 중복 트리거 방지: is_recording_ 플래그로 제어
// ================================================================

class EventRecorder {
public:
  // 의존성 주입 (DIP 원칙)
  EventRecorder(CircularFrameBuffer &buffer, IStreamTransmitter &transmitter,
                int post_event_frames);
  ~EventRecorder();

  // 이벤트 트리거 (AI 스레드에서 호출)
  // 녹화 중이면 큐에 적재, 아니면 즉시 녹화 시작
  void triggerEvent(int track_id);

  // post-event 프레임 공급 (카메라 스레드에서 호출)
  void feedFrame(const cv::Mat &frame);

  bool isRecording() const;

private:
  // 대기 중인 이벤트 정보
  struct PendingEvent {
    int track_id;
    std::vector<FramePtr> pre_frames; // 트리거 시점의 pre-event snapshot
  };

  void startRecording(int track_id, std::vector<FramePtr> pre_snapshot);
  void recordingWorker(std::vector<FramePtr> pre, std::vector<FramePtr> post,
                       int track_id);
  void processNextEvent(); // 큐에서 다음 이벤트를 꺼내 녹화 시작

  CircularFrameBuffer &buffer_;
  IStreamTransmitter &transmitter_;
  int post_event_frames_;

  std::atomic<bool> is_recording_{false};
  int current_track_id_ = -1;

  // pre-event 프레임 (snapshot 결과)
  std::vector<FramePtr> pre_frames_;

  // post-event 프레임 수집
  std::vector<FramePtr> post_frames_;
  std::mutex post_mutex_;
  std::atomic<int> post_count_{0};

  // 대기 이벤트 큐
  std::queue<PendingEvent> pending_events_;
  std::mutex pending_mutex_;

  // 전송 스레드
  std::thread transmit_thread_;
};

#endif // EVENT_RECORDER_H
