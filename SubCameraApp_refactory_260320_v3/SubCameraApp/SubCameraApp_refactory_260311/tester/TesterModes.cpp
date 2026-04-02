#include "TesterModes.h"
#include <iostream>

using namespace cv;
using namespace std;

// ── 1. CompareMode Implementation ──────────────────────────────────
void CompareMode::process(const Mat& frame, Mat& display) {
    int cell_w = 640 / 2; // 9분할 시 작게 표시
    int cell_h = 480 / 2;
    
    Mat canvas = Mat::zeros(hconcat_ready_size, CV_8UC3); // Actually handled by 3x3 grid logic
    // ... Simplified 3x3 Grid
    vector<Mat> cells;
    for(int i=0; i<9; ++i) {
        Mat res;
        if(enhancers_[i].enhancer) {
            enhancers_[i].enhancer->enhance(frame, res);
        } else {
            res = frame.clone();
        }
        resize(res, res, Size(426, 320)); // 3x3 to fit ~1280x960
        putText(res, enhancers_[i].name, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0,255,0), 2);
        cells.push_back(res);
    }
    
    Mat row1, row2, row3;
    hconcat(vector<Mat>{cells[0], cells[1], cells[2]}, row1);
    hconcat(vector<Mat>{cells[3], cells[4], cells[5]}, row2);
    hconcat(vector<Mat>{cells[6], cells[7], cells[8]}, row3);
    vconcat(vector<Mat>{row1, row2, row3}, display);
}

// ── 2. TuneMode Implementation ─────────────────────────────────────
static int g_k = 40, g_gamma = 50, g_clips = 20, g_w1 = 15, g_w2 = 20, g_s = 22;

void TuneMode::onEnter() {
    destroyAllWindows();
    namedWindow("Interactive Tuning");
    _setupTrackbars();
}

void TuneMode::_setupTrackbars() {
    createTrackbar("Alg(1-9)", "Interactive Tuning", &selected_idx_, 8);
    createTrackbar("Retinex K", "Interactive Tuning", &g_k, 200);
    createTrackbar("YUV Gamma", "Interactive Tuning", &g_gamma, 200);
    createTrackbar("CLAHE Clip", "Interactive Tuning", &g_clips, 100);
    createTrackbar("W1(Detail)", "Interactive Tuning", &g_w1, 50);
    createTrackbar("W2(Forms)", "Interactive Tuning", &g_w2, 50);
    createTrackbar("S(Balanced)", "Interactive Tuning", &g_s, 50);
}

void TuneMode::process(const Mat& frame, Mat& display) {
    auto info = enhancers_[selected_idx_];
    
    // 파라미터 동기화
    if (auto e = dynamic_cast<RetinexEnhancer*>(info.enhancer.get())) e->setK((float)g_k);
    if (auto e = dynamic_cast<YuvAdvancedEnhancer*>(info.enhancer.get())) {
        e->setGamma((float)g_gamma * 0.01f);
        e->setClips((float)g_clips * 0.1f);
    }
    if (auto e = dynamic_cast<DetailBoostEnhancer*>(info.enhancer.get())) {
        e->setW1((float)g_w1 * 0.1f);
        e->setW2((float)g_w2 * 0.1f);
    }
    if (auto e = dynamic_cast<UltimateBalancedEnhancer*>(info.enhancer.get())) {
        e->setSStrength((float)g_s * 0.1f);
    }

    Mat res;
    if (info.enhancer) {
        info.enhancer->enhance(frame, res);
    } else {
        res = frame.clone();
    }
    
    display = res;
    string title = "Alg: " + info.name;
    putText(display, title, Point(20, 40), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0,255,255), 2);
    imshow("Interactive Tuning", display);
}

void TuneMode::onKey(int key) {
    if (key >= '1' && key <= '9') {
        selected_idx_ = key - '1';
        setTrackbarPos("Alg(1-9)", "Interactive Tuning", selected_idx_);
    }
}

// ── 3. AiMode Implementation ───────────────────────────────────────
void AiMode::process(const Mat& frame, Mat& display) {
    auto dets = detector_.detect(frame);
    DetectionResult result;
    result.person_count = dets.size();
    
    for(size_t i=0; i<dets.size(); ++i) {
        auto r = fall_detector_.checkFall(dets[i].box, dets[i].skeleton, (int)i);
        result.objects.push_back({(int)i, dets[i].box, dets[i].skeleton, r.is_falling});
    }
    
    display = frame.clone();
    renderer_.drawDetections(display, result);
    
    if(!result.objects.empty()) {
        string status = result.objects[0].is_falling ? "FALL DETECTED!" : "STABLE";
        Scalar color = result.objects[0].is_falling ? Scalar(0,0,255) : Scalar(0,255,0);
        putText(display, status, Point(50, 100), FONT_HERSHEY_SIMPLEX, 1.5, color, 3);
    }
}
