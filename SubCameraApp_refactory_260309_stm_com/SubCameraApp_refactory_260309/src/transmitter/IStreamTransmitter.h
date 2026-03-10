#ifndef I_STREAM_TRANSMITTER_H
#define I_STREAM_TRANSMITTER_H

#include <vector>
#include <memory>
#include <string>
#include <opencv2/opencv.hpp>

using FramePtr = std::shared_ptr<cv::Mat>;

// ================================================================
// 인터페이스: 영상 클립 전송 (OCP + DIP)
//
// Containerless Streaming:
//   컨테이너 포맷 (MP4, AVI 등) 없이 프레임을 청크 단위로 전송
//   구현체에 따라 UDP, TCP, WebRTC 등으로 교체 가능
//
// 에러 처리:
//   transmit() 실패 시 내부적으로 재시도 + 에러 로깅
// ================================================================

class IStreamTransmitter {
public:
    virtual ~IStreamTransmitter() = default;

    // 대상 서버 설정
    virtual void setTarget(const std::string& host, int port) = 0;

    // 프레임 클립을 서버로 전송
    // frames: 시간순 프레임 배열 (pre-event + post-event)
    // track_id: 이벤트를 유발한 트래킹 ID
    virtual void transmit(const std::vector<FramePtr>& frames, int track_id) = 0;

    // 연결 상태 확인
    virtual bool isConnected() const = 0;
};

#endif // I_STREAM_TRANSMITTER_H
