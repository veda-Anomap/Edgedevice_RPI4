#ifndef POSE_ESTIMATOR_H
#define POSE_ESTIMATOR_H

#include "../imageprocessing/IImagePreprocessor.h"
#include "IAiDetector.h"
#include <memory>
#include <string>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/model.h>

// =============================================
// PoseEstimator: TFLite 기반 포즈 추정 (SRP + DIP)
// IAiDetector 구현체 — 모델 로드 + 추론만 담당
// 전처리는 IImagePreprocessor에 위임 (DIP)
// =============================================

class PoseEstimator : public IAiDetector {
public:
  // 의존성 주입: 전처리기 참조 (DIP 원칙)
  PoseEstimator(const std::string &model_path,
                IImagePreprocessor &preprocessor);
  ~PoseEstimator() override;

  // 모델 초기화 (TFLite 로드 + 텐서 할당)
  bool initialize() override;

  // 프레임에서 포즈 감지 실행
  // 내부적으로 preprocessor_.preprocess() 호출 후 추론
  std::vector<SingleDetection> detect(const cv::Mat &frame) override;

private:
  std::string model_path_;
  IImagePreprocessor &preprocessor_; // 전처리기 참조 (DIP)

  std::unique_ptr<tflite::Interpreter> interpreter_;
  std::unique_ptr<tflite::FlatBufferModel> model_;
  float *input_ptr_ = nullptr;
  bool initialized_ = false;
};

#endif // POSE_ESTIMATOR_H
