#include "PersonTracker.h"
#include "../../config/AppConfig.h"

int PersonTracker::assignId(const cv::Point& center) {
    int matched_id = -1;
    float min_dist = AppConfig::TRACKER_MAX_DISTANCE;

    // 기존 트랙 중 가장 가까운 것 찾기
    for (auto& [id, last_center] : tracks_) {
        float dist = (float)cv::norm(center - last_center);
        if (dist < min_dist) {
            min_dist = dist;
            matched_id = id;
        }
    }

    // 매칭 실패 시 새 ID 부여
    if (matched_id == -1) {
        matched_id = next_id_++;
    }

    // 위치 업데이트
    tracks_[matched_id] = center;

    return matched_id;
}

void PersonTracker::reset() {
    tracks_.clear();
    next_id_ = 0;
}
