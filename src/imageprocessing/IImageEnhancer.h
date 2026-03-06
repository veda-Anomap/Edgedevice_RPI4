#ifndef I_IMAGE_ENHANCER_H
#define I_IMAGE_ENHANCER_H

#include <opencv2/opencv.hpp>

// ================================================================
// IImageEnhancer: 이미지 개선을 위한 인터페이스 (DIP, OCP)
//
// 여러 가지 전처리(저조도 개선, 노이즈 제거 등) 코드가
// 시스템 전체 구조 수정 없이 쉽게 플러그인될 수 있도록 인터페이스화합니다.
// ================================================================

class IImageEnhancer {
public:
  virtual ~IImageEnhancer() = default;

  // 원본 프레임을 입력받아 개선된 프레임을 반환하거나 출력에 씁니다.
  // In-place 처리(src == dst)도 가능하도록 설계하는 것이 좋습니다.
  virtual void enhance(const cv::Mat &src, cv::Mat &dst) = 0;
};

#endif // I_IMAGE_ENHANCER_H
