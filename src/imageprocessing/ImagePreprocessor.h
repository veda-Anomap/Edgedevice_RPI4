#ifndef IMAGE_PREPROCESSOR_H
#define IMAGE_PREPROCESSOR_H

#include "IImagePreprocessor.h"

// ================================================================
// ImagePreprocessor: 표준 전처리 파이프라인 (SRP + LSP)
//
// 구성 가능한 파이프라인:
//   1. 리사이즈 (입력 크기 → 모델 입력 크기)
//   2. 색상 변환 (BGR → RGB 등)
//   3. 정규화 (0-255 → 0.0-1.0)
//
// 생성자에서 모든 파라미터를 설정 → 런타임 교체 가능
// ================================================================

class ImagePreprocessor : public IImagePreprocessor {
public:
    // target_size: 모델 입력 크기 (정사각형)
    // color_conversion: OpenCV 색상 변환 코드 (예: cv::COLOR_BGR2RGB)
    //                   -1이면 변환 안 함
    // normalize_scale: 정규화 배율 (예: 1.0/255.0)
    //                  1.0이면 정규화 안 함
    // output_type: 출력 cv::Mat 타입 (예: CV_32FC3)
    ImagePreprocessor(int target_size,
                      int color_conversion = cv::COLOR_BGR2RGB,
                      double normalize_scale = 1.0 / 255.0,
                      int output_type = CV_32FC3);
    ~ImagePreprocessor() override = default;

    cv::Mat preprocess(const cv::Mat& raw_frame) override;

private:
    int target_size_;
    int color_conversion_;
    double normalize_scale_;
    int output_type_;
};

#endif // IMAGE_PREPROCESSOR_H
