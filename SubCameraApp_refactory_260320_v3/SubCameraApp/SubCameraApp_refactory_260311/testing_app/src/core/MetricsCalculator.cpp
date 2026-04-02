#include "MetricsCalculator.h"
#include <cmath>

ImageMetrics MetricsCalculator::compute(const cv::Mat& src, const std::string& name) {
    ImageMetrics m;
    m.name         = name;
    m.sharpness    = getSharpness(src);
    m.colorfulness = getColorfulness(src);
    m.brightness   = getBrightness(src);
    m.tenengrad    = getTenengrad(src);
    m.noiseEstimate= getNoiseEstimate(src);
    return m;
}

double MetricsCalculator::getSharpness(const cv::Mat& src) {
    cv::Mat gray, lap;
    if (src.channels() == 3) cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else gray = src;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma;
    cv::meanStdDev(lap, mu, sigma);
    return sigma.val[0] * sigma.val[0];
}

double MetricsCalculator::getColorfulness(const cv::Mat& src) {
    if (src.channels() != 3) return 0.0;
    cv::Mat bgr[3];
    cv::split(src, bgr);
    cv::Mat rg = cv::abs(bgr[2] - bgr[1]);
    cv::Mat yb = cv::abs(0.5 * (bgr[2] + bgr[1]) - bgr[0]);
    cv::Scalar std_rg, mean_rg, std_yb, mean_yb;
    cv::meanStdDev(rg, mean_rg, std_rg);
    cv::meanStdDev(yb, mean_yb, std_yb);
    double std_root  = std::sqrt(std_rg.val[0]*std_rg.val[0] + std_yb.val[0]*std_yb.val[0]);
    double mean_root = std::sqrt(mean_rg.val[0]*mean_rg.val[0] + mean_yb.val[0]*mean_yb.val[0]);
    return std_root + 0.3 * mean_root;
}

double MetricsCalculator::getBrightness(const cv::Mat& src) {
    cv::Mat gray;
    if (src.channels() == 3) cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else gray = src;
    return cv::mean(gray)[0];
}

double MetricsCalculator::getTenengrad(const cv::Mat& src) {
    cv::Mat gray, gx, gy, mag;
    if (src.channels() == 3) cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else gray = src;
    cv::Sobel(gray, gx, CV_64F, 1, 0);
    cv::Sobel(gray, gy, CV_64F, 0, 1);
    cv::magnitude(gx, gy, mag);
    cv::Scalar s = cv::sum(mag.mul(mag));
    return s.val[0] / (src.rows * src.cols);
}

double MetricsCalculator::getNoiseEstimate(const cv::Mat& src) {
    cv::Mat gray, diff;
    if (src.channels() == 3) cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else gray = src;
    cv::absdiff(gray(cv::Rect(0, 0, gray.cols-1, gray.rows)),
                gray(cv::Rect(1, 0, gray.cols-1, gray.rows)), diff);
    return cv::mean(diff)[0];
}

void MetricsCalculator::drawOverlay(cv::Mat& cell, const ImageMetrics& m) {
    // 제목
    cv::putText(cell, m.name, cv::Point(10, 25),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0,0,0), 3);
    cv::putText(cell, m.name, cv::Point(10, 25),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0,255,255), 1);

    // 하단 지표 (2줄)
    int y = cell.rows - 40;
    std::string line1 = cv::format("T:%.0f Ns:%.1f", m.tenengrad, m.noiseEstimate);
    std::string line2 = cv::format("S:%.0f C:%.1f B:%.0f", m.sharpness, m.colorfulness, m.brightness);
    cv::putText(cell, line1, cv::Point(8, y),
                cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(0,0,0), 2);
    cv::putText(cell, line1, cv::Point(8, y),
                cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(255,255,255), 1);
    cv::putText(cell, line2, cv::Point(8, y+18),
                cv::FONT_HERSHEY_SIMPLEX, 0.40, cv::Scalar(0,255,255), 1);
}
