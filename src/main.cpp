#include "controller/SubCamController.h"
#include <csignal>
#include <atomic>

// 전역 종료 플래그
std::atomic<bool> g_stop_requested(false);

void signalHandler(int signum) {
    // 시그널 핸들러에서는 플래그만 설정 (무거운 작업 금지)
    g_stop_requested = true;
}

int main() {
    signal(SIGINT, signalHandler);

    SubCamController controller;

    // 종료 플래그를 전달하여 메인 루프 실행
    controller.run(&g_stop_requested);

    // run() 리턴 후 소멸자에서 안전하게 정리됨
    std::cout << "[Main] 시스템이 정상 종료되었습니다." << std::endl;
    return 0;
}
