#include "ChunkedStreamTransmitter.h"
#include <chrono>
#include <iostream>
#include <thread>

ChunkedStreamTransmitter::ChunkedStreamTransmitter(INetworkSender &sender,
                                                   int max_retries,
                                                   int jpeg_quality)
    : sender_(sender), max_retries_(max_retries), jpeg_quality_(jpeg_quality) {
  std::cout << "[ChunkedTransmitter] 초기화 (Protocol 통합 모드, JPEG 품질: "
            << jpeg_quality_ << ")" << std::endl;
}

ChunkedStreamTransmitter::~ChunkedStreamTransmitter() {}

void ChunkedStreamTransmitter::setTarget(const std::string &host, int port) {
  host_ = host;
  port_ = port;
}

bool ChunkedStreamTransmitter::isConnected() const { return connected_; }

void ChunkedStreamTransmitter::transmit(const std::vector<FramePtr> &frames,
                                        int track_id) {
  std::lock_guard<std::mutex> lock(transmit_mutex_);

  if (frames.empty()) {
    std::cerr << "[ChunkedTransmitter] 전송할 프레임 없음." << std::endl;
    return;
  }

  int total_frames = static_cast<int>(frames.size());
  int success_count = 0;
  int fail_count = 0;

  // JPEG 인코딩 파라미터
  std::vector<int> encode_params = {cv::IMWRITE_JPEG_QUALITY, jpeg_quality_};

  // 타임스탬프 기준
  auto base_time = std::chrono::steady_clock::now();

  std::cout << "[ChunkedTransmitter] 전송 시작: " << total_frames
            << " 프레임 (ID: " << track_id << ", Protocol 통합)" << std::endl;

  for (int i = 0; i < total_frames; ++i) {
    if (!frames[i] || frames[i]->empty())
      continue;

    // JPEG 인코딩
    std::vector<uint8_t> jpeg_buffer;
    if (!cv::imencode(".jpg", *frames[i], jpeg_buffer, encode_params)) {
      std::cerr << "[ChunkedTransmitter] JPEG 인코딩 실패 (프레임 " << i << ")"
                << std::endl;
      fail_count++;
      continue;
    }

    // JSON 메타데이터 구성
    long long timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - base_time)
            .count();

    std::string metadata_json =
        "{\"track_id\":" + std::to_string(track_id) +
        ",\"frame_index\":" + std::to_string(i) +
        ",\"total_frames\":" + std::to_string(total_frames) +
        ",\"timestamp_ms\":" + std::to_string(timestamp_ms) +
        ",\"jpeg_size\":" + std::to_string(jpeg_buffer.size()) + "}";

    // 종료 신호 확인
    if (running_flag_ && !running_flag_->load()) {
      std::cout << "[ChunkedTransmitter] 시스템 종료 감지: 전송 중단"
                << std::endl;
      return;
    }

    // [핵심 변경] 기존 소켓을 재사용하는 NetworkFacade를 통해 전송
    try {
      sender_.sendImage(metadata_json, jpeg_buffer);
      success_count++;
    } catch (const std::exception &e) {
      std::cerr << "[ChunkedTransmitter] 전송 중 에러 발생: " << e.what()
                << std::endl;
      fail_count++;
      // 하나만 실패해도 전체 중단할지 여부는 정책에 따라 다름 (일단 계속 시도)
    }
  }

  std::cout << "[ChunkedTransmitter] 전송 완료: 성공 " << success_count << "/"
            << total_frames << ", 실패 " << fail_count << std::endl;
}
