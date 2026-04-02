#ifndef I_CAMERA_H
#define I_CAMERA_H

#include <string>
#include <opencv2/opencv.hpp>

// =============================================
// 인터페이스: 카메라 추상화 (DIP 원칙)
// 캡처 구현을 교체 가능하게 만듦
// =============================================

class ICamera {
public:
    virtual ~ICamera() = default;

    // 카메라 파이프라인 열기 (성공 시 true)
    virtual bool open(const std::string& pipeline) = 0;

    // 한 프레임 읽기 (성공 시 true)
    virtual bool read(cv::Mat& frame) = 0;

    // 카메라 자원 해제
    virtual void release() = 0;

    // 열려 있는지 확인
    virtual bool isOpened() const = 0;
};

#endif // I_CAMERA_H
