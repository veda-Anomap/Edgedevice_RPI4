#include "ImagePreprocessor.h"
#include <iostream>

ImagePreprocessor::ImagePreprocessor(int target_size,
                                     int color_conversion,
                                     double normalize_scale,
                                     int output_type)
    : target_size_(target_size)
    , color_conversion_(color_conversion)
    , normalize_scale_(normalize_scale)
    , output_type_(output_type) {
    std::cout << "[ImagePreprocessor] 초기화 완료 (크기: " << target_size_
              << ", 정규화: " << normalize_scale_ << ")" << std::endl;
}

cv::Mat ImagePreprocessor::preprocess(const cv::Mat& raw_frame) {
    cv::Mat result;

    // 1. 리사이즈 (모델 입력 크기로)
    cv::Mat resized;
    cv::resize(raw_frame, resized,
               cv::Size(target_size_, target_size_),
               0, 0, cv::INTER_NEAREST);

    // 2. 색상 변환 (예: BGR → RGB)
    cv::Mat converted;
    if (color_conversion_ >= 0) {
        cv::cvtColor(resized, converted, color_conversion_);
    } else {
        converted = resized;
    }

    // 3. 정규화 + 타입 변환 (예: uint8 → float32, 0-255 → 0.0-1.0)
    if (normalize_scale_ != 1.0 || output_type_ != converted.type()) {
        converted.convertTo(result, output_type_, normalize_scale_);
    } else {
        result = converted;
    }

    return result;
}

void ImagePreprocessor::preprocessToTensor(const cv::Mat& raw_frame, float* tensor_ptr) {
    // 1. 하드웨어 가속 파이프라인 덕분에 이미 크기가 맞을 가능성이 높음
    //    만약 다르면 여기서 한 번 더 체크하여 리사이즈
    cv::Mat resized;
    if (raw_frame.cols != target_size_ || raw_frame.rows != target_size_) {
        cv::resize(raw_frame, resized, cv::Size(target_size_, target_size_), 0, 0, cv::INTER_NEAREST);
    } else {
        resized = raw_frame;
    }

    // 2. 텐서 포인터를 Mat으로 감싸서 직접 쓰기 준비 (Zero-copy Wrapper)
    cv::Mat tensor_wrapper(target_size_, target_size_, output_type_, tensor_ptr);

    // 3. 변환 및 복사를 한 번에 수행 (convertTo는 내부적으로 루프 최적화됨)
    //    BGR → RGB 변환이 필요한 경우
    if (color_conversion_ >= 0) {
        cv::Mat converted;
        cv::cvtColor(resized, converted, color_conversion_);
        converted.convertTo(tensor_wrapper, output_type_, normalize_scale_);
    } else {
        resized.convertTo(tensor_wrapper, output_type_, normalize_scale_);
    }
}
