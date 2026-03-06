#include "CircularFrameBuffer.h"
#include <iostream>

CircularFrameBuffer::CircularFrameBuffer(size_t capacity)
    : capacity_(capacity) {
    buffer_.resize(capacity_);
    std::cout << "[CircularFrameBuffer] 생성됨 (용량: " << capacity_ << " 프레임)" << std::endl;
}

void CircularFrameBuffer::push(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    // shared_ptr로 독립 사본 생성 (원본과 메모리 분리)
    buffer_[head_] = std::make_shared<cv::Mat>(frame.clone());

    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) {
        count_++;
    }
}

std::vector<FramePtr> CircularFrameBuffer::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // shared_ptr 복사만 수행 → 잠금 시간 최소화
    // 시간순 정렬: 가장 오래된 프레임부터 최신까지
    std::vector<FramePtr> result;
    result.reserve(count_);

    if (count_ < capacity_) {
        // 버퍼가 아직 가득 차지 않은 경우: 0부터 순서대로
        for (size_t i = 0; i < count_; ++i) {
            if (buffer_[i]) {
                result.push_back(buffer_[i]);
            }
        }
    } else {
        // 버퍼가 가득 찬 경우: head_부터 순환하여 시간순 정렬
        for (size_t i = 0; i < capacity_; ++i) {
            size_t idx = (head_ + i) % capacity_;
            if (buffer_[idx]) {
                result.push_back(buffer_[idx]);
            }
        }
    }

    return result;
}

size_t CircularFrameBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

size_t CircularFrameBuffer::capacity() const {
    return capacity_;
}

void CircularFrameBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& slot : buffer_) {
        slot.reset();
    }
    head_ = 0;
    count_ = 0;
}
