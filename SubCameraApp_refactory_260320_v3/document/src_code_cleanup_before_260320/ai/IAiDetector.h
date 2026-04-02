#ifndef I_AI_DETECTOR_H
#define I_AI_DETECTOR_H

#include <opencv2/opencv.hpp>
#include <vector>

// =============================================
// AI 감지 결과 구조체 (공용)
// =============================================

struct SingleDetection {
  cv::Rect box;
  std::vector<cv::Point> skeleton;
  bool is_falling = false;
  int track_id = -1;
};

struct DetectionResult {
  std::vector<SingleDetection> objects;
  int person_count = 0;
};

// =============================================
// 인터페이스: AI 감지기 (OCP + DIP 원칙)
// 새로운 AI 모델(행동 인식 등)을 수정 없이 추가 가능
// =============================================

class IAiDetector {
public:
  virtual ~IAiDetector() = default;

  // 모델 초기화 (성공 시 true)
  virtual bool initialize() = 0;

  // 프레임에서 객체 감지 실행
  virtual std::vector<SingleDetection> detect(const cv::Mat &frame) = 0;
};

#endif // I_AI_DETECTOR_H
