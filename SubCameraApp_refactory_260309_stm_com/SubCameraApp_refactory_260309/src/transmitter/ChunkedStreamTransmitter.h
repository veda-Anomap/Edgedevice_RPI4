#ifndef CHUNKED_STREAM_TRANSMITTER_H
#define CHUNKED_STREAM_TRANSMITTER_H

#include "../network/INetworkSender.h"
#include "IStreamTransmitter.h"

#include <atomic>
#include <mutex>
#include <string>

// ================================================================
// ChunkedStreamTransmitter: Containerless 청크 전송 (SRP)
//
// 각 프레임을 JPEG 인코딩 → INetworkSender를 통해 IMAGE 패킷으로 전송
// TCP 기반, 재시도 메커니즘 및 에러 로깅 포함
// ================================================================

class ChunkedStreamTransmitter : public IStreamTransmitter {
public:
  // INetworkSender 주입: 기존 소켓 재사용을 위함
  ChunkedStreamTransmitter(INetworkSender &sender, int max_retries = 3,
                           int jpeg_quality = 85);
  ~ChunkedStreamTransmitter() override;

  void setTarget(const std::string &host, int port) override;
  void transmit(const std::vector<FramePtr> &frames, int track_id) override;
  bool isConnected() const override;

  // 시스템 실행 상태 플래그 공유 (종료 시 루프 탈출용)
  void setRunningFlag(std::atomic<bool> *flag) { running_flag_ = flag; }

private:
  INetworkSender &sender_;

  std::string host_;
  int port_ = 0;
  std::atomic<bool> connected_{true}; // 상시 연결로 간주 (NetworkFacade가 관리)

  int max_retries_;
  int jpeg_quality_;
  std::mutex transmit_mutex_; // 동시 전송 방지
  std::atomic<bool> *running_flag_ = nullptr;
};

#endif // CHUNKED_STREAM_TRANSMITTER_H
