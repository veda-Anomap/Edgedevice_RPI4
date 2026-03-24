#ifndef LOW_LIGHT_ENHANCER_H
#define LOW_LIGHT_ENHANCER_H

#include "IImageEnhancer.h"
#include <opencv2/opencv.hpp>

// ================================================================
// LowLightEnhancer: 시스템 리소스를 최적화한 저조도 개선 알고리즘
//
// 최적화 포인트 (Raspberry Pi 4 CPU 집중 최적화):
// 1. LUT (Look-Up Table) 사전 계산: 루프 내의 pow 연산 제거
// 2. CLAHE 객체 재사용: 매 프레임 객체 생성/소멸 비용 제거
// 3. 필터 경량화: 무거운 medianBlur 대신 boxFilter/blur 사용
// 4. 불필요한 샤프닝 생략 옵션: CPU 점유율을 위해 선택적으로 사용
// ================================================================

class LowLightEnhancer : public IImageEnhancer {
public:
  LowLightEnhancer();
  ~LowLightEnhancer() override = default;

  void enhance(const cv::Mat &src, cv::Mat &dst) override;

private:
  void precomputeLUTs();

  // 재사용할 CLAHE 객체
  cv::Ptr<cv::CLAHE> clahe_;

  // 미리 계산해둔 Gamma LUT (0.4와 0.7)
  cv::Mat lut_gamma04_;
  cv::Mat lut_gamma07_;
};

#endif // LOW_LIGHT_ENHANCER_H
