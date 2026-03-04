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
