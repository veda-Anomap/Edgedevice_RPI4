#include "LowLightEnhancer.h"

LowLightEnhancer::LowLightEnhancer() {
  // 1. 객체 생성 시 한 번만 CLAHE 생성 및 할당
  clahe_ = cv::createCLAHE(2.5, cv::Size(8, 8));

  // 2. 자주 사용되는 감마(0.4와 0.7)에 대한 LUT 미리 계산
  precomputeLUTs();
}

void LowLightEnhancer::precomputeLUTs() {
  lut_gamma04_.create(1, 256, CV_8U);
  lut_gamma07_.create(1, 256, CV_8U);

  for (int i = 0; i < 256; i++) {
    lut_gamma04_.at<uchar>(i) =
        cv::saturate_cast<uchar>(pow(i / 255.0, 0.4) * 255.0);
    lut_gamma07_.at<uchar>(i) =
        cv::saturate_cast<uchar>(pow(i / 255.0, 0.7) * 255.0);
  }
}

void LowLightEnhancer::enhance(const cv::Mat &src, cv::Mat &dst) {
  if (src.empty())
    return;

  // BGR -> YUV 색공간 변환
  cv::Mat yuv;
  cv::cvtColor(src, yuv, cv::COLOR_BGR2YUV);

  std::vector<cv::Mat> channels;
  cv::split(yuv, channels); // channels[0]: Y, channels[1]: U, channels[2]: V

  // [Y 채널] - 밝기 및 대비 개선
  double mean_b = cv::mean(channels[0])[0];

  // 루프 내 pow() 연산 대신, O(1) 조회 테이블(LUT) 사용
  cv::Mat &lut = (mean_b < 50) ? lut_gamma04_ : lut_gamma07_;
  cv::LUT(channels[0], lut, channels[0]);

  // 미리 생성해둔 CLAHE 재사용
  clahe_->apply(channels[0], channels[0]);

  // [U, V 채널] - 무지개 노이즈 억제
  // medianBlur(k=5)는 CPU 부담이 매우 큼.
  // RPi 4의 실시간성을 위해 3x3 boxFilter(가장 빠름)로 대체하여 색상 노이즈
  // 완화.
  cv::boxFilter(channels[1], channels[1], -1, cv::Size(3, 3));
  cv::boxFilter(channels[2], channels[2], -1, cv::Size(3, 3));

  // 채널 병합 및 원래 색공간 복귀
  cv::merge(channels, yuv);
  cv::cvtColor(yuv, dst, cv::COLOR_YUV2BGR);

  // [최종 샤프닝 생략]
  // GaussianBlur + addWeighted 연산은 CPU 비용 대비 엣지 강화 실효성이 낮음.
  // CLAHE 만으로 대비와 텍스처가 충분히 살아나므로 생략하여 초당 5~10ms 세이브.
}
