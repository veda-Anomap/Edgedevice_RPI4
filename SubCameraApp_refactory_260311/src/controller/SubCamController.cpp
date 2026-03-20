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

#include "../util/Logger.h" // Added for LOG_* macros
#include <nlohmann/json.hpp>
#include <thread>
#include <string> // Required for std::to_string and TAG

static const std::string TAG = "Controller"; // Define TAG for logging

SubCamController::SubCamController() : is_running_(false) {
  LOG_INFO(TAG, "컴포넌트 초기화 중...");

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
  // 톤업(ToneMapping) 알고리즘 적용 (CPU 다운샘플링 최적화 패치 적용됨)
  low_light_enhancer_ = std::make_unique<ToneMappingEnhancer>();

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
  
  // 'q' 키 입력을 통한 전체 종료 연동을 위해 플래그 주입
  stream_pipeline_->setControllerRunningFlag(&is_running_);

  // 10. 시스템 자원 모니터 생성
  resource_monitor_ = std::make_unique<SystemResourceMonitor>();

  // 11. [통합] Edge Device 브릿지 모듈 생성 및 시작
  edge_bridge_ = std::make_unique<EdgeBridgeModule>();
  edge_bridge_->setNetworkSender(sender_ptr_); // [DIP] 네트워크 송신기 주입

  if (!edge_bridge_->start()) {
    LOG_ERROR(TAG, "EdgeBridge 시작 실패 (UART/설정 확인 필요). 카메라 기능은 정상 동작합니다.");
  }

  LOG_INFO(TAG, "컴포넌트 초기화 완료 (Phase 2 + EdgeBridge 포함).");
}

SubCamController::~SubCamController() { stop(); }

void SubCamController::run(std::atomic<bool> *stop_flag) {
  is_running_ = true;
  LOG_INFO(TAG, "시스템 시작됨. 서버 대기 중...");

  // 네트워크 시작 (명령 수신 콜백 등록) - [DIP] 바디 인자 추가
  command_receiver_->start([this](const std::string &ip, int port, const std::string &body) {
    this->handleServerCommand(ip, port, body);
  });

  // 시스템 모니터링 스레드 시작
  monitor_thread_ =
      std::thread(&SubCamController::monitorDeviceStatusLoop, this);

  // 메인 루프
  while (is_running_) {
    if (stop_flag && *stop_flag) {
      LOG_INFO(TAG, "종료 신호 감지!");
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  stop();
}

void SubCamController::stop() {
  // 중복 호출되더라도 안전하게 각 컴포넌트의 stop()을 호출하고 스레드를 join함
  bool was_running = is_running_.exchange(false);
  
  LOG_INFO(TAG, "시스템 종료 절차 시작...");

  if (edge_bridge_)
    edge_bridge_->stop();
  if (stream_pipeline_)
    stream_pipeline_->stopStreaming();
  if (command_receiver_)
    command_receiver_->stop();

  // 모니터링 스레드가 살아있다면 확실히 join
  if (monitor_thread_.joinable()) {
    LOG_INFO(TAG, "모니터링 스레드 종료 대기 중...");
    monitor_thread_.join();
  }

  LOG_INFO(TAG, "모든 시스템 컴포넌트 정리 완료.");
}

void SubCamController::handleServerCommand(const std::string &server_ip,
                                           int udp_port,
                                           const std::string &body) {
  LOG_INFO(TAG, "========================================");
  LOG_INFO(TAG, "명령 수신!");
  LOG_INFO(TAG, " -> 발신지: " + server_ip + ":" + std::to_string(udp_port));
  LOG_INFO(TAG, " -> 명령 내용: " + body);
  LOG_INFO(TAG, "========================================");

  // 1. 스트리밍 시작 명령 조건 (START_STREAM 등 문자열 포함 시)
  if (body.find("START_STREAM:") != std::string::npos) {
    stream_transmitter_->setTarget(server_ip, udp_port + 1);
    stream_pipeline_->startStreaming(server_ip, udp_port);
  }
  
  // 2. [DIP] 장치(모터) 제어 명령 분기 (MessageType::DEVICE 관련 JSON일 경우)
  if (body.find("\"motor\"") != std::string::npos || body.find("\"cmd\"") != std::string::npos) {
    if (edge_bridge_) {
      // JSON 파싱 후 "motor" 키값만 전달하는 로직 (Bridge 내부 재사용 가능)
      try {
        auto j = nlohmann::json::parse(body);
        std::string motor_cmd = "";

        // get<std::string>()을 사용하여 명확하게 추출
        if (j.contains("motor") && j.at("motor").is_string()) {
          motor_cmd = j.at("motor").get<std::string>();
        } else if (j.contains("cmd") && j.at("cmd").is_string()) {
          motor_cmd = j.at("cmd").get<std::string>();
        }

        if (!motor_cmd.empty()) {
          LOG_INFO(TAG, "STM32로 릴레이 시도: " + motor_cmd);
          
          // 성공 여부 로그 추가
          bool res = edge_bridge_->handleMotorCmd(motor_cmd);
          if (!res) {
            LOG_ERROR(TAG, "EdgeBridge 전달 실패 (bridge_ 객체 확인 필요)");
          } 
      }
    }
    catch (...) {
        LOG_ERROR(TAG, "JSON 파싱 중 오류 발생 또는 'motor'/'cmd' 키 없음.");
    }
    } // if (edge_bridge_) 닫기
  } // if (body.find...) 닫기
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
