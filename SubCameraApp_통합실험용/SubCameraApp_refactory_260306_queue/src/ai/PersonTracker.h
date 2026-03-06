#ifndef PERSON_TRACKER_H
#define PERSON_TRACKER_H

#include <map>
#include <opencv2/opencv.hpp>

// =============================================
// PersonTracker: 센트로이드 기반 트래킹 (SRP)
// ID 할당만 담당 — 감지/낙상 로직과 분리
// =============================================

class PersonTracker {
public:
    PersonTracker() = default;
    ~PersonTracker() = default;

    // 감지된 중심점으로 트래킹 ID 할당
    // 기존 트랙과 가까우면 해당 ID, 아니면 새 ID 부여
    int assignId(const cv::Point& center);

    // 트래킹 상태 초기화
    void reset();

private:
    // 트랙 ID → 마지막 중심점
    std::map<int, cv::Point> tracks_;
    int next_id_ = 0;
};

#endif // PERSON_TRACKER_H
