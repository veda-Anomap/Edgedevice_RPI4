#include "TesterApp.h"
#include <iostream>
#include "../config/AppConfig.h"

#include "../src/imageprocessing/ImagePreprocessor.h"

using namespace cv;
using namespace std;

TesterApp::TesterApp() : active_mode_idx_(0), is_running_(false) {
    // 1. 이미지 전처리기 (AI 입력용)
    auto preprocessor = make_unique<ImagePreprocessor>(
        AppConfig::MODEL_INPUT_SIZE, 
        cv::COLOR_BGR2RGB,           
        1.0 / 255.0,                 
        CV_32FC3                     
    );
    
    // 2. AI 디펜던시 초기화
    detector_ = make_unique<PoseEstimator>(AppConfig::MODEL_PATH, *preprocessor);
    // 소유권 이전을 피하기 위해 멤버 변수로 유지 (shared_ptr 대신 unique_ptr + 참조)
    preprocessor_ = std::move(preprocessor); 

    fall_detector_ = make_unique<FallDetector>();
    renderer_ = make_unique<FrameRenderer>();
}

bool TesterApp::initialize(const string& source) {
    if (source == "cam") {
        string pipeline = "libcamerasrc ! video/x-raw, width=640, height=480 ! videoconvert ! video/x-raw, format=BGR ! appsink";
        cap_.open(pipeline, CAP_GSTREAMER);
    } else {
        cap_.open(source);
    }

    if (!cap_.isOpened()) {
        cerr << "[Tester] Failed to open source: " << source << endl;
        return false;
    }

    // 모드 리스트 등록 (SOLID)
    modes_.push_back(make_unique<CompareMode>());
    modes_.push_back(make_unique<TuneMode>());
    modes_.push_back(make_unique<AiMode>(*detector_, *fall_detector_, *renderer_));

    modes_[active_mode_idx_]->onEnter();
    is_running_ = true;
    return true;
}

void TesterApp::run() {
    Mat frame, display;
    while (is_running_) {
        if (!cap_.read(frame)) break;

        modes_[active_mode_idx_]->process(frame, display);
        
        // 공통 UI 오버레이
        string footer = "[m] Switch Mode | [q] Quit | Current: " + modes_[active_mode_idx_]->getName();
        putText(display, footer, Point(20, display.rows - 20), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255,255,255), 1);
        
        imshow("SubCam SOLID Tester", display);

        int key = waitKey(1);
        if (key == 'q' || key == 27) is_running_ = false;
        else if (key == 'm') _switchMode();
        else {
            modes_[active_mode_idx_]->onKey(key);
        }
    }
}

void TesterApp::_switchMode() {
    active_mode_idx_ = (active_mode_idx_ + 1) % modes_.size();
    modes_[active_mode_idx_]->onEnter();
    cout << "[Tester] Switched to mode: " << modes_[active_mode_idx_]->getName() << endl;
}
