#pragma once
#include <opencv2/opencv.hpp>
#include <string>

// =============================================
// ImageMetrics: 한 이미지의 5종 품질 지표
// =============================================
struct ImageMetrics {
    std::string name;
    double sharpness    = 0.0; // Laplacian Variance
    double colorfulness = 0.0; // Hasler-Suesstrunk
    double brightness   = 0.0; // 평균 밝기
    double tenengrad    = 0.0; // Sobel 기반 에지 강도
    double noiseEstimate= 0.0; // Brenner Gradient
};

// =============================================
// MetricsCalculator: 이미지 품질 지표 계산 (SRP)
// 260313_compare.cpp 의 지표 함수들을 클래스로 추출
// =============================================
class MetricsCalculator {
public:
    // 한 이미지의 모든 지표 한번에 계산
    static ImageMetrics compute(const cv::Mat& src, const std::string& name = "");

    // 개별 지표
    static double getSharpness(const cv::Mat& src);     // Laplacian Variance
    static double getColorfulness(const cv::Mat& src);  // Hasler-Suesstrunk
    static double getBrightness(const cv::Mat& src);    // 평균 밝기
    static double getTenengrad(const cv::Mat& src);     // Sobel 기반 에지 강도
    static double getNoiseEstimate(const cv::Mat& src); // Brenner Gradient

    // 지표를 이미지 셀 위에 오버레이 텍스트로 그려줌
    static void drawOverlay(cv::Mat& cell, const ImageMetrics& m);
};
