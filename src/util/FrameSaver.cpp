#include "FrameSaver.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <ctime>

FrameSaver::FrameSaver(const std::string& output_dir)
    : output_dir_(output_dir) {}

void FrameSaver::saveFallFrame(const cv::Mat& frame, int track_id) {
    // <filesystem> 대신 POSIX stat/mkdir 사용
    // (TFLite 헤더와 <filesystem>의 네임스페이스 충돌 방지)
    struct stat st;
    if (stat(output_dir_.c_str(), &st) != 0) {
        mkdir(output_dir_.c_str(), 0755);
    }

    time_t now = time(0);
    tm* ltm = localtime(&now);
    char buffer[80];
    strftime(buffer, 80, "%Y%m%d_%H%M%S", ltm);

    std::string filename = output_dir_ + "/fall_id"
                           + std::to_string(track_id) + "_"
                           + std::string(buffer) + ".jpg";

    cv::imwrite(filename, frame);
    std::cout << "\n[FrameSaver] 낙상 감지! 저장됨: " << filename << std::endl;
}
