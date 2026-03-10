#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp>
#include <iostream>
#include <vector>
#include <cmath>

using namespace std;
using namespace cv;

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

void enhanceRetinex(const Mat& src, Mat& dst) {
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
    if (src.empty()) return;
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
    R.convertTo(R_tmp, CV_32F, 1.0/255.0); // R_8U -> R로 수정
    ximgproc::guidedFilter(R_tmp, R_tmp, R_tmp, 3, 0.01); 
    R_tmp.convertTo(R, CV_8U, 255.0);      // R_8U -> R로 수정

    Mat eL = enhanceIllumination(L, 40.0f);
    Mat el_32, er_32, ev_32, eV;
    eL.convertTo(el_32, CV_32F);
    R.convertTo(er_32, CV_32F);
    multiply(el_32, er_32, ev_32, 1.0/255.0);
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
void enhanceLowLightAdvanced(const Mat& src, Mat& dst) {
    Mat yuv;
    cvtColor(src, yuv, COLOR_BGR2YUV);
    vector<Mat> channels;
    split(yuv, channels);

    double mean_b = mean(channels[0])[0];
    double gamma = (mean_b < 50) ? 0.5 : 0.8; 
    Mat lut(1, 256, CV_8U);
    for (int i = 0; i < 256; i++) lut.at<uchar>(i) = saturate_cast<uchar>(pow(i / 255.0, gamma) * 255.0);
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
Mat WWGIF(const Mat& I, const Mat& p, int r, double eps) {
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

void enhanceWWGIFRetinex(const Mat& src, Mat& dst) {
    Mat hsv;
    cvtColor(src, hsv, COLOR_BGR2HSV);
    vector<Mat> hsv_planes;
    split(hsv, hsv_planes);
    Mat V = hsv_planes[2];
    Mat V_32F;
    V.convertTo(V_32F, CV_32F, 1.0/255.0);

    // 멀티스케일 조명 추출
    int radii[3] = {15, 30, 80};
    double eps = 0.04; // 논문 5p: 0.2^2 적용
    
    Mat L_sum = Mat::zeros(V.size(), CV_32F);
    for(int r : radii) L_sum += WWGIF(V_32F, V_32F, r, eps);
    Mat L = L_sum / 3.0;

    Mat R_32F;
    divide(V_32F, L + 0.001, R_32F);

    Mat R_8U;
    R_32F.convertTo(R_8U, CV_8U, 255.0);
    Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8, 8));
    clahe->apply(R_8U, R_8U);
    
    // CLAHE로 증폭된 노이즈를 WWGIF로 다시 정제 (엣지는 유지)
    Mat R_tmp;
    R_8U.convertTo(R_tmp, CV_32F, 1.0/255.0);
    Mat R_refined = WWGIF(R_tmp, R_tmp, 5, 0.01); 
    R_refined.convertTo(R_8U, CV_8U, 255.0);
    //추가

    Mat eL = enhanceWeber(L, 30.0f);
    Mat eV, eL_32, eR_32;
    eL.convertTo(eL_32, CV_32F);
    R_8U.convertTo(eR_32, CV_32F);
    multiply(eL_32, eR_32, eV, 1.0/255.0);
    eV.convertTo(eV, CV_8U);

    Mat eS = adaptiveStretch(hsv_planes[1]);

    vector<Mat> merged = {hsv_planes[0], eS, eV};
    merge(merged, hsv);
    cvtColor(hsv, dst, COLOR_HSV2BGR);

    // 최종 미세 샤프닝 (Unsharp Mask)
    Mat blur;
    GaussianBlur(dst, blur, Size(0, 0), 2);
    addWeighted(dst, 1.25, blur, -0.25, 0, dst);
    //추가
}

// [방식 1] 2024 CVPR 기반: 적응형 톤 매핑 (Zero-Shot Illumination Estimation)
// 논문 핵심: I = R * L 구조에서 L(조명)을 Fast Guided Filter로 추정하고 적응형 감마 적용
Mat enhanceToneMapping(const Mat& input) {
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
    for(int i=0; i<3; ++i) {
        Mat c32;
        channels[i].convertTo(c32, CV_32F, 1.0 / 255.0);
        divide(c32, illumination + 0.001f, out_channels[i]); // R 추출
        multiply(out_channels[i], lut_illumination, out_channels[i]); // 강화된 L 합성
    }

    merge(out_channels, res);
    res.convertTo(res, CV_8U, 255.0);
    return res;
}

// [방식 2] 2025 ICCV 기반: 가중치 다중 스케일 디테일 강화
// 논문 핵심: DoG(Difference of Gaussians)를 이용한 주파수 대역 분리 및 지각 기반 증폭
Mat enhanceDetailBoost(const Mat& input) {
    Mat float_img, res;
    input.convertTo(float_img, CV_32F);

    // 멀티스케일 분해 (미세 디테일 및 중간 질감 대역)
    Mat blur1, blur2;
    GaussianBlur(float_img, blur1, Size(3, 3), 1.0); // 고주파용
    GaussianBlur(float_img, blur2, Size(7, 7), 2.0); // 중주파용

    Mat detail1 = float_img - blur1; // 미세 선 (Sharpening 대상)
    Mat detail2 = blur1 - blur2;     // 형태 및 질감

    // 수치적 결정 요인: 지각 화질 향상을 위한 고정 가중치 (w1=1.5, w2=2.0)
    // 2025 논문 가이드: 중주파수 대역(detail2) 증폭이 시각적 '선명함' 체감에 더 효과적
    Mat boosted = float_img + (detail1 * 1.5f) + (detail2 * 2.0f);

    boosted.setTo(0, boosted < 0);
    boosted.setTo(255, boosted > 255);
    boosted.convertTo(res, CV_8U);
    return res;
}

Mat enhanceCleanSharp(const Mat& src) {
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

Mat enhanceUltimateBalanced(const Mat& src) {
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
    L.convertTo(L_32F, CV_32F, 1.0/255.0);

    // 조명 성분 추출
    Mat base;
    ximgproc::guidedFilter(L_32F, L_32F, base, 12, 0.01);

    // [핵심 튜닝 1] 8번의 밝기를 재현하기 위한 2단계 부스팅
    // 감마 보정을 0.5로 더 강하게 적용 (암부 가시성 대폭 향상)
    Mat enhanced_L;
    pow(base, 0.5, enhanced_L); 
    
    // 조명 성분의 평균을 계산하여 전체적인 밝기를 한 번 더 오프셋 보정
    Scalar avg_l = mean(enhanced_L);
    if(avg_l[0] < 0.4) {
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
// ==========================================
// 4. 메인 함수
// ==========================================
int main() {
    Mat src = imread("gamma1.jpg");s
    if (src.empty()) {
        cout << "이미지를 찾을 수 없습니다!" << endl;
        return -1;
    }
    
    // 8개 출력을 위해 크기를 조금 더 줄임 (480x360 -> 320x240 권장)
    // 라즈베리파이 메모리 및 화면 해상도 고려
    Mat base;
    resize(src, base, Size(400, 300)); 

    // 결과 저장용 Mat 배열
    vector<Mat> results(8);

    cout << "알고리즘 연산 시작..." << endl;

    // 1~3: 기존 방식
    enhanceRetinex(base, results[0]);
    enhanceLowLightAdvanced(base, results[1]);
    enhanceWWGIFRetinex(base, results[2]);

    // 4~6: 논문 기반 및 하이브리드
    results[3] = enhanceToneMapping(base);
    results[4] = enhanceDetailBoost(base);
    results[5] = enhanceToneMapping(base);
    results[5] = enhanceDetailBoost(results[5]);

    // 7: 최적화된 CleanSharp (색상 노이즈 억제형)
    results[6] = enhanceUltimateBalanced(base);

    // 8: Custome (다중 중첩 방식 - 테스트용)
    // 연산 순서: WWGIF -> ToneMap -> Detail -> YUV_Adv
    Mat tmp;
    enhanceWWGIFRetinex(base, tmp);
    tmp = enhanceToneMapping(tmp);
    tmp = enhanceDetailBoost(tmp);
    enhanceLowLightAdvanced(tmp, results[7]);

    // 라벨링 및 텍스트 추가
    vector<string> labels = {
        "1. RETINEX", "2. YUV ADV", "3. WWGIF", "4. TONEMAP",
        "5. DETAIL", "6. HYBRID", "7. 2+5+8", "8. CUSTOME"
    };

    for(int i=0; i<8; ++i) {
        putText(results[i], labels[i], Point(15, 30), 
                FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 255), 2);
    }

    // 2x4 레이아웃 병합 (가로로 4개씩 두 줄)
    Mat row1, row2, combined;
    hconcat(vector<Mat>{results[0], results[1], results[2], results[3]}, row1);
    hconcat(vector<Mat>{results[4], results[5], results[6], results[7]}, row2);
    vconcat(row1, row2, combined);

    // 결과 출력
    namedWindow("8-Way Comparison (2x4 Layout)", WINDOW_NORMAL);
    imshow("8-Way Comparison (2x4 Layout)", combined);
    
    cout << "비교 완료. 'q'를 누르면 종료됩니다." << endl;
    while(waitKey(0) != 'q');

    return 0;
}