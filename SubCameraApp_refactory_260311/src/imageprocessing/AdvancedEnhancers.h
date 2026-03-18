#ifndef ADVANCED_ENHANCERS_H
#define ADVANCED_ENHANCERS_H

#include "IImageEnhancer.h"
#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp> // 가이드 필터 용

// ================================================================
// AdvancedEnhancers: compare_0213.cpp의 다양한 제안 방식들을
// 플러그인 형태로 분리 및 최적화한 모음집입니다.
// ================================================================

// 1. [방식 1] Retinex 기반 (기존 1번)
class RetinexEnhancer : public IImageEnhancer {
public:
  RetinexEnhancer();
  void enhance(const cv::Mat &src, cv::Mat &dst) override;

private:
  cv::Ptr<cv::CLAHE> clahe_;
};

// 2. [방식 4] 2024 CVPR 기반 적응형 톤 매핑 (Zero-Shot Illumination)
class ToneMappingEnhancer : public IImageEnhancer {
public:
  ToneMappingEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
};

// 3. [방식 5] 2025 ICCV 기반 가중치 다중 스케일 디테일 강화
class DetailBoostEnhancer : public IImageEnhancer {
public:
  DetailBoostEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
};

// 4. [방식 7] CleanSharp (YUV 색상 노이즈 억제형)
class CleanSharpEnhancer : public IImageEnhancer {
public:
  CleanSharpEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
};

// 5. [추가] WWGIF 기반 Improved Retinex (방식 3)
class WWGIFEnhancer : public IImageEnhancer {
public:
  WWGIFEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
};

// 6. [방식 8] UltimateBalanced (Lab 색상 노이즈 억제형 - V5 업그레이드)
class UltimateBalancedEnhancer : public IImageEnhancer {
public:
  UltimateBalancedEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
};

// 7. [추가] Hybrid Enhancer (ToneMapping + DetailBoost)
class HybridEnhancer : public IImageEnhancer {
public:
  HybridEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
};

#endif // ADVANCED_ENHANCERS_H
