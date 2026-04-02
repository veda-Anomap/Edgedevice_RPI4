#include "GStreamerCamera.h"
#include "../util/Logger.h"
#include "../util/ProcessGuard.h" // Added for isDeviceBusy
#include <chrono>
#include <thread>

static const std::string TAG = "GStreamerCamera";

GStreamerCamera::~GStreamerCamera() {
    release();
}

bool GStreamerCamera::open(const std::string& pipeline) {
    // [보안/안정성] 하드웨어 자원 충돌 방지 (PipeWire 등 타 프로세스 점유 확인)
    if (ProcessGuard::isDeviceBusy("/dev/video0")) {
        LOG_ERROR(TAG, "하드웨어 충돌: /dev/video0 장치가 이미 다른 프로세스(PipeWire 등)에 의해 점유 중입니다.");
        LOG_ERROR(TAG, "해결법: sudo systemctl disable --now pipewire pipewire.socket wireplumber");
        return false; // 열기 시도 전 중단 (RequestWrap Assertion 방지)
    }

    // [SRP] 이미 열려있는 경우 먼저 해제
    if (cap_ && cap_->isOpened()) {
        LOG_WARN(TAG, "카메라가 이미 열려있습니다. 기존 연결을 해제합니다...");
        release();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    LOG_INFO(TAG, "카메라 파이프라인 열기 시도...");
    LOG_DEBUG(TAG, "Pipeline: " + pipeline);

    // [Lazy Initialization] 실제로 open()할 때만 VideoCapture 객체 생성
    // GStreamer 라이브러리가 초기화 과정에서 시스템 장치를 프로빙(Probing)하는 것을 늦추어 충돌 방지
    cap_ = std::make_unique<cv::VideoCapture>();
    cap_->open(pipeline, cv::CAP_GSTREAMER);

    if (!cap_->isOpened()) {
        LOG_ERROR(TAG, "카메라 파이프라인 열기 실패!");
        cap_.reset(); // 실패 시 객체 제거
        return false;
    }

    LOG_INFO(TAG, "카메라 파이프라인 정상 열림.");
    return true;
}

bool GStreamerCamera::read(cv::Mat& frame) {
    if (!cap_) return false;
    return cap_->read(frame) && !frame.empty();
}

void GStreamerCamera::release() {
    if (cap_) {
        if (cap_->isOpened()) {
            LOG_INFO(TAG, "카메라 리소스 해제 중...");
            cap_->release();
            LOG_INFO(TAG, "카메라 리소스 해제 완료.");
        }
        cap_.reset(); // [중요] GStreamer 객체 자체를 소멸시켜 백엔드 자원을 완전히 반환
    }
}

bool GStreamerCamera::isOpened() const {
    return cap_ && cap_->isOpened();
}
