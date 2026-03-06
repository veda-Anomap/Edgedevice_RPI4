#include "controller/SubCamController.h"
#include <atomic>
#include <csignal>
#include <opencv2/opencv.hpp>

// 전역 종료 플래그
std::atomic<bool> g_stop_requested(false);

void signalHandler(int signum) {
  // 시그널 핸들러에서는 플래그만 설정 (무거운 작업 금지)
  g_stop_requested = true;
}

int main() {
  signal(SIGINT, signalHandler);

  // [최적화] OpenCV가 내부적으로 백그라운드 스레드를 과도하게 생성하여
  // TFLite의 XNNPACK(AI 추론 스레드)와 CPU 코어를 두고 경합하는 현상을 방지
  cv::setNumThreads(1);

  SubCamController controller;

  // 종료 플래그를 전달하여 메인 루프 실행
  controller.run(&g_stop_requested);

  // run() 리턴 후 소멸자에서 안전하게 정리됨
  std::cout << "[Main] 시스템이 정상 종료되었습니다." << std::endl;
  return 0;
}
