#include "PoseEstimator.h"
#include "../../config/AppConfig.h"
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model.h>

PoseEstimator::PoseEstimator(const std::string &model_path,
                             IImagePreprocessor &preprocessor)
    : model_path_(model_path), preprocessor_(preprocessor) {}

PoseEstimator::~PoseEstimator() = default;

bool PoseEstimator::initialize() {
  if (initialized_)
    return true;

  // 모델 파일 존재 확인 (std::filesystem 사용)
  if (!std::filesystem::exists(model_path_)) {
    std::cerr << "[PoseEstimator] 모델 파일 없음: " << model_path_ << std::endl;
    return false;
  }

  model_ = tflite::FlatBufferModel::BuildFromFile(model_path_.c_str());
  if (!model_) {
    std::cerr << "[PoseEstimator] 모델 빌드 실패." << std::endl;
    return false;
  }

  tflite::ops::builtin::BuiltinOpResolver resolver;
  tflite::InterpreterBuilder(*model_, resolver)(&interpreter_);

  if (!interpreter_) {
    std::cerr << "[PoseEstimator] 인터프리터 생성 실패." << std::endl;
    return false;
  }

  interpreter_->SetNumThreads(AppConfig::NUM_THREADS);
  interpreter_->AllocateTensors();

  input_ptr_ = interpreter_->template typed_input_tensor<float>(0);
  initialized_ = true;

  std::cout << "[PoseEstimator] 모델 로드 완료: " << model_path_ << std::endl;
  return true;
}

std::vector<SingleDetection> PoseEstimator::detect(const cv::Mat &frame) {
  std::vector<SingleDetection> detections;

  if (!initialized_ || !input_ptr_)
    return detections;

  const int model_in = AppConfig::MODEL_INPUT_SIZE;

  // 전처리를 IImagePreprocessor에 위임 (DIP)
  cv::Mat preprocessed = preprocessor_.preprocess(frame);

  // 전처리 결과를 입력 텐서로 복사
  cv::Mat tensor_wrapper(model_in, model_in, CV_32FC3, input_ptr_);
  preprocessed.copyTo(tensor_wrapper);

  // 추론 실행
  interpreter_->Invoke();

  // 후처리: 출력 파싱
  float *output = interpreter_->template typed_output_tensor<float>(0);

  for (int i = 0; i < AppConfig::MAX_DETECTIONS; ++i) {
    float *data = output + (i * AppConfig::DETECTION_STRIDE);

    if (data[4] > AppConfig::CONFIDENCE_THRESHOLD) {
      SingleDetection det;

      float x1 = data[0] * AppConfig::FRAME_WIDTH;
      float y1 = data[1] * AppConfig::FRAME_HEIGHT;
      float x2 = data[2] * AppConfig::FRAME_WIDTH;
      float y2 = data[3] * AppConfig::FRAME_HEIGHT;
      det.box =
          cv::Rect(cv::Point((int)x1, (int)y1), cv::Point((int)x2, (int)y2));

      det.skeleton.resize(AppConfig::NUM_KEYPOINTS);
      for (int j = 0; j < AppConfig::NUM_KEYPOINTS; ++j) {
        det.skeleton[j] =
            cv::Point((int)(data[6 + j * 3] * AppConfig::FRAME_WIDTH),
                      (int)(data[7 + j * 3] * AppConfig::FRAME_HEIGHT));
      }

      detections.push_back(det);
    }
  }

  return detections;
}
