#ifndef ADVANCED_ENHANCERS_H
#define ADVANCED_ENHANCERS_H

#include "IImageEnhancer.h"
#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp> // 가이드 필터 용
#include <functional>

// ================================================================
// AdvancedEnhancers: compare_0213.cpp의 다양한 제안 방식들을
// 플러그인 형태로 분리 및 최적화한 모음집입니다.
// ================================================================

// 1. [방식 1] Retinex 기반 (기존 1번)
class RetinexEnhancer : public IImageEnhancer {
public:
  RetinexEnhancer();
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
  void setK(float k) { k_ = k; }

private:
  cv::Ptr<cv::CLAHE> clahe_;
  float k_ = 40.0f;
};

// 2. [방식 2] YUV Advanced (기존 2번) - 추가됨
class YuvAdvancedEnhancer : public IImageEnhancer {
public:
  YuvAdvancedEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
  void setGamma(float g) { gamma_ = g; }
  void setClips(float c) { clips_ = c; }

private:
  float gamma_ = -1.0f; // -1 means auto
  float clips_ = 2.0f;
};

// 2. [방식 4] 2024 CVPR 기반 적응형 톤 매핑 (Zero-Shot Illumination)
class ToneMappingEnhancer : public IImageEnhancer {
public:
  ToneMappingEnhancer(int radius = 16, float eps = 0.01f, float gamma_base = 0.5f);
  void enhance(const cv::Mat &src, cv::Mat &dst) override;

  void setEps(float eps) { eps_ = eps; }
  void setGammaBase(float gamma) { gamma_base_ = gamma; }

private:
  int radius_;
  float eps_;
  float gamma_base_;
};

// 3. [방식 5] 2025 ICCV 기반 가중치 다중 스케일 디테일 강화
class DetailBoostEnhancer : public IImageEnhancer {
public:
  DetailBoostEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
  void setW1(float w1) { w1_ = w1; }
  void setW2(float w2) { w2_ = w2; }

private:
  float w1_ = 1.5f;
  float w2_ = 2.0f;
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
  void setSStrength(float s) { s_strength_ = s; }

private:
  float s_strength_ = 2.2f;
};

// 7. [추가] Hybrid Enhancer (ToneMapping + DetailBoost)
class HybridEnhancer : public IImageEnhancer {
public:
  HybridEnhancer() = default;
  void enhance(const cv::Mat &src, cv::Mat &dst) override;
};

// 8. [신규] AdaptiveHybridEnhancer (조도 기반 자동 전환)
class AdaptiveHybridEnhancer : public IImageEnhancer {
public:
  explicit AdaptiveHybridEnhancer(std::function<int()> get_lux_fn = nullptr);
  void enhance(const cv::Mat &src, cv::Mat &dst) override;

private:
  std::function<int()> get_lux_fn_;
  int prev_level_ = 2; // Hysteresis용 이전 단계 저장
  RetinexEnhancer retinex_;
  ToneMappingEnhancer tone_map_;
  DetailBoostEnhancer detail_boost_;
};

#endif // ADVANCED_ENHANCERS_H
