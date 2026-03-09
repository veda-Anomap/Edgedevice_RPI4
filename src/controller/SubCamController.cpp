#include "SubCamController.h"

// 구체 클래스 포함 (구현 파일에서만 포함 — DIP 준수)
#include "../../config/AppConfig.h"
#include "../ai/PoseEstimator.h"
#include "../buffer/CircularFrameBuffer.h"
#include "../buffer/EventRecorder.h"
#include "../imageprocessing/ImagePreprocessor.h"
#include "../imageprocessing/LowLightEnhancer.h"
#include "../imageprocessing/AdvancedEnhancers.h"
#include "../network/NetworkFacade.h"
#include "../rendering/FrameRenderer.h"
#include "../stream/GStreamerCamera.h"
#include "../stream/StreamPipeline.h"
#include "../system/SystemResourceMonitor.h"
#include "../transmitter/ChunkedStreamTransmitter.h"
#include "../edge_device/EdgeBridgeModule.h"

#include <thread>

SubCamController::SubCamController() : is_running_(false) {
  std::cout << "[Controller] 컴포넌트 초기화 중..." << std::endl;

  // 1. NetworkFacade 생성 (INetworkSender + ICommandReceiver 동시 구현)
  auto facade = std::make_unique<NetworkFacade>();
  sender_ptr_ = facade.get();
  command_receiver_ = std::move(facade);

  // 2. 카메라 생성 (ICamera 구현체)
  camera_ = std::make_unique<GStreamerCamera>();

  // 3. [Phase 2] 이미지 전처리기 (IImagePreprocessor 구현체)
  preprocessor_ = std::make_unique<ImagePreprocessor>(
      AppConfig::MODEL_INPUT_SIZE, // 리사이즈 크기
      cv::COLOR_BGR2RGB,           // 색상 변환
      1.0 / 255.0,                 // 정규화 배율
      CV_32FC3                     // 출력 타입
  );

  // 4.5. [최적화] 저조도 개선기 초기화
  //low_light_enhancer_ = std::make_unique<LowLightEnhancer>();
  //Retinex 적용 
  low_light_enhancer_ = std::make_unique<RetinexEnhancer>();

  // 5. AI 감지기 (의존성 주입: IImagePreprocessor)
  detector_ =
      std::make_unique<PoseEstimator>(AppConfig::MODEL_PATH, *preprocessor_);

  // 5. 렌더러, 파일 저장 유틸리티
  renderer_ = std::make_unique<FrameRenderer>();
  saver_ = std::make_unique<FrameSaver>(AppConfig::CAPTURES_DIR);

  // 6. [Phase 2] 순환 프레임 버퍼 (pre-event: 1 FPS 샘플링하여 저장하도록
  // 최적화)
  size_t buffer_capacity =
      AppConfig::PRE_EVENT_SEC * AppConfig::EVENT_SAMPLING_FPS;
  frame_buffer_ = std::make_unique<CircularFrameBuffer>(buffer_capacity);

  // 7. [Phase 2] 청크 스트림 전송기 (IStreamTransmitter 구현체)
  // [수정] NetworkFacade(sender_ptr_)를 주입하여 기존 소켓을 재사용하도록 설정
  auto transmitter = std::make_unique<ChunkedStreamTransmitter>(
      *sender_ptr_, AppConfig::TRANSMITTER_MAX_RETRIES,
      AppConfig::JPEG_QUALITY);
  transmitter->setRunningFlag(&is_running_);
  stream_transmitter_ = std::move(transmitter);

  // 8. [Phase 2] 이벤트 녹화기 (순환 버퍼 + 전송기 주입)

  // feedFrame()은 EVENT_SAMPLING_FPS(1 FPS)로 호출되므로 post_frames도 동일
  // 기준
  // int post_frames = AppConfig::POST_EVENT_SEC * AppConfig::FPS_TARGET;
  int post_frames = AppConfig::POST_EVENT_SEC * AppConfig::EVENT_SAMPLING_FPS;
  event_recorder_ = std::make_unique<EventRecorder>(
      *frame_buffer_, *stream_transmitter_, post_frames);

  // 9. StreamPipeline 생성 (모든 의존성 주입)
  stream_pipeline_ = std::make_unique<StreamPipeline>(
      *camera_, *detector_, *sender_ptr_, *renderer_, *saver_, *frame_buffer_,
      *event_recorder_, low_light_enhancer_.get());

  // 10. 시스템 자원 모니터 생성
  resource_monitor_ = std::make_unique<SystemResourceMonitor>();

  // 11. [통합] Edge Device 브릿지 모듈 생성 및 시작
  // 수정 전: 하드코딩된 경로가 우선순위를 가짐
  // edge_bridge_ = std::make_unique<EdgeBridgeModule>("./config/edge_device_config.json");

  // 수정 후: 인자를 비우면 .h의 기본값을 참조함
  edge_bridge_ = std::make_unique<EdgeBridgeModule>();

  if (!edge_bridge_->start()) {
    std::cerr << "[Controller] EdgeBridge 시작 실패 (UART/설정 확인 필요). "
              << "카메라 기능은 정상 동작합니다." << std::endl;
    // 브릿지 실패해도 카메라 기능은 계속 동작
  }

  std::cout << "[Controller] 컴포넌트 초기화 완료 (Phase 2 + EdgeBridge 포함)." << std::endl;
}

SubCamController::~SubCamController() { stop(); }

void SubCamController::run(std::atomic<bool> *stop_flag) {
  is_running_ = true;
  std::cout << "[Controller] 시스템 시작됨. 서버 대기 중..." << std::endl;

  // 네트워크 시작 (명령 수신 콜백 등록)
  command_receiver_->start([this](const std::string &ip, int port) {
    this->handleServerCommand(ip, port);
  });

  // 시스템 모니터링 스레드 시작
  monitor_thread_ =
      std::thread(&SubCamController::monitorDeviceStatusLoop, this);

  // 메인 루프
  while (is_running_) {
    if (stop_flag && *stop_flag) {
      std::cout << "[Controller] 종료 신호 감지!" << std::endl;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  stop();
}

void SubCamController::stop() {
  if (is_running_) {
    is_running_ = false;
    std::cout << "[Controller] 시스템 종료 중..." << std::endl;

    if (edge_bridge_)
      edge_bridge_->stop();
    if (stream_pipeline_)
      stream_pipeline_->stopStreaming();
    if (command_receiver_)
      command_receiver_->stop();
  }
}

void SubCamController::handleServerCommand(const std::string &server_ip,
                                           int udp_port) {
  std::cout << "\n========================================" << std::endl;
  std::cout << "[Controller] 명령 수신!" << std::endl;
  std::cout << " -> 대상 서버: " << server_ip << std::endl;
  std::cout << " -> 대상 포트: " << udp_port << std::endl;
  std::cout << "========================================" << std::endl;

  // [Phase 2] 이벤트 클립 전송 대상 서버 설정
  stream_transmitter_->setTarget(server_ip, udp_port + 1); // 별도 포트 사용

  stream_pipeline_->startStreaming(server_ip, udp_port);
}

void SubCamController::monitorDeviceStatusLoop() {
  std::cout << "[SystemMonitor] 주기적 자원 모니터링 스레드 시작." << std::endl;

  while (is_running_) {
    if (resource_monitor_ && sender_ptr_) {
      DeviceStatus status = resource_monitor_->getStatus();

      // 추가: 큐 상태 정보 반영 (현재 대기 중인 녹화 개수)
      status.pending_event_count = stream_pipeline_->getPendingEventCount();

      sender_ptr_->sendDeviceStatus(status);
    }

    // 5초에 한 번씩 측정 및 전송
    for (int i = 0; i < 50 && is_running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}
