#ifndef SUBCAM_CONTROLLER_H
#define SUBCAM_CONTROLLER_H

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// 전방 선언 (DIP 원칙: 구체 클래스 대신 인터페이스에 의존)
class ICommandReceiver;
class INetworkSender;
class ICamera;
class IAiDetector;
class IImagePreprocessor;
class IImageEnhancer;
class IStreamTransmitter;
class StreamPipeline;
class FrameRenderer;
class FrameSaver;
class CircularFrameBuffer;
class EventRecorder;
class IResourceMonitor;
class EdgeBridgeModule;

// =============================================
// SubCamController: 시스템 총괄 컨트롤러 (DIP)
// Composition Root: 모든 구체 클래스를 생성하고
// 인터페이스로 주입하는 유일한 장소
// =============================================

class SubCamController {
public:
  SubCamController();
  ~SubCamController();

  // 시스템 가동 (메인 루프)
  void run(std::atomic<bool> *stop_flag = nullptr);

  // 시스템 종료
  void stop();

private:
  void handleServerCommand(const std::string &server_ip, int udp_port, const std::string &body);
  void monitorDeviceStatusLoop();

  // --- 인터페이스 기반 컴포넌트 ---
  std::unique_ptr<ICommandReceiver> command_receiver_;
  std::unique_ptr<ICamera> camera_;
  std::unique_ptr<IAiDetector> detector_;
  std::unique_ptr<IImagePreprocessor> preprocessor_;
  std::unique_ptr<IImageEnhancer> low_light_enhancer_;
  std::unique_ptr<IStreamTransmitter> stream_transmitter_;

  // --- 구체 유틸리티 ---
  std::unique_ptr<FrameRenderer> renderer_;
  std::unique_ptr<FrameSaver> saver_;
  std::unique_ptr<CircularFrameBuffer> frame_buffer_;
  std::unique_ptr<EventRecorder> event_recorder_;
  std::unique_ptr<StreamPipeline> stream_pipeline_;

  // NetworkFacade → 양쪽 인터페이스 참조용 원시 포인터
  INetworkSender *sender_ptr_ = nullptr;

  std::unique_ptr<IResourceMonitor> resource_monitor_;
  std::thread monitor_thread_;

  // [통합] Edge Device (STM32 UART 브릿지) 모듈
  std::unique_ptr<EdgeBridgeModule> edge_bridge_;

  std::atomic<bool> is_running_;
};

#endif // SUBCAM_CONTROLLER_H
