#include <algorithm>
#include <chrono> // 시간 측정을 위해 추가
#include <cmath>
#include <ctime> // 시간 포맷 변환을 위해 추가
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp>
#include <sstream> // 문자열 조립을 위해 추가
#include <vector>

using namespace std;
using namespace cv;

#include <fstream> // 파일 저장을 위해 추가
#include <iomanip> // 출력 포맷 설정을 위해 추가

// 현재 시간을 문자열로 반환하는 헬퍼 함수
string getCurrentTimestamp() {
  auto now = chrono::system_clock::now();
  time_t now_time = chrono::system_clock::to_time_t(now);
  struct tm *timeinfo = localtime(&now_time);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", timeinfo);
  return string(buffer);
}

// ==========================================
// 0. 이미지 개선 측정  관련 함수
// ==========================================
// [추가] 지표 1: Laplacian Variance (선명도/포커싱)
double getSharpness(const Mat &src) {
  Mat gray, laplacian;
  if (src.channels() == 3)
    cvtColor(src, gray, COLOR_BGR2GRAY);
  else
    gray = src;
  Laplacian(gray, laplacian, CV_64F);
  Scalar mu, sigma;
  meanStdDev(laplacian, mu, sigma);
  return sigma.val[0] * sigma.val[0]; // 분산 반환
}

// [추가] 지표 2: Color Saturation (색상 풍부도) 논문(Hasler and Suesstrunk)
double getColorfulness(const Mat &src) {
  if (src.channels() != 3)
    return 0; // 그레이스케일은 채도 0

  Mat bgr[3];
  split(src, bgr);

  // 1. rg = R - G / yb = 0.5 * (R + G) - B
  Mat rg = abs(bgr[2] - bgr[1]);
  Mat yb = abs(0.5 * (bgr[2] + bgr[1]) - bgr[0]);

  // 2. 각 차이값의 평균과 표준편차 계산
  Scalar std_rg, mean_rg, std_yb, mean_yb;
  meanStdDev(rg, mean_rg, std_rg);
  meanStdDev(yb, mean_yb, std_yb);

  // 3. 최종 수식 계산
  double std_root = sqrt(pow(std_rg.val[0], 2) + pow(std_yb.val[0], 2));
  double mean_root = sqrt(pow(mean_rg.val[0], 2) + pow(mean_yb.val[0], 2));

  return std_root + (0.3 * mean_root);
}

// [추가] 지표 3: Average Brightness (평균 밝기)
double getBrightness(const Mat &src) {
  Mat gray;
  if (src.channels() == 3)
    cvtColor(src, gray, COLOR_BGR2GRAY);
  else
    gray = src;
  return mean(gray)[0];
}

// [지표 4] Tenengrad: 에지 강도 기반 선명도
double getTenengrad(const Mat &src) {
  Mat gray, gradX, gradY, mag;
  if (src.channels() == 3)
    cvtColor(src, gray, COLOR_BGR2GRAY);
  else
    gray = src;

  Sobel(gray, gradX, CV_64F, 1, 0);
  Sobel(gray, gradY, CV_64F, 0, 1);
  magnitude(gradX, gradY, mag);

  Scalar s = sum(mag.mul(mag));
  return s.val[0] / (src.rows * src.cols);
}

// [지표 5] Noise/Artifact Estimate: 이미지의 거칠기 측정 // Brenner Gradient
// 픽셀 간의 급격한 변화를 측정하여 노이즈나 블록 왜곡이 심해졌는지 간접
// 확인합니다.
double getNoiseEstimate(const Mat &src) {
  Mat gray, temp;
  if (src.channels() == 3)
    cvtColor(src, gray, COLOR_BGR2GRAY);
  else
    gray = src;

  // 인접 픽셀 간의 차이의 절대값 평균을 구함
  Mat diff;
  absdiff(gray(Rect(0, 0, gray.cols - 1, gray.rows)),
          gray(Rect(1, 0, gray.cols - 1, gray.rows)), diff);

  return mean(
      diff)[0]; // 이 값이 보정 전보다 너무 커지면 노이즈/블록화가 심해진 것
}

// ==========================================
// 1. 공통 및 방식 1 (기존 Retinex) 관련 함수
// ==========================================
Mat enhanceIllumination(Mat L, float k = 40.0f) {
  Mat L_32F, L_out;
  L.convertTo(L_32F, CV_32F);
  L_out = L_32F.mul((255.0f + k) / (L_32F + k));
  L_out.convertTo(L_out, CV_8U);
  return L_out;
}

Mat adaptiveStretch(Mat s_channel) {
  double minVal, maxVal;
  minMaxLoc(s_channel, &minVal, &maxVal);
  Mat stretched;
  s_channel.convertTo(stretched, CV_32F);
  stretched = (stretched - minVal) * (255.0 / (maxVal - minVal + 1e-6));
  stretched.convertTo(stretched, CV_8U);
  return stretched;
}

void enhanceRetinex(const Mat &src, Mat &dst) {
  // if (src.empty()) return;
  // Mat hsv;
  // cvtColor(src, hsv, COLOR_BGR2HSV);
  // vector<Mat> hsv_planes;
  // split(hsv, hsv_planes);

  // Mat V = hsv_planes[2];
  // Mat L;
  // int radius = 12;
  // double eps = 0.0001 * 255 * 255;
  // ximgproc::guidedFilter(V, V, L, radius, eps);

  // Mat V_32F, L_32F, R_32F, R;
  // V.convertTo(V_32F, CV_32F);
  // L.convertTo(L_32F, CV_32F);
  // divide(V_32F, L_32F + 1.0f, R_32F, 255.0);
  // R_32F.convertTo(R, CV_8U);

  // Mat denoiseR;
  // bilateralFilter(R, denoiseR, 5, 50, 50);
  // Ptr<CLAHE> claheR = createCLAHE(1.5, Size(8, 8));
  // claheR->apply(denoiseR, R);

  // // [정제] CLAHE 노이즈 제거 (가이드 필터 재사용)
  // Mat R_tmp;
  // R_8U.convertTo(R_tmp, CV_32F, 1.0/255.0);
  // ximgproc::guidedFilter(R_tmp, R_tmp, R_tmp, 3, 0.01);
  // R_tmp.convertTo(R_8U, CV_8U, 255.0);

  // Mat eL = enhanceIllumination(L, 40.0f);
  // Mat el_32, er_32, ev_32, eV;
  // eL.convertTo(el_32, CV_32F);
  // R.convertTo(er_32, CV_32F);
  // multiply(el_32, er_32, ev_32, 1.0/255.0);
  // ev_32.convertTo(eV, CV_8U);

  // vector<Mat> merged = {hsv_planes[0], hsv_planes[1], eV};
  // merge(merged, hsv);
  // cvtColor(hsv, dst, COLOR_HSV2BGR);

  // Mat blurred;
  // GaussianBlur(dst, blurred, Size(0, 0), 2);
  // addWeighted(dst, 1.4, blurred, -0.4, 0, dst);
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
  ximgproc::guidedFilter(V, V, L, radius, eps);

  Mat V_32F, L_32F, R_32F, R;
  V.convertTo(V_32F, CV_32F);
  L.convertTo(L_32F, CV_32F);
  divide(V_32F, L_32F + 1.0f, R_32F, 255.0);
  R_32F.convertTo(R, CV_8U);

  Mat denoiseR;
  bilateralFilter(R, denoiseR, 5, 50, 50);
  Ptr<CLAHE> claheR = createCLAHE(1.5, Size(8, 8));
  claheR->apply(denoiseR, R);

  // [정제] CLAHE 노이즈 제거 (가이드 필터 재사용)
  Mat R_tmp;
  R.convertTo(R_tmp, CV_32F, 1.0 / 255.0); // R_8U -> R로 수정
  ximgproc::guidedFilter(R_tmp, R_tmp, R_tmp, 3, 0.01);
  R_tmp.convertTo(R, CV_8U, 255.0); // R_8U -> R로 수정

  Mat eL = enhanceIllumination(L, 40.0f);
  Mat el_32, er_32, ev_32, eV;
  eL.convertTo(el_32, CV_32F);
  R.convertTo(er_32, CV_32F);
  multiply(el_32, er_32, ev_32, 1.0 / 255.0);
  ev_32.convertTo(eV, CV_8U);

  vector<Mat> merged = {hsv_planes[0], hsv_planes[1], eV};
  merge(merged, hsv);
  cvtColor(hsv, dst, COLOR_HSV2BGR);

  Mat blurred;
  GaussianBlur(dst, blurred, Size(0, 0), 2);
  addWeighted(dst, 1.4, blurred, -0.4, 0, dst);
}

// ==========================================
// 2. 방식 2 (기존 YUV Advanced) 관련 함수
// ==========================================
void enhanceLowLightAdvanced(const Mat &src, Mat &dst) {
  Mat yuv;
  cvtColor(src, yuv, COLOR_BGR2YUV);
  vector<Mat> channels;
  split(yuv, channels);

  double mean_b = mean(channels[0])[0];
  double gamma = (mean_b < 50) ? 0.5 : 0.8;
  Mat lut(1, 256, CV_8U);
  for (int i = 0; i < 256; i++)
    lut.at<uchar>(i) = saturate_cast<uchar>(pow(i / 255.0, gamma) * 255.0);
  LUT(channels[0], lut, channels[0]);

  Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8, 8));
  clahe->apply(channels[0], channels[0]);

  medianBlur(channels[1], channels[1], 5);
  medianBlur(channels[2], channels[2], 5);

  merge(channels, yuv);
  cvtColor(yuv, dst, COLOR_YUV2BGR);

  Mat blurred;
  GaussianBlur(dst, blurred, Size(0, 0), 3);
  addWeighted(dst, 1.3, blurred, -0.3, 0, dst);
}

// ==========================================
// 3. 방식 3 (논문 기반 WWGIF Improved Retinex)
// ==========================================

// WWGIF 구현 (가중치 및 그래디언트 기반 가이드 필터)
Mat WWGIF(const Mat &I, const Mat &p, int r, double eps) {
  Mat mean_I, mean_p, mean_Ip, cov_Ip;
  boxFilter(I, mean_I, CV_32F, Size(2 * r + 1, 2 * r + 1));
  boxFilter(p, mean_p, CV_32F, Size(2 * r + 1, 2 * r + 1));
  boxFilter(I.mul(p), mean_Ip, CV_32F, Size(2 * r + 1, 2 * r + 1));
  cov_Ip = mean_Ip - mean_I.mul(mean_p);

  Mat mean_II, var_I;
  boxFilter(I.mul(I), mean_II, CV_32F, Size(2 * r + 1, 2 * r + 1));
  var_I = mean_II - mean_I.mul(mean_I);

  // 논문 식 (8): 그래디언트 기반 엣지 보호
  Mat gradX, gradY, gradMag;
  Sobel(I, gradX, CV_32F, 1, 0);
  Sobel(I, gradY, CV_32F, 0, 1);
  magnitude(gradX, gradY, gradMag);
  Mat gamma_val;
  exp(-gradMag, gamma_val);
  gamma_val = 1.0 / (1.0 + gamma_val);

  Mat a = cov_Ip / (var_I + eps);
  Mat b = mean_p - a.mul(mean_I);

  Mat mean_a, mean_b;
  boxFilter(a, mean_a, CV_32F, Size(2 * r + 1, 2 * r + 1));
  boxFilter(b, mean_b, CV_32F, Size(2 * r + 1, 2 * r + 1));

  return mean_a.mul(I) + mean_b;
}

// 웨버-페히너 기반 조명 보정
Mat enhanceWeber(Mat L, float k = 40.0f) {
  Scalar avg = mean(L);
  float L_mean = avg[0] * 255.0f;
  Mat L_32F = L * 255.0f;
  Mat denom;
  max(L_32F, L_mean, denom);
  Mat L_out = L_32F.mul((255.0f + k) / (denom + k));
  L_out.convertTo(L_out, CV_8U);
  return L_out;
}

void enhanceWWGIFRetinex(const Mat &src, Mat &dst) {
  Mat hsv;
  cvtColor(src, hsv, COLOR_BGR2HSV);
  vector<Mat> hsv_planes;
  split(hsv, hsv_planes);
  Mat V = hsv_planes[2];
  Mat V_32F;
  V.convertTo(V_32F, CV_32F, 1.0 / 255.0);

  // 멀티스케일 조명 추출
  int radii[3] = {15, 30, 80};
  double eps = 0.04; // 논문 5p: 0.2^2 적용

  Mat L_sum = Mat::zeros(V.size(), CV_32F);
  for (int r : radii)
    L_sum += WWGIF(V_32F, V_32F, r, eps);
  Mat L = L_sum / 3.0;

  Mat R_32F;
  divide(V_32F, L + 0.001, R_32F);

  Mat R_8U;
  R_32F.convertTo(R_8U, CV_8U, 255.0);
  Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8, 8));
  clahe->apply(R_8U, R_8U);

  // CLAHE로 증폭된 노이즈를 WWGIF로 다시 정제 (엣지는 유지)
  Mat R_tmp;
  R_8U.convertTo(R_tmp, CV_32F, 1.0 / 255.0);
  Mat R_refined = WWGIF(R_tmp, R_tmp, 5, 0.01);
  R_refined.convertTo(R_8U, CV_8U, 255.0);
  // 추가

  Mat eL = enhanceWeber(L, 30.0f);
  Mat eV, eL_32, eR_32;
  eL.convertTo(eL_32, CV_32F);
  R_8U.convertTo(eR_32, CV_32F);
  multiply(eL_32, eR_32, eV, 1.0 / 255.0);
  eV.convertTo(eV, CV_8U);

  Mat eS = adaptiveStretch(hsv_planes[1]);

  vector<Mat> merged = {hsv_planes[0], eS, eV};
  merge(merged, hsv);
  cvtColor(hsv, dst, COLOR_HSV2BGR);

  // 최종 미세 샤프닝 (Unsharp Mask)
  Mat blur;
  GaussianBlur(dst, blur, Size(0, 0), 2);
  addWeighted(dst, 1.25, blur, -0.25, 0, dst);
  // 추가
}

// [방식 1] 2024 CVPR 기반: 적응형 톤 매핑 (Zero-Shot Illumination Estimation)
// 논문 핵심: I = R * L 구조에서 L(조명)을 Fast Guided Filter로 추정하고 적응형
// 감마 적용
Mat enhanceToneMapping(const Mat &input) {
  Mat res;
  vector<Mat> channels;
  split(input, channels);

  // 조명 성분(L) 추출: Max 채널 이용 (LIME 논문 최적화 방식)
  Mat v_channel;
  cv::max(channels[0], channels[1], v_channel);
  cv::max(v_channel, channels[2], v_channel);
  v_channel.convertTo(v_channel, CV_32F, 1.0 / 255.0);

  // Fast Guided Filter: r=16, eps=0.01 (640x480 기준 최적 수치)
  Mat illumination;
  ximgproc::guidedFilter(v_channel, v_channel, illumination, 16, 0.01);

  // 적응형 감마 결정: 조명 평균이 낮을수록 강하게 보정
  Scalar mean_val = mean(illumination);
  float gamma = pow(0.5f, (0.5f - (float)mean_val[0]) / 0.5f);
  Mat lut_illumination;
  pow(illumination, gamma, lut_illumination);

  // 반영률(R) 복원 및 합성: I_out = (I_in / L) * L_enhanced
  vector<Mat> out_channels(3);
  for (int i = 0; i < 3; ++i) {
    Mat c32;
    channels[i].convertTo(c32, CV_32F, 1.0 / 255.0);
    divide(c32, illumination + 0.001f, out_channels[i]); // R 추출
    multiply(out_channels[i], lut_illumination,
             out_channels[i]); // 강화된 L 합성
  }

  merge(out_channels, res);
  res.convertTo(res, CV_8U, 255.0);
  return res;
}

// [방식 2] 2025 ICCV 기반: 가중치 다중 스케일 디테일 강화
// 논문 핵심: DoG(Difference of Gaussians)를 이용한 주파수 대역 분리 및 지각
// 기반 증폭
Mat enhanceDetailBoost(const Mat &input) {
  Mat float_img, res;
  input.convertTo(float_img, CV_32F);

  // 멀티스케일 분해 (미세 디테일 및 중간 질감 대역)
  Mat blur1, blur2;
  GaussianBlur(float_img, blur1, Size(3, 3), 1.0); // 고주파용
  GaussianBlur(float_img, blur2, Size(7, 7), 2.0); // 중주파용

  Mat detail1 = float_img - blur1; // 미세 선 (Sharpening 대상)
  Mat detail2 = blur1 - blur2;     // 형태 및 질감

  // 수치적 결정 요인: 지각 화질 향상을 위한 고정 가중치 (w1=1.5, w2=2.0)
  // 2025 논문 가이드: 중주파수 대역(detail2) 증폭이 시각적 '선명함' 체감에 더
  // 효과적
  Mat boosted = float_img + (detail1 * 1.5f) + (detail2 * 2.0f);

  boosted.setTo(0, boosted < 0);
  boosted.setTo(255, boosted > 255);
  boosted.convertTo(res, CV_8U);
  return res;
}

Mat enhanceCleanSharp(const Mat &src) {
  // [단계 1] 색공간 분리 및 색상 노이즈 제거 (2번 방식의 장점)
  Mat yuv;
  cvtColor(src, yuv, COLOR_BGR2YUV);
  vector<Mat> yuv_planes;
  split(yuv, yuv_planes);

  // U, V 채널(색상)의 무지개 노이즈 제거
  medianBlur(yuv_planes[1], yuv_planes[1], 3);
  medianBlur(yuv_planes[2], yuv_planes[2], 3);

  // [단계 2] Y(밝기) 채널에만 톤 매핑 적용 (논문 기반 가이드 필터 활용)
  Mat Y = yuv_planes[0];
  Mat Y_32F;
  Y.convertTo(Y_32F, CV_32F, 1.0 / 255.0);

  Mat illumination;
  // 가이드 필터는 노이즈를 뭉개지 않고 경계선을 보호하며 조명 지도를 만듦
  ximgproc::guidedFilter(Y_32F, Y_32F, illumination, 12, 0.01);

  // 적응형 밝기 보정 (과도한 증폭 방지 위해 감마 조절 k=0.7 부여)
  Scalar avg_illum = mean(illumination);
  float gamma = pow(0.7f, (0.5f - (float)avg_illum[0]));
  Mat enhanced_Y;
  pow(Y_32F, gamma, enhanced_Y);

  // [단계 3] 에지 보존형 디테일 강화 (노이즈는 빼고 선만 강화)
  // 2025 논문 컨셉: 가이드 필터의 잔차(Residual)를 디테일로 사용
  Mat detail = Y_32F - illumination;
  // 노이즈가 섞인 고주파 대신, 가이드 필터가 거른 '의미 있는 선'만 증폭
  enhanced_Y = enhanced_Y + (detail * 1.2f);

  // 최종 복원
  enhanced_Y.convertTo(yuv_planes[0], CV_8U, 255.0);
  merge(yuv_planes, yuv);

  Mat dst;
  cvtColor(yuv, dst, COLOR_YUV2BGR);

  // [마무리] 가벼운 Unsharp Mask로 시각적 선명도 마무리
  Mat final_blur;
  GaussianBlur(dst, final_blur, Size(0, 0), 2);
  addWeighted(dst, 1.2, final_blur, -0.2, 0, dst);

  return dst;
}

Mat enhanceUltimateBalanced(const Mat &src) {
  // [1] Lab 색공간 활용 (색상 노이즈 차단은 유지)
  Mat lab;
  cvtColor(src, lab, COLOR_BGR2Lab);
  vector<Mat> lab_planes;
  split(lab, lab_planes);

  // 색상 채널 블러 (2번의 깔끔함 유지)
  medianBlur(lab_planes[1], lab_planes[1], 3);
  medianBlur(lab_planes[2], lab_planes[2], 3);

  // [2] 밝기(L) 채널 처리
  Mat L = lab_planes[0];
  Mat L_32F;
  L.convertTo(L_32F, CV_32F, 1.0 / 255.0);

  // 조명 성분 추출
  Mat base;
  ximgproc::guidedFilter(L_32F, L_32F, base, 12, 0.01);

  // [핵심 튜닝 1] 8번의 밝기를 재현하기 위한 2단계 부스팅
  // 감마 보정을 0.5로 더 강하게 적용 (암부 가시성 대폭 향상)
  Mat enhanced_L;
  pow(base, 0.5, enhanced_L);

  // 조명 성분의 평균을 계산하여 전체적인 밝기를 한 번 더 오프셋 보정
  Scalar avg_l = mean(enhanced_L);
  if (avg_l[0] < 0.4) {
    enhanced_L += (0.4 - avg_l[0]) * 0.5; // 너무 어두우면 강제로 밑바닥 밝기 업
  }

  // [핵심 튜닝 2] 엣지 식별력 강화
  Mat detail = L_32F - base;

  // 노이즈 컷오프를 0.02에서 0.01로 낮춤 (더 미세한 엣지도 식별 가능하게)
  Mat mask;
  threshold(abs(detail), mask, 0.01, 1.0, THRESH_BINARY);

  // 6번 하이브리드의 선명함을 위해 디테일 증폭비를 1.8배로 상향
  enhanced_L = enhanced_L + (detail.mul(mask) * 1.8f);

  // [3] 안전한 클리핑 (밝기 보정 후 타는 현상 방지)
  enhanced_L.setTo(0, enhanced_L < 0);
  enhanced_L.setTo(1.0, enhanced_L > 1.0);

  // [4] 복원
  enhanced_L.convertTo(lab_planes[0], CV_8U, 255.0);
  merge(lab_planes, lab);
  Mat dst;
  cvtColor(lab, dst, COLOR_Lab2BGR);

  return dst;
}

Mat enhanceUltimateBalanced_V2(const Mat &src) {
  Mat lab;
  cvtColor(src, lab, COLOR_BGR2Lab);
  vector<Mat> lab_planes;
  split(lab, lab_planes);

  // [1] 색상 채널: 단순 블러 대신 '채도 강화' 적용
  // 7번의 약점인 낮은 Color 수치 해결
  lab_planes[1].convertTo(lab_planes[1], lab_planes[1].type(), 1.2);
  lab_planes[2].convertTo(lab_planes[2], lab_planes[2].type(), 1.2);

  // [2] 밝기(L) 채널 처리
  Mat L = lab_planes[0];
  Mat L_32F;
  L.convertTo(L_32F, CV_32F, 1.0 / 255.0);

  // 멀티스케일 가이드 필터 (미세 엣지 + 중간 형태)
  Mat base_mid, base_fine;
  ximgproc::guidedFilter(L_32F, L_32F, base_mid, 12, 0.01);  // 중간 크기
  ximgproc::guidedFilter(L_32F, L_32F, base_fine, 3, 0.005); // 미세 디테일

  // [개선] 감마 부스팅 + 오프셋 조정 (8번의 밝기 재현)
  Mat enhanced_L;
  pow(base_mid, 0.45, enhanced_L); // 감마를 0.5 -> 0.45로 약간 더 강화

  // [개선] 디테일 강화 (6번의 선명도 도입)
  Mat detail_fine = L_32F - base_fine;
  Mat detail_mid = base_fine - base_mid;

  // 미세 디테일은 2.0배, 중간 디테일은 1.5배로 차등 증폭
  enhanced_L = enhanced_L + (detail_fine * 2.0f) + (detail_mid * 1.5f);

  // [3] 안전한 클리핑 및 복원
  enhanced_L.setTo(0, enhanced_L < 0);
  enhanced_L.setTo(1.0, enhanced_L > 1.0);
  enhanced_L.convertTo(lab_planes[0], CV_8U, 255.0);

  merge(lab_planes, lab);
  Mat dst;
  cvtColor(lab, dst, COLOR_Lab2BGR);

  return dst;
}

Mat enhanceUltimateBalanced_V3(const Mat &src) {
  Mat lab;
  cvtColor(src, lab, COLOR_BGR2Lab);
  vector<Mat> lab_planes;
  split(lab, lab_planes);

  // [1] 밝기(L) 채널 처리 (기존 로직 유지하되 선명도 튜닝)
  Mat L = lab_planes[0];
  Mat L_32F;
  L.convertTo(L_32F, CV_32F, 1.0 / 255.0);

  Mat base_mid, base_fine;
  ximgproc::guidedFilter(L_32F, L_32F, base_mid, 12, 0.01);
  ximgproc::guidedFilter(L_32F, L_32F, base_fine, 3, 0.005);

  Mat enhanced_L;
  pow(base_mid, 0.48,
      enhanced_L); // 0.45에서 0.48로 조절 (너무 밝아지는 것 방지)

  Mat detail_fine = L_32F - base_fine;
  Mat detail_mid = base_fine - base_mid;
  enhanced_L = enhanced_L + (detail_fine * 1.8f) +
               (detail_mid * 1.3f); // 증폭비 살짝 하향

  // [2] 색상 채널 개선 (붉은 기 방지 핵심)
  for (int i = 1; i <= 2; ++i) {
    Mat c_32F;
    lab_planes[i].convertTo(c_32F, CV_32F);

    // 색상 중심점(128)을 기준으로 편차 계산
    Mat diff = c_32F - 128.0f;

    // 붉은 기가 너무 튀지 않게 '적응형 가중치' 적용
    // 이미 색이 진한 곳은 적게, 연한 곳은 조금 더 보정
    Mat weight;
    exp(-abs(diff) * 0.01f, weight); // 색이 강할수록 weight가 작아짐

    // 1.2배 대신 1.05~1.15배 사이에서 적응형 보정
    c_32F = 128.0f + diff.mul(1.05f + 0.1f * weight);

    c_32F.convertTo(lab_planes[i], CV_8U);
  }

  // [3] 복원
  enhanced_L.setTo(0, enhanced_L < 0);
  enhanced_L.setTo(1.0, enhanced_L > 1.0);
  enhanced_L.convertTo(lab_planes[0], CV_8U, 255.0);

  merge(lab_planes, lab);
  Mat dst;
  cvtColor(lab, dst, COLOR_Lab2BGR);

  return dst;
}

Mat enhanceUltimateBalanced_V4(const Mat &src) {
  Mat lab;
  cvtColor(src, lab, COLOR_BGR2Lab);
  vector<Mat> lab_planes;
  split(lab, lab_planes);

  // [1] 적응형 감마 결정 (논문 기반 기준)
  Mat L_32F;
  lab_planes[0].convertTo(L_32F, CV_32F, 1.0 / 255.0);
  Scalar mu_L = mean(L_32F);
  // 평균 밝기에 따른 적응형 감마 (어두울수록 강하게 보정)
  float adaptive_gamma = clamp(pow(0.5f, (0.5f - (float)mu_L[0])), 0.4f, 0.7f);

  // [2] 멀티스케일 디테일 및 '뿌연 현상' 제거 로직
  Mat base, detail;
  int r = 16; // 가이드 필터 반경 확대 (전역적 대비 확보)
  ximgproc::guidedFilter(L_32F, L_32F, base, r, 0.01);
  detail = L_32F - base;

  // 조명 맵(base)에 감마 보정 적용
  Mat enhanced_base;
  pow(base, adaptive_gamma, enhanced_base);

  // [핵심] 로컬 대비 강화: 뿌연 느낌을 없애기 위해 디테일을 base에 비례하여
  // 증폭 지각 모델: 밝은 곳보다 중간 밝기 영역의 디테일을 더 강조
  Mat detail_weight;
  multiply(enhanced_base, (1.0f - enhanced_base), detail_weight);
  Mat enhanced_L = enhanced_base + detail.mul(1.5f + 2.0f * detail_weight);

  // [3] Lab 색공간의 색상 복원 (LACE 기법 적용)
  // 밝기 변화 비율(Ratio)에 기반한 적응형 채도 강화
  Mat ratio;
  divide(enhanced_L, base + 0.01f, ratio);
  for (int i = 1; i <= 2; ++i) {
    Mat c_32F;
    lab_planes[i].convertTo(c_32F, CV_32F);
    // 밝기가 밝아진 만큼만 색차(a, b)를 정밀하게 확장 (뿌연 느낌 제거)
    c_32F = 128.0f + (c_32F - 128.0f).mul(0.8f + 0.4f * ratio);
    c_32F.convertTo(lab_planes[i], CV_8U);
  }

  // [4] 복원 및 안전 클리핑
  enhanced_L.setTo(0, enhanced_L < 0);
  enhanced_L.setTo(1.0, enhanced_L > 1.0);
  enhanced_L.convertTo(lab_planes[0], CV_8U, 255.0);

  merge(lab_planes, lab);
  Mat dst;
  cvtColor(lab, dst, COLOR_Lab2BGR);

  return dst;
}

Mat enhanceUltimateBalanced_V5(const Mat &src) {
  Mat lab;
  cvtColor(src, lab, COLOR_BGR2Lab);
  vector<Mat> lab_planes;
  split(lab, lab_planes);

  Mat L = lab_planes[0];
  Mat L_32F;
  L.convertTo(L_32F, CV_32F, 1.0 / 255.0);

  // [1] Dual-Scale 조명 추정
  Mat illumination;
  ximgproc::guidedFilter(L_32F, L_32F, illumination, 16, 0.01);

  // [2] Adaptive S-Curve Mapping
  Mat enhanced_L = Mat::zeros(L_32F.size(), CV_32F);
  for (int i = 0; i < L_32F.rows; ++i) {
    for (int j = 0; j < L_32F.cols; ++j) {
      float v = L_32F.at<float>(i, j);
      // 시그모이드 함수: 중간 톤 대비 향상
      float s_curve = 1.0f / (1.0f + exp(-10.0f * (v - 0.35f)));
      enhanced_L.at<float>(i, j) = 0.7f * pow(v, 0.4f) + 0.3f * s_curve;
    }
  }

  // [3] 고주파 디테일 증폭 (선명도 확보)
  Mat detail = L_32F - illumination;
  Mat mask;
  threshold(abs(detail), mask, 0.01, 1.0, THRESH_BINARY);
  enhanced_L = enhanced_L + detail.mul(mask) * 2.2f;

  // [4] 색상 복원: 로그 스케일 채도 보정 (에러 수정 부분)
  Mat ratio;
  divide(enhanced_L, L_32F + 0.01f, ratio);

  // log1p(x) = log(1 + x) 구현
  Mat log_input = ratio * 0.5f + 1.0f;
  Mat sat_gain;
  cv::log(log_input, sat_gain); // 행렬 원소별 로그 연산
  sat_gain = 1.0f + sat_gain;   // 최종 가중치: 1 + log(1 + ratio*0.5)

  for (int i = 1; i <= 2; ++i) {
    Mat c_32F;
    lab_planes[i].convertTo(c_32F, CV_32F);

    // 원소별 곱셈을 위해 mul 사용
    Mat res_c = 128.0f + (c_32F - 128.0f).mul(sat_gain);
    res_c.convertTo(lab_planes[i], CV_8U);
  }

  // [5] 최종 처리
  enhanced_L.setTo(0, enhanced_L < 0);
  enhanced_L.setTo(1.0, enhanced_L > 1.0);
  enhanced_L.convertTo(lab_planes[0], CV_8U, 255.0);

  merge(lab_planes, lab);
  Mat dst;
  cvtColor(lab, dst, COLOR_Lab2BGR);
  return dst;
}

// ==========================================
// 4. 메인 함수
// ==========================================
int main() {
  // 1. 이미지 리스트 정의 (수정하여 사용 가능)
  vector<string> imageList = {"gamma1.jpg", "gamma2.jpg"};

  // 2. 현재 시각 가져오기 (전체 수행에 대해 동일한 타임스탬프 설정)
  string timestamp = getCurrentTimestamp();

  for (const string &imagePath : imageList) {
    cout << "\n==========================================" << endl;
    cout << "[이미지 처리 시작] " << imagePath << endl;
    cout << "==========================================" << endl;

    // 1. 이미지 로드
    Mat src = imread(imagePath);
    if (src.empty()) {
      cout << "이미지를 로드할 수 없습니다: " << imagePath << endl;
      continue;
    }

    // 2. 파일명 동적 생성 (이미지 이름 추출)
    string baseName = imagePath;
    size_t lastDot = baseName.find_last_of(".");
    if (lastDot != string::npos)
      baseName = baseName.substr(0, lastDot);
    size_t lastSlash = baseName.find_last_of("/\\");
    if (lastSlash != string::npos)
      baseName = baseName.substr(lastSlash + 1);

    string txtFilename = baseName + "_analysis_" + timestamp + ".txt";
    string jsonFilename = baseName + "_analysis_" + timestamp + ".json";
    string imageFilename = baseName + "_result_" + timestamp + ".jpg";

    // 3. 처리 준비
    Mat base;
    resize(src, base, Size(320, 240));
    vector<Mat> results(9);

    cout << "9단계 알고리즘 연산 중..." << endl;
    enhanceRetinex(base, results[0]);
    enhanceLowLightAdvanced(base, results[1]);
    enhanceWWGIFRetinex(base, results[2]);
    results[3] = enhanceToneMapping(base);
    results[4] = enhanceDetailBoost(base);
    Mat hyb = enhanceToneMapping(base);
    results[5] = enhanceDetailBoost(hyb);
    results[6] = enhanceUltimateBalanced_V5(base);
    results[7] = enhanceCleanSharp(base);
    results[8] = base.clone();

    vector<string> labels = {
        "1. RETINEX",     "2. YUV ADV",    "3. WWGIF",
        "4. TONEMAP",     "5. DETAIL",     "6. HYBRID",
        "7. BALANCED_V5", "8. CleanSharp", "9. RAW ORIGIN"};

    // 4. 지표 분석 및 텍스트 추가 (레이아웃 합치기 전에 수행)
    struct Metrics {
      string name;
      double sharpness, colorfulness, brightness, tenengrad, noiseEst;
    };
    vector<Metrics> allMetrics;

    for (int i = 0; i < 9; ++i) {
      double s = getSharpness(results[i]);
      double c = getColorfulness(results[i]);
      double b = getBrightness(results[i]);
      double t = getTenengrad(results[i]);
      double n = getNoiseEstimate(results[i]);
      allMetrics.push_back({labels[i], s, c, b, t, n});

      // 제목 추가
      putText(results[i], labels[i], Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.6,
              Scalar(0, 0, 0), 3);
      putText(results[i], labels[i], Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.6,
              Scalar(0, 255, 255), 1);

      // 지표 정보 추가
      string info1 = format("T:%.0f Noise:%.1f", t, n);
      string info2 = format("S:%.0f C:%.1f", s, c);
      putText(results[i], info1, Point(10, 210), FONT_HERSHEY_SIMPLEX, 0.45,
              Scalar(0, 0, 0), 2);
      putText(results[i], info1, Point(10, 210), FONT_HERSHEY_SIMPLEX, 0.45,
              Scalar(255, 255, 255), 1);
      putText(results[i], info2, Point(10, 230), FONT_HERSHEY_SIMPLEX, 0.4,
              Scalar(0, 255, 255), 1);
    }

    // 5. 레이아웃 병합 (3x3 Layout)
    Mat row1, row2, row3, combined;
    hconcat(vector<Mat>{results[0], results[1], results[2]}, row1);
    hconcat(vector<Mat>{results[3], results[4], results[5]}, row2);
    hconcat(vector<Mat>{results[6], results[7], results[8]}, row3);
    vconcat(vector<Mat>{row1, row2, row3}, combined);

    // 6. 결과 출력 및 저장
    static bool windowCreated = false;
    if (!windowCreated) {
      namedWindow("Image Comparison Batch", WINDOW_NORMAL);
      windowCreated = true;
    }
    imshow("Image Comparison Batch", combined);
    imwrite(imageFilename, combined);

    // 7. 결과 로그 저장 (TXT)
    ofstream txtFile(txtFilename);
    if (txtFile.is_open()) {
      txtFile << "Analysis Time: " << timestamp << " | Source: " << imagePath
              << "\n";
      txtFile << "Algorithm Performance Metrics\n";
      txtFile << "-------------------------------------------------------------"
                 "--\n";
      txtFile << "Name\t\tSharp\tTenengrad\tNoiseEst\tColor\tBright\n";
      for (const auto &m : allMetrics) {
        txtFile << left << setw(15) << m.name << "\t" << fixed
                << setprecision(2) << m.sharpness << "\t" << m.tenengrad << "\t"
                << m.noiseEst << "\t" << m.colorfulness << "\t" << m.brightness
                << "\n";
      }
      txtFile.close();
    }

    // 8. 결과 로그 저장 (JSON)
    ofstream jsonFile(jsonFilename);
    if (jsonFile.is_open()) {
      jsonFile << "{\n  \"source\": \"" << imagePath
               << "\",\n  \"metrics\": [\n";
      for (int i = 0; i < (int)allMetrics.size(); ++i) {
        jsonFile << "    {\n";
        jsonFile << "      \"method\": \"" << allMetrics[i].name << "\",\n";
        jsonFile << "      \"sharpness\": " << allMetrics[i].sharpness << ",\n";
        jsonFile << "      \"tenengrad\": " << allMetrics[i].tenengrad << ",\n";
        jsonFile << "      \"noise_estimate\": " << allMetrics[i].noiseEst
                 << ",\n";
        jsonFile << "      \"colorfulness\": " << allMetrics[i].colorfulness
                 << ",\n";
        jsonFile << "      \"brightness\": " << allMetrics[i].brightness
                 << "\n";
        jsonFile << "    }" << (i == (int)allMetrics.size() - 1 ? "" : ",")
                 << "\n";
      }
      jsonFile << "  ]\n}";
      jsonFile.close();
    }

    cout << "분석 완료: " << imageFilename << endl;
    waitKey(30); // 다음 이미지로 넘어가기 전 잠깐 표시
  }

  cout << "\n모든 작업이 완료되었습니다. 'q'를 누르면 종료됩니다." << endl;
  while (waitKey(0) != 'q')
    ;

  return 0;
}