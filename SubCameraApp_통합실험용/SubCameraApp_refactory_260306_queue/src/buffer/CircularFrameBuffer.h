#ifndef CIRCULAR_FRAME_BUFFER_H
#define CIRCULAR_FRAME_BUFFER_H

#include <vector>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>

// ================================================================
// CircularFrameBuffer: 스레드 안전 링 버퍼 (SRP)
//
// 고정 용량의 순환 버퍼에 프레임을 저장.
// - push(): 새 프레임 삽입 (가장 오래된 프레임 덮어쓰기)
// - snapshot(): 현재 버퍼 내용을 시간순으로 복사 반환
//
// 메모리 관리:
//   std::shared_ptr<cv::Mat>로 프레임 관리 → 메모리 누수 방지
//   snapshot() 시 shallow copy (shared_ptr 복사) → 잠금 시간 최소화
//   호출자가 deep copy 필요 시 직접 .clone() 수행
//
// 스레드 안전성:
//   push()와 snapshot() 모두 std::mutex 보호
//   snapshot()은 shared_ptr 복사만 하므로 잠금 시간 O(N) 포인터 복사
//   (cv::Mat 픽셀 데이터 복사 아님 → 매우 빠름)
// ================================================================

using FramePtr = std::shared_ptr<cv::Mat>;

class CircularFrameBuffer {
public:
    // capacity: 버퍼에 저장할 최대 프레임 수
    explicit CircularFrameBuffer(size_t capacity);
    ~CircularFrameBuffer() = default;

    // 프레임 삽입 (카메라 스레드에서 호출)
    // 내부적으로 frame.clone()하여 독립 사본 저장
    void push(const cv::Mat& frame);

    // 현재 버퍼의 시간순 스냅샷 반환
    // shared_ptr 복사만 수행 → 빠른 잠금 해제
    // 반환된 벡터의 [0] = 가장 오래된 프레임, [back] = 최신 프레임
    std::vector<FramePtr> snapshot() const;

    // 현재 저장된 프레임 수
    size_t size() const;

    // 버퍼 용량
    size_t capacity() const;

    // 버퍼 초기화
    void clear();

private:
    size_t capacity_;
    std::vector<FramePtr> buffer_;  // 고정 크기 슬롯
    size_t head_ = 0;               // 다음 쓰기 위치
    size_t count_ = 0;              // 현재 저장된 프레임 수
    mutable std::mutex mutex_;
};

#endif // CIRCULAR_FRAME_BUFFER_H
