#include "FallDetector.h"
#include "../../config/AppConfig.h"
#include <chrono>
#include <cmath>
#include <iostream>

bool FallDetector::checkFall(const cv::Rect &box,
                             const std::vector<cv::Point> &kpts, int track_id) {
  if ((int)kpts.size() < AppConfig::NUM_KEYPOINTS)
    return false;

  auto &history = history_map_[track_id];
  history.id = track_id;

  // --- 1. 시간 간격 (dt) 계산 ---
  auto now = std::chrono::steady_clock::now();
  double current_time = std::chrono::duration<double>(now.time_since_epoch()).count();
  double dt = (history.last_update_time > 0) ? (current_time - history.last_update_time) : 0.033;
  history.last_update_time = current_time;

  // --- 2. 주요 관절 위치 및 몸통 길이 계산 (정규화용) ---
  float head_y = (float)kpts[0].y;
  float shoulder_y = (kpts[5].y + kpts[6].y) * 0.5f;
  float shoulder_x = (kpts[5].x + kpts[6].x) * 0.5f;
  float hip_y = (kpts[11].y + kpts[12].y) * 0.5f;
  float hip_x = (kpts[11].x + kpts[12].x) * 0.5f;
  float ankle_y = (kpts[15].y + kpts[16].y) * 0.5f;

  // 몸통 길이 (어깨-골반 거리) 계산 및 필터링
  float torso_length = std::sqrt(std::pow(shoulder_x - hip_x, 2) + std::pow(shoulder_y - hip_y, 2)) + 1e-6f;
  if (history.filtered_torso_length < 0) {
      history.filtered_torso_length = torso_length;
  } else {
      history.filtered_torso_length = 0.8f * history.filtered_torso_length + 0.2f * torso_length;
  }

  // --- 3. 정규화된 물리 지표 계산 ---
  
  // A. 정규화된 하강 속도 (Torso Lengths / Second)
  float current_cy = (float)box.y + (box.height / 2.0f);
  float norm_velocity = 0.0f;
  if (!history.cy_history.empty() && dt > 0.001) {
      float pixel_velocity = (current_cy - history.cy_history.back()) / dt;
      norm_velocity = pixel_velocity / history.filtered_torso_length;
  }
  history.cy_history.push_back(current_cy);
  if ((int)history.cy_history.size() > AppConfig::CY_HISTORY_SIZE) history.cy_history.pop_front();

  // B. 압축률 및 척추 각도
  float body_height = std::abs(ankle_y - head_y);
  float compression_ratio = body_height / ((float)box.height + 1e-6f);
  float dx = std::abs(shoulder_x - hip_x);
  float dy = std::abs(shoulder_y - hip_y) + 1e-6f;
  float angle = atan2(dy, dx) * 180.0 / CV_PI;

  // --- 4. 상태 머신 탈피 (1 FPS 대응 Stateless 로직) ---
  // AI 연산 지연(1 FPS)으로 인해 과거 트래킹 정보와 속도 기록 유지가 보장되지 않음
  // 따라서 1.5초 대기(연속성) 등 지연 판정 로직을 제외하고, 단일 프레임 상태를 기반으로 판정
  
  bool is_horizontal = angle < AppConfig::DYNAMIC_FALL_ANGLE;
  bool is_on_ground = head_y > (AppConfig::GROUND_ZONE_RATIO * AppConfig::FRAME_HEIGHT);
  bool raw_fall = is_horizontal && is_on_ground;

  // 레거시 측면 낙상 및 정면 낙상 보완 (단일 프레임 기준)
  bool legacy_side = (box.width > body_height * AppConfig::SIDE_FALL_ASPECT_RATIO) && (angle < AppConfig::SIDE_FALL_ANGLE);
  bool frontal_fall = (compression_ratio < AppConfig::FRONTAL_FALL_COMPRESSION) && (angle < AppConfig::FRONTAL_FALL_ANGLE);

  bool is_falling_final = raw_fall || legacy_side || frontal_fall;

  // UI 또는 로깅 표시용으로 상태명만 업데이트
  if (is_falling_final) {
      history.state = LYING_DOWN;
  } else {
      history.state = STANDING;
  }

  return is_falling_final;
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
