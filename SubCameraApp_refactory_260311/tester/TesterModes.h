#ifndef TESTER_MODES_H
#define TESTER_MODES_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "TesterRegistry.h"
#include "../src/ai/PoseEstimator.h"
#include "../src/ai/FallDetector.h"
#include "../src/rendering/FrameRenderer.h"

// ── 인터페이스: IAppMode ──────────────────────────────────────────
class IAppMode {
public:
    virtual ~IAppMode() = default;
    virtual void onEnter() = 0;
    virtual void process(const cv::Mat& frame, cv::Mat& display) = 0;
    virtual void onKey(int key) = 0;
    virtual std::string getName() const = 0;
};

// ── 1. CompareMode: 3x3 그리드 비교 (Tab 1) ───────────────────────
class CompareMode : public IAppMode {
public:
    CompareMode() { 
        enhancers_ = TesterRegistry::getAllEnhancers(); 
    }
    void onEnter() override { cv::destroyAllWindows(); }
    void process(const cv::Mat& frame, cv::Mat& display) override;
    void onKey(int key) override {}
    std::string getName() const override { return "9-Grid Comparison"; }

private:
    std::vector<EnhancerInfo> enhancers_;
};

// ── 2. TuneMode: 실시간 파라미터 튜닝 (Tab 2) ─────────────────────
class TuneMode : public IAppMode {
public:
    TuneMode() { 
        enhancers_ = TesterRegistry::getAllEnhancers(); 
        selected_idx_ = 0;
    }
    void onEnter() override;
    void process(const cv::Mat& frame, cv::Mat& display) override;
    void onKey(int key) override;
    std::string getName() const override { return "Interactive Tuning"; }

private:
    std::vector<EnhancerInfo> enhancers_;
    int selected_idx_;
    void _setupTrackbars();
};

// ── 3. AiMode: AI 추론 및 룰 검증 (Tab 3) ─────────────────────────
class AiMode : public IAppMode {
public:
    AiMode(PoseEstimator& det, FallDetector& rule, FrameRenderer& render)
        : detector_(det), fall_detector_(rule), renderer_(render) {}
    
    void onEnter() override { cv::destroyAllWindows(); }
    void process(const cv::Mat& frame, cv::Mat& display) override;
    void onKey(int key) override {}
    std::string getName() const override { return "AI Inference & Rule Check"; }

private:
    PoseEstimator& detector_;
    FallDetector& fall_detector_;
    FrameRenderer& renderer_;
};

#endif // TESTER_MODES_H
