#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

using namespace std;

// 1. PSNR (신호 대 잡음비) 계산
double getPSNR(const cv::Mat& I1, const cv::Mat& I2) {
    cv::Mat s1;
    cv::absdiff(I1, I2, s1);
    s1.convertTo(s1, CV_32F);
    s1 = s1.mul(s1);
    cv::Scalar s = cv::sum(s1);
    double sse = s.val[0] + s.val[1] + s.val[2];
    if (sse <= 1e-10) return 0;
    double mse = sse / (double)(I1.channels() * I1.total());
    return 10.0 * log10((255 * 255) / mse);
}

// 2. SSIM (구조적 유사도) 계산 - 간소화 버전
double getSSIM(const cv::Mat& i1, const cv::Mat& i2) {
    const double C1 = 6.5025, C2 = 58.5225;
    cv::Mat I1, I2;
    i1.convertTo(I1, CV_32F); i2.convertTo(I2, CV_32F);
    cv::Mat I1_2 = I1.mul(I1), I2_2 = I2.mul(I2), I1_I2 = I1.mul(I2);
    cv::Mat mu1, mu2;
    cv::GaussianBlur(I1, mu1, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(I2, mu2, cv::Size(11, 11), 1.5);
    cv::Mat mu1_2 = mu1.mul(mu1), mu2_2 = mu2.mul(mu2), mu1_mu2 = mu1.mul(mu2);
    cv::Mat sigma1_2, sigma2_2, sigma12;
    cv::GaussianBlur(I1_2, sigma1_2, cv::Size(11, 11), 1.5);
    sigma1_2 -= mu1_2;
    cv::GaussianBlur(I2_2, sigma2_2, cv::Size(11, 11), 1.5);
    sigma2_2 -= mu2_2;
    cv::GaussianBlur(I1_I2, sigma12, cv::Size(11, 11), 1.5);
    sigma12 -= mu1_mu2;
    cv::Mat t1 = 2 * mu1_mu2 + C1, t2 = 2 * sigma12 + C2, t3 = t1.mul(t2);
    t1 = mu1_2 + mu2_2 + C1; t2 = sigma1_2 + sigma2_2 + C2;
    t1 = t1.mul(t2);
    cv::Mat ssim_map;
    cv::divide(t3, t1, ssim_map);
    cv::Scalar mssim = cv::mean(ssim_map);
    return (mssim.val[0] + mssim.val[1] + mssim.val[2]) / 3.0;
}

// 3. 저조도 개선 및 노이즈 억제 함수
void enhanceLowLightAdvanced(cv::Mat& src, cv::Mat& dst) {
    cv::Mat yuv;
    cv::cvtColor(src, yuv, cv::COLOR_BGR2YUV);
    std::vector<cv::Mat> channels;
    cv::split(yuv, channels);

    // [Y 채널] - 밝기 및 대비 개선
    double mean_b = cv::mean(channels[0])[0];
    double gamma = (mean_b < 50) ? 0.4 : 0.7; // 더 공격적인 감마 적용 가능
    cv::Mat lut(1, 256, CV_8U);
    for (int i = 0; i < 256; i++) lut.at<uchar>(i) = cv::saturate_cast<uchar>(pow(i / 255.0, gamma) * 255.0);
    cv::LUT(channels[0], lut, channels[0]);
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.5, cv::Size(8, 8));
    clahe->apply(channels[0], channels[0]);

    // [U, V 채널] - 무지개 노이즈 억제 (Chroma Denoising)
    // Median Filter는 엣지를 유지하면서 색상 튀는 노이즈를 잡는 데 탁월합니다.
    cv::medianBlur(channels[1], channels[1], 5); 
    cv::medianBlur(channels[2], channels[2], 5);

    cv::merge(channels, yuv);
    cv::cvtColor(yuv, dst, cv::COLOR_YUV2BGR);
    
    // 최종 샤프닝 (노이즈 제거 후 뭉개진 엣지 보정)
    cv::GaussianBlur(dst, yuv, cv::Size(0, 0), 3);
    cv::addWeighted(dst, 1.5, yuv, -0.5, 0, dst);
}

int main() {
    int WIDTH = 640, HEIGHT = 480, FPS = 30;
    string pipeline = "libcamerasrc ! video/x-raw, width=1920, height=1080, framerate=" + to_string(FPS) + "/1 ! v4l2convert ! videoscale ! video/x-raw, width=" + to_string(WIDTH) + ", height=" + to_string(HEIGHT) + " ! videoconvert ! video/x-raw, format=BGR ! appsink drop=true";

    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    if (!cap.isOpened()) return -1;

    // 비디오 저장 설정 (XVID 코덱, 비교를 위해 가로 2배 사이즈)
    cv::VideoWriter writer("output_comparison.avi", cv::VideoWriter::fourcc('X', 'V', 'I', 'D'), FPS, cv::Size(WIDTH * 2, HEIGHT));

    cv::Mat frame, enhanced;
    cout << "Recording... Press 'q' to stop." << endl;

    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        enhanceLowLightAdvanced(frame, enhanced);

        // 지표 계산
        double psnr = getPSNR(frame, enhanced);
        double ssim = getSSIM(frame, enhanced);

        // 화면 결합 및 텍스트 표시
        cv::Mat combined;
        cv::hconcat(frame, enhanced, combined);
        string label = cv::format("PSNR: %.2f dB | SSIM: %.2f", psnr, ssim);
        cv::putText(combined, label, cv::Point(20, HEIGHT - 20), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);
        cv::putText(combined, "RAW", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        cv::putText(combined, "ENHANCED", cv::Point(WIDTH + 10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

        cv::imshow("Real-time Analysis", combined);
        writer.write(combined); // avi 파일에 프레임 추가

        if (cv::waitKey(1) == 'q') break;
    }

    cap.release();
    writer.release();
    cv::destroyAllWindows();
    return 0;
}