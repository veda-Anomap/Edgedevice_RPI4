#ifndef FALL_DETECTOR_H
#define FALL_DETECTOR_H

#include <map>
#include <deque>
#include <opencv2/opencv.hpp>

// =============================================
// PersonHistory: 개인별 낙상 판정 이력 (SRP)
// =============================================

struct PersonHistory {
    int id = -1;
    std::deque<float> cy_history;     // 중심 Y 좌표 이력
    std::deque<bool> fall_votes;      // 낙상 투표 이력
    bool already_saved = false;       // 중복 저장 방지 플래그
};

// =============================================
// FallDetector: 낙상 감지 로직 (SRP)
// 순수 로직만 담당 — I/O 없음, 파일 저장 없음
// =============================================

class FallDetector {
public:
    FallDetector() = default;
    ~FallDetector() = default;

    // 낙상 판정 (박스, 키포인트, 트랙 ID)
    // 반환: true이면 낙상 확정 (시간적 투표 포함)
    bool checkFall(const cv::Rect& box,
                   const std::vector<cv::Point>& kpts,
                   int track_id);

    // 트랙 ID의 낙상 저장 상태 확인/설정
    bool isAlreadySaved(int track_id) const;
    void markSaved(int track_id);
    void clearSavedFlag(int track_id);

    // 전체 상태 초기화
    void reset();

private:
    std::map<int, PersonHistory> history_map_;
};

#endif // FALL_DETECTOR_H
