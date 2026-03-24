#ifndef PROCESS_GUARD_H
#define PROCESS_GUARD_H

#include "Logger.h"
#include <string>

#ifdef __linux__
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <csignal>
#include <dirent.h>
#include <cstring>
#endif

// ================================================================
// ProcessGuard: 시스템 수준 프로세스 충돌 방지 유틸리티 (SRP)
//
// 책임: 이전 프로세스가 카메라 등 배타적 자원을 점유한 채
//       비정상 종료된 경우, 시스템 시작 전에 정리합니다.
//
// 설계 원칙:
//   - SRP: 프로세스 관리만 담당 (카메라/파이프라인 코드에서 분리)
//   - DIP: Logger를 통해 진단 출력 (std::cout 직접 사용 없음)
// ================================================================

class ProcessGuard {
public:
    // 현재 프로세스와 동일한 이름의 기존 프로세스를 정리합니다.
    // 프로그램 시작 시 main()에서 1회 호출합니다.
    static void killStaleProcesses(const std::string& process_name = "SubCameraApp") {
#ifdef __linux__
        static const std::string TAG = "ProcessGuard";
        pid_t my_pid = getpid();
        int killed_count = 0;

        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) {
            LOG_WARN(TAG, "/proc 디렉토리를 열 수 없습니다.");
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            // /proc/<pid> 형식인지 확인 (숫자로만 구성된 디렉토리)
            if (entry->d_type != DT_DIR) continue;

            pid_t pid = 0;
            bool is_numeric = true;
            for (const char* p = entry->d_name; *p; ++p) {
                if (*p < '0' || *p > '9') { is_numeric = false; break; }
                pid = pid * 10 + (*p - '0');
            }
            if (!is_numeric || pid == my_pid || pid <= 1) continue;

            // /proc/<pid>/cmdline 읽기
            std::string cmdline_path = "/proc/" + std::string(entry->d_name) + "/cmdline";
            std::ifstream cmdline_file(cmdline_path);
            if (!cmdline_file.is_open()) continue;

            std::string cmdline;
            std::getline(cmdline_file, cmdline, '\0');
            cmdline_file.close();

            // 프로세스 이름 매칭
            if (cmdline.find(process_name) != std::string::npos) {
                LOG_WARN(TAG, "이전 프로세스 발견 (PID: " + std::to_string(pid) +
                              ", CMD: " + cmdline + "). SIGTERM 전송...");

                if (kill(pid, SIGTERM) == 0) {
                    killed_count++;
                    // 정상 종료 대기 (최대 2초)
                    for (int i = 0; i < 20; ++i) {
                        usleep(100000); // 100ms
                        if (kill(pid, 0) != 0) break; // 프로세스 종료 확인
                    }
                    // 아직 살아있으면 SIGKILL
                    if (kill(pid, 0) == 0) {
                        LOG_WARN(TAG, "PID " + std::to_string(pid) +
                                      " 정상 종료 실패. SIGKILL 전송...");
                        kill(pid, SIGKILL);
                        usleep(500000); // 500ms 대기
                    }
                } else {
                    LOG_ERROR(TAG, "PID " + std::to_string(pid) +
                                   " 종료 실패 (권한 부족? sudo로 실행하세요)");
                }
            }
        }
        closedir(proc_dir);

        if (killed_count > 0) {
            LOG_INFO(TAG, "이전 프로세스 " + std::to_string(killed_count) +
                          "개 정리 완료. 카메라 자원 해제 대기 (1초)...");
            usleep(1000000); // libcamera 자원 정리 시간 확보
        } else {
            LOG_INFO(TAG, "이전 프로세스 없음. 정상 시작 가능.");
        }
#else
        // Windows/기타 OS: 미지원 (RPi 전용 기능)
        (void)process_name;
#endif
    }

    // 특정 장치 파일(예: /dev/video0)을 점유 중인 프로세스가 있는지 확인합니다.
    static bool isDeviceBusy(const std::string& device_path = "/dev/video0") {
#ifdef __linux__
        static const std::string TAG = "ProcessGuard";
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) return false;

        struct dirent* proc_entry;
        while ((proc_entry = readdir(proc_dir)) != nullptr) {
            if (proc_entry->d_type != DT_DIR) continue;

            // PID 디렉토리인지 확인
            bool is_pid = true;
            for (const char* p = proc_entry->d_name; *p; ++p) {
                if (*p < '0' || *p > '9') { is_pid = false; break; }
            }
            if (!is_pid) continue;

            // /proc/<pid>/fd 디렉토리 스캔
            std::string fd_dir_path = "/proc/" + std::string(proc_entry->d_name) + "/fd";
            DIR* fd_dir = opendir(fd_dir_path.c_str());
            if (!fd_dir) continue;

            struct dirent* fd_entry;
            while ((fd_entry = readdir(fd_dir)) != nullptr) {
                if (fd_entry->d_type != DT_LNK) continue;

                char link_target[PATH_MAX];
                std::string link_path = fd_dir_path + "/" + fd_entry->d_name;
                ssize_t len = readlink(link_path.c_str(), link_target, sizeof(link_target) - 1);
                
                if (len != -1) {
                    link_target[len] = '\0';
                    if (device_path == link_target) {
                        LOG_WARN(TAG, "장치 점유 감지: " + device_path + " (PID: " + proc_entry->d_name + ")");
                        closedir(fd_dir);
                        closedir(proc_dir);
                        return true;
                    }
                }
            }
            closedir(fd_dir);
        }
        closedir(proc_dir);
#endif
        return false;
    }
};

#endif // PROCESS_GUARD_H
