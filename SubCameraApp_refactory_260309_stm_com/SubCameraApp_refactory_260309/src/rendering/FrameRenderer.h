#ifndef FRAME_RENDERER_H
#define FRAME_RENDERER_H

#include "../ai/IAiDetector.h"
#include <opencv2/opencv.hpp>

// =============================================
// FrameRenderer: 시각화 전용 (SRP)
// 스켈레톤/바운딩 박스/라벨 그리기만 책임
// 중복 코드 제거 (aiWorkerLoop + cameraLoop에 있던 것)
// =============================================

class FrameRenderer {
public:
    FrameRenderer() = default;
    ~FrameRenderer() = default;

    // 감지 결과를 프레임 위에 그리기
    void drawDetections(cv::Mat& frame, const DetectionResult& result);

    // 단일 감지 결과 그리기 (낙상 캡처 이미지용)
    void drawSingleDetection(cv::Mat& frame, const SingleDetection& det);
};

#endif // FRAME_RENDERER_H
