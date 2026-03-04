#include "GStreamerCamera.h"
#include <iostream>

GStreamerCamera::~GStreamerCamera() {
    release();
}

bool GStreamerCamera::open(const std::string& pipeline) {
    cap_.open(pipeline, cv::CAP_GSTREAMER);
    if (!cap_.isOpened()) {
        std::cerr << "[GStreamerCamera] 카메라 파이프라인 열기 실패!" << std::endl;
        return false;
    }
    std::cout << "[GStreamerCamera] 카메라 파이프라인 열림." << std::endl;
    return true;
}

bool GStreamerCamera::read(cv::Mat& frame) {
    return cap_.read(frame) && !frame.empty();
}

void GStreamerCamera::release() {
    if (cap_.isOpened()) {
        cap_.release();
    }
}

bool GStreamerCamera::isOpened() const {
    return cap_.isOpened();
}
