#ifndef I_IMAGE_PREPROCESSOR_H
#define I_IMAGE_PREPROCESSOR_H

#include <opencv2/opencv.hpp>

// ================================================================
// 인터페이스: 이미지 전처리 (DIP + ISP)
//
// AI 추론 엔진을 하드웨어 특정 이미지 포맷에서 분리:
//   - BGR/GStreamer → IImagePreprocessor → 텐서 입력
//   - NV12/V4L2    → IImagePreprocessor → 텐서 입력
//
// 구현체 교체만으로 다른 카메라/프레임 포맷 지원 가능
// ================================================================

class IImagePreprocessor {
public:
    virtual ~IImagePreprocessor() = default;

    // 원시 프레임 → AI 모델 입력 텐서 포맷으로 변환
    // 반환: 전처리된 cv::Mat (예: 640×640 RGB float32)
    virtual cv::Mat preprocess(const cv::Mat& raw_frame) = 0;
};

#endif // I_IMAGE_PREPROCESSOR_H
