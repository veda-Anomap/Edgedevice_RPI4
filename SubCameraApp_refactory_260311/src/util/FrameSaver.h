#ifndef FRAME_SAVER_H
#define FRAME_SAVER_H

#include <string>
#include <opencv2/opencv.hpp>

// =============================================
// FrameSaver: 이미지 파일 저장 전용 (SRP)
// 디렉토리 생성 + 타임스탬프 파일명 + 저장
// =============================================

class FrameSaver {
public:
    explicit FrameSaver(const std::string& output_dir);
    ~FrameSaver() = default;

    // 낙상 프레임 저장 (트랙 ID로 파일명 생성)
    void saveFallFrame(const cv::Mat& frame, int track_id);

private:
    std::string output_dir_;
};

#endif // FRAME_SAVER_H
