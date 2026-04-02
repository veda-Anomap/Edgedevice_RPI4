#include "FallDetector.h"
#include "../../config/AppConfig.h"
#include <cmath>

bool FallDetector::checkFall(const cv::Rect &box,
                             const std::vector<cv::Point> &kpts, int track_id) {
  if ((int)kpts.size() < AppConfig::NUM_KEYPOINTS)
    return false;

  auto &history = history_map_[track_id];
  history.id = track_id;

  // --- 1. 주요 관절 위치 계산 ---
  float head_y = (float)kpts[0].y;
  float shoulder_y = (kpts[5].y + kpts[6].y) * 0.5f;
  float shoulder_x = (kpts[5].x + kpts[6].x) * 0.5f;
  float hip_y = (kpts[11].y + kpts[12].y) * 0.5f;
  float hip_x = (kpts[11].x + kpts[12].x) * 0.5f;
  float knee_y = (kpts[13].y + kpts[14].y) * 0.5f;
  float ankle_y = (kpts[15].y + kpts[16].y) * 0.5f;

  // --- 2. 하이브리드 지표 계산 ---

  // A. 신체 유효 높이 (머리에서 발목까지의 수직 거리)
  float body_height = std::abs(ankle_y - head_y);
  float body_width = (float)box.width;

  // B. 수직 압축률 (정면 낙상 판단용)
  float compression_ratio = body_height / ((float)box.height + 1e-6f);

  // C. 척추 각도 계산 (어깨 중심과 골반 중심 이용)
  float dx = std::abs(shoulder_x - hip_x);
  float dy = std::abs(shoulder_y - hip_y) + 1e-6f;
  float angle = atan2(dy, dx) * 180.0 / CV_PI;

  // D. 하강 속도 분석
  float current_cy = (float)box.y + (box.height / 2.0f);
  bool velocity_cond = false;
  if (!history.cy_history.empty()) {
    float diff = current_cy - history.cy_history.back();
    if (diff > AppConfig::FALL_VELOCITY_THRESHOLD)
      velocity_cond = true;
  }
  history.cy_history.push_back(current_cy);
  if ((int)history.cy_history.size() > AppConfig::CY_HISTORY_SIZE) {
    history.cy_history.pop_front();
  }

  // --- 3. 판정 조건 결합 ---

  // 조건 1: 측면 낙상 (박스가 가로로 길고 척추가 누웠을 때)
  bool side_fall =
      (body_width > body_height * AppConfig::SIDE_FALL_ASPECT_RATIO) &&
      (angle < AppConfig::SIDE_FALL_ANGLE);

  // 조건 2: 정면 낙상 (신체가 수직으로 압축됨)
  bool frontal_fall =
      (compression_ratio < AppConfig::FRONTAL_FALL_COMPRESSION) &&
      (angle < AppConfig::FRONTAL_FALL_ANGLE);

  // 조건 3: 동적 낙상 (빠른 하강 + 기울어진 각도)
  bool dynamic_fall = velocity_cond && (angle < AppConfig::DYNAMIC_FALL_ANGLE);

  // --- 4. 오탐 방지: 앉기 자세 필터 ---
  bool is_sitting = (knee_y > hip_y) && (ankle_y > knee_y) &&
                    (angle > AppConfig::SITTING_ANGLE);

  // 최종 원시 판정
  bool raw_fall = (side_fall || frontal_fall || dynamic_fall) && !is_sitting;

  // --- 5. 시간적 안정성 (Voting) ---
  history.fall_votes.push_back(raw_fall);
  if ((int)history.fall_votes.size() > AppConfig::VOTE_WINDOW_SIZE) {
    history.fall_votes.pop_front();
  }

  int votes = 0;
  for (bool v : history.fall_votes) {
    if (v)
      votes++;
  }

  // 투표 임계점 이상이면 낙상 확정
  bool is_falling = (votes >= AppConfig::VOTE_THRESHOLD);

  return is_falling;
}

bool FallDetector::isAlreadySaved(int track_id) const {
  auto it = history_map_.find(track_id);
  if (it == history_map_.end())
    return false;
  return it->second.already_saved;
}

void FallDetector::markSaved(int track_id) {
  history_map_[track_id].already_saved = true;
}

void FallDetector::clearSavedFlag(int track_id) {
  auto it = history_map_.find(track_id);
  if (it != history_map_.end()) {
    it->second.already_saved = false;
  }
}

void FallDetector::reset() { history_map_.clear(); }
