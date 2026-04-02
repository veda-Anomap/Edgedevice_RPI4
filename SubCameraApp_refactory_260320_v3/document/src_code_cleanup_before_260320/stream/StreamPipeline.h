#ifndef STREAM_PIPELINE_H
#define STREAM_PIPELINE_H

#include "../ai/FallDetector.h"
#include "../ai/IAiDetector.h"
#include "../ai/PersonTracker.h"
#include "../buffer/CircularFrameBuffer.h"
#include "../buffer/EventRecorder.h"
#include "../imageprocessing/IImageEnhancer.h"
#include "../network/INetworkSender.h"
#include "../rendering/FrameRenderer.h"
#include "../util/FrameSaver.h"
#include "ICamera.h"
#include <opencv2/opencv.hpp>
#include "../util/PerformanceMonitor.h"
#include <string>
#include <vector>

#include "../util/ThreadSafeQueue.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// =============================================
// StreamPipeline: 얇은 오케스트레이터 (DIP + SRP)
// 생성자 주입된 의존성을 조합하여 스레드 관리만 담당
//
// Phase 2 통합:
//   - CircularFrameBuffer: 매 프레임 push (pre-event 버퍼링)
//   - EventRecorder: 낙상 감지 시 트리거 + post-event 수집
// =============================================

class StreamPipeline {
public:
  // 의존성 주입 (DIP 원칙)
  StreamPipeline(ICamera &camera, IAiDetector &detector, INetworkSender &sender,
                 FrameRenderer &renderer, FrameSaver &saver,
                 CircularFrameBuffer &frame_buffer,
                 EventRecorder &event_recorder,
                 IImageEnhancer *enhancer = nullptr);
  ~StreamPipeline();

  // 스트리밍 시작/중지
  void startStreaming(const std::string &target_ip, int target_port);
  void stopStreaming();
  void setControllerRunningFlag(std::atomic<bool>* flag);
  int getPendingEventCount() const;

private:
  void cameraLoop();
  void aiWorkerLoop();
  void processingLoop();

  // 주입된 의존성 참조 (소유하지 않음)
  ICamera &camera_;
  IAiDetector &detector_;
  INetworkSender &sender_;
  FrameRenderer &renderer_;
  FrameSaver &saver_;
  CircularFrameBuffer &frame_buffer_;
  EventRecorder &event_recorder_;
  IImageEnhancer *enhancer_;

  // 내부 소유 컴포넌트
  PersonTracker tracker_;
  FallDetector fall_detector_;

  // 스레드 관리
  std::atomic<bool> is_streaming_{false};
  std::atomic<bool> is_detector_ready_{false}; // [신규] AI 모델 가용 상태 (Fail-safe)
  std::thread camera_thread_;
  std::thread ai_thread_;
  std::thread processing_thread_;
  std::atomic<bool>* controller_running_flag_{nullptr};

  // 공유 데이터 (카메라 → 처리 스레드 → AI 스레드)
  ThreadSafeQueue<cv::Mat> processing_queue_;
  ThreadSafeQueue<cv::Mat> frame_queue_;

  DetectionResult shared_result_;
  std::mutex result_mutex_;
  std::mutex stop_mutex_; // stopStreaming 중복 실행 방지
  std::mutex camera_mutex_; // camera_.read()와 release() 동기화

  // GStreamer 네트워크 쓰기
  cv::VideoWriter network_writer_;

  // [최적화] 정합성 및 SOLID 준수를 위한 성능 모니터링
  PerformanceMonitor perf_ai_;
  PerformanceMonitor perf_pre_;
  PerformanceMonitor perf_fps_cap_;
  PerformanceMonitor perf_fps_proc_;

};

#endif // STREAM_PIPELINE_H
