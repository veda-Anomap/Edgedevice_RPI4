#include "AdvancedEnhancers.h"

using namespace cv;
using namespace std;

// 공통 조명 강화 헬퍼 함수
static Mat enhanceIllumination(Mat L, float k = 40.0f) {
  Mat L_32F, L_out;
  L.convertTo(L_32F, CV_32F);
  L_out = L_32F.mul((255.0f + k) / (L_32F + k));
  L_out.convertTo(L_out, CV_8U);
  return L_out;
}

// 1. [방식 1] Retinex 기반 (기존 1번)
RetinexEnhancer::RetinexEnhancer() {
  clahe_ = createCLAHE(1.5, Size(8, 8)); // CLAHE 1회 생성 (최적화)
}

void RetinexEnhancer::enhance(const cv::Mat &src, cv::Mat &dst) {
  if (src.empty())
    return;
  Mat hsv;
  cvtColor(src, hsv, COLOR_BGR2HSV);
  vector<Mat> hsv_planes;
  split(hsv, hsv_planes);

  Mat V = hsv_planes[2];
  Mat L;
  int radius = 12;
  double eps = 0.0001 * 255 * 255;

  // CPU 보호를 위해 가이드 필터의 반지름을 줄이는 것도 가능하지만,
  // 동일한 품질을 위해 ximgproc::guidedFilter 사용
  ximgproc::guidedFilter(V, V, L, radius, eps);

  Mat V_32F, L_32F, R_32F, R;
  V.convertTo(V_32F, CV_32F);
  L.convertTo(L_32F, CV_32F);
  divide(V_32F, L_32F + 1.0f, R_32F, 255.0);
  R_32F.convertTo(R, CV_8U);

  Mat denoiseR;
  // bilateralFilter는 CPU에 매우 부담됨. boxFilter로 대체(최적화)
  // bilateralFilter(R, denoiseR, 5, 50, 50);
  boxFilter(R, denoiseR, -1, Size(3, 3));

  clahe_->apply(denoiseR, R);

  Mat R_tmp;
  R.convertTo(R_tmp, CV_32F, 1.0 / 255.0);
  ximgproc::guidedFilter(R_tmp, R_tmp, R_tmp, 3, 0.01);
  R_tmp.convertTo(R, CV_8U, 255.0);

  Mat eL = enhanceIllumination(L, 40.0f);
  Mat el_32, er_32, ev_32, eV;
  eL.convertTo(el_32, CV_32F);
  R.convertTo(er_32, CV_32F);
  multiply(el_32, er_32, ev_32, 1.0 / 255.0);
  ev_32.convertTo(eV, CV_8U);

  vector<Mat> merged = {hsv_planes[0], hsv_planes[1], eV};
  merge(merged, hsv);
  cvtColor(hsv, dst, COLOR_HSV2BGR);

  // [최적화] Unsharp mask 생략 (10ms+ 절감)
  // Mat blurred;
  // GaussianBlur(dst, blurred, Size(0, 0), 2);
  // addWeighted(dst, 1.4, blurred, -0.4, 0, dst);
}

#include <arm_neon.h>

// 2. [방식 4] 2024 CVPR 기반 적응형 톤 매핑
ToneMappingEnhancer::ToneMappingEnhancer(int radius, float eps, float gamma_base)
    : radius_(radius), eps_(eps), gamma_base_(gamma_base) {}

void ToneMappingEnhancer::enhance(const cv::Mat &src, cv::Mat &dst) {
  if (src.empty())
    return;
  vector<Mat> channels;
  split(src, channels);

  Mat v_channel;
  cv::max(channels[0], channels[1], v_channel);
  cv::max(v_channel, channels[2], v_channel);
  v_channel.convertTo(v_channel, CV_32F, 1.0 / 255.0);

  Mat illumination;
  ximgproc::guidedFilter(v_channel, v_channel, illumination, radius_, eps_);

  Scalar mean_val = mean(illumination);
  float gamma = pow(gamma_base_, (0.5f - (float)mean_val[0]) / 0.5f);
  Mat lut_illumination;
  pow(illumination, gamma, lut_illumination);

  dst.create(src.size(), src.type());
  vector<Mat> out_channels(3);
  for (int i = 0; i < 3; ++i) {
    out_channels[i].create(src.size(), CV_8U);
    
    // [SIMD 최적화] NEON 연산 루프
    const uint8_t* p_src = channels[i].data;
    const float* p_illum = (float*)illumination.data;
    const float* p_lut_illum = (float*)lut_illumination.data;
    uint8_t* p_dst = out_channels[i].data;
    int size = channels[i].total();

    float32x4_t v_inv255 = vdupq_n_f32(1.0f / 255.0f);
    float32x4_t v_eps = vdupq_n_f32(0.001f);
    float32x4_t v_255 = vdupq_n_f32(255.0f);

    int j = 0;
    for (; j <= size - 4; j += 4) {
        // Load 8-bit, convert to 32-bit float
        uint8x8_t v8 = vld1_u8(p_src + j);
        uint16x4_t v16 = vget_low_u16(vmovl_u8(v8));
        uint32x4_t v32 = vmovl_u16(v16);
        float32x4_t v_fp = vcvtq_f32_u32(v32);
        v_fp = vmulq_f32(v_fp, v_inv255);

        // Load Illum
        float32x4_t v_i = vld1q_f32(p_illum + j);
        float32x4_t v_li = vld1q_f32(p_lut_illum + j);

        // Core Math: (c / (i + eps)) * li
        float32x4_t v_res = vmulq_f32(v_fp, vdivq_f32(v_li, vaddq_f32(v_i, v_eps)));
        
        // Convert back to 8-bit
        v_res = vmulq_f32(v_res, v_255);
        uint32x4_t v_u32 = vcvtq_u32_f32(v_res);
        
        // 4픽셀 한꺼번에 스토어 (u32 -> u16 -> u8)
        uint16x4_t v16_out = vmovn_u32(v_u32);
        uint8x8_t v8_out = vmovn_u16(vcombine_u16(v16_out, v16_out));
        vst1_lane_u32((uint32_t*)(p_dst + j), vreinterpret_u32_u8(v8_out), 0);
    }
    // Tail
    for (; j < size; ++j) {
        float c = p_src[j] / 255.0f;
        p_dst[j] = saturate_cast<uchar>((c / (p_illum[j] + 0.001f)) * p_lut_illum[j] * 255.0f);
    }
  }

  merge(out_channels, dst);
}

// 3. [방식 5] 2025 ICCV 기반 카중치 다중 스케일 디테일 강화
void DetailBoostEnhancer::enhance(const cv::Mat &src, cv::Mat &dst) {
  if (src.empty())
    return;
  Mat float_img;
  src.convertTo(float_img, CV_32F);

  Mat blur1, blur2;
  GaussianBlur(float_img, blur1, Size(3, 3), 1.0);
  GaussianBlur(float_img, blur2, Size(7, 7), 2.0);

  Mat detail1 = float_img - blur1;
  Mat detail2 = blur1 - blur2;

  Mat boosted = float_img + (detail1 * 1.5f) + (detail2 * 2.0f);

  boosted.setTo(0, boosted < 0);
  boosted.setTo(255, boosted > 255);
  boosted.convertTo(dst, CV_8U);
}

// 4. [방식 7] CleanSharp (YUV 색상 노이즈 억제형)
void CleanSharpEnhancer::enhance(const cv::Mat &src, cv::Mat &dst) {
  if (src.empty())
    return;
  Mat yuv;
  cvtColor(src, yuv, COLOR_BGR2YUV);
  vector<Mat> yuv_planes;
  split(yuv, yuv_planes);

  // [최적화] medianBlur -> boxFilter (3x3)
  boxFilter(yuv_planes[1], yuv_planes[1], -1, Size(3, 3));
  boxFilter(yuv_planes[2], yuv_planes[2], -1, Size(3, 3));

  Mat Y = yuv_planes[0];
  Mat Y_32F;
  Y.convertTo(Y_32F, CV_32F, 1.0 / 255.0);

  Mat illumination;
  ximgproc::guidedFilter(Y_32F, Y_32F, illumination, 12, 0.01);

  Scalar avg_illum = mean(illumination);
  float gamma = pow(0.7f, (0.5f - (float)avg_illum[0]));
  Mat enhanced_Y;
  pow(Y_32F, gamma, enhanced_Y);

  Mat detail = Y_32F - illumination;
  enhanced_Y = enhanced_Y + (detail * 1.2f);

  enhanced_Y.convertTo(yuv_planes[0], CV_8U, 255.0);
  merge(yuv_planes, yuv);
  cvtColor(yuv, dst, COLOR_YUV2BGR);

  // [최적화] Unsharp Mask 생략
}

// 5. [방식 8] UltimateBalanced (Lab 색상 노이즈 억제형)
void UltimateBalancedEnhancer::enhance(const cv::Mat &src, cv::Mat &dst) {
  if (src.empty())
    return;
  Mat lab;
  cvtColor(src, lab, COLOR_BGR2Lab);
  vector<Mat> lab_planes;
  split(lab, lab_planes);

  // [최적화] medianBlur -> boxFilter
  boxFilter(lab_planes[1], lab_planes[1], -1, Size(3, 3));
  boxFilter(lab_planes[2], lab_planes[2], -1, Size(3, 3));

  Mat L = lab_planes[0];
  Mat L_32F;
  L.convertTo(L_32F, CV_32F, 1.0 / 255.0);

  Mat base;
  ximgproc::guidedFilter(L_32F, L_32F, base, 12, 0.01);

  Mat enhanced_L;
  pow(base, 0.5, enhanced_L);

  Scalar avg_l = mean(enhanced_L);
  if (avg_l[0] < 0.4) {
    enhanced_L += (0.4 - avg_l[0]) * 0.5;
  }

  Mat detail = L_32F - base;

  Mat mask;
  threshold(abs(detail), mask, 0.01, 1.0, THRESH_BINARY);

  enhanced_L = enhanced_L + (detail.mul(mask) * 1.8f);

  enhanced_L.setTo(0, enhanced_L < 0);
  enhanced_L.setTo(1.0, enhanced_L > 1.0);

  enhanced_L.convertTo(lab_planes[0], CV_8U, 255.0);
  merge(lab_planes, lab);
  cvtColor(lab, dst, COLOR_Lab2BGR);
}

// 8. AdaptiveHybridEnhancer 구현
AdaptiveHybridEnhancer::AdaptiveHybridEnhancer()
    : tone_map_(16, 0.01f, 0.5f) {}

void AdaptiveHybridEnhancer::enhance(const cv::Mat &src, cv::Mat &dst) {
  if (src.empty()) return;

  Scalar m = mean(src);
  float avg = (float)(m[0] + m[1] + m[2]) / 3.0f;

  /**
   * [Hysteresis Logic]
   * User Thresholds: Bright(>180), Normal(100~180), Dim(70~100), Extreme(<70)
   */
  int current_level = prev_level_;
  if (avg > 185) current_level = 1;
  else if (avg < 175 && avg > 105) current_level = 2;
  else if (avg < 95 && avg > 75)   current_level = 3;
  else if (avg < 65)                current_level = 4;

  switch (current_level) {
    case 1: // Bright (Bypass)
      dst = src;
      break;
    case 2: // Normal (Retinex/CLAHE)
      retinex_.enhance(src, dst);
      break;
    case 3: // Dim (NEON ToneMap)
      tone_map_.enhance(src, dst);
      break;
    case 4: // Extreme (ToneMap + DetailBoost)
      tone_map_.enhance(src, dst);
      detail_boost_.enhance(dst, dst);
      break;
    default:
      dst = src;
  }
  prev_level_ = current_level;
}
