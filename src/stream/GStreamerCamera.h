#ifndef GSTREAMER_CAMERA_H
#define GSTREAMER_CAMERA_H

#include "ICamera.h"

// =============================================
// GStreamerCamera: GStreamer 기반 카메라 구현 (LSP)
// ICamera 인터페이스의 구체적 구현체
// 다른 캡처 방식으로 교체 가능
// =============================================

class GStreamerCamera : public ICamera {
public:
  GStreamerCamera() = default;
  ~GStreamerCamera() override;

  bool open(const std::string &pipeline) override;
  bool read(cv::Mat &frame) override;
  void release() override;
  bool isOpened() const override;

private:
  cv::VideoCapture cap_;
};

#endif // GSTREAMER_CAMERA_H
