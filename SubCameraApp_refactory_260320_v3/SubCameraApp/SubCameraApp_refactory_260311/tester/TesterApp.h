#ifndef TESTER_APP_H
#define TESTER_APP_H

#include <memory>
#include <vector>
#include <opencv2/opencv.hpp>
#include "TesterModes.h"
#include "../src/imageprocessing/ImagePreprocessor.h"

class TesterApp {
public:
    TesterApp();
    ~TesterApp() = default;

    bool initialize(const std::string& source);
    void run();

private:
    void _switchMode();
    
    cv::VideoCapture cap_;
    std::vector<std::unique_ptr<IAppMode>> modes_;
    int active_mode_idx_;
    bool is_running_;

    // AI Dependencies (Shared across modes if needed)
    std::unique_ptr<ImagePreprocessor> preprocessor_;
    std::unique_ptr<PoseEstimator> detector_;
    std::unique_ptr<FallDetector> fall_detector_;
    std::unique_ptr<FrameRenderer> renderer_;
};

#endif // TESTER_APP_H
