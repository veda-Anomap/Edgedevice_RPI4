#include <gpiod.h>
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>

static volatile std::sig_atomic_t g_run = 1;

void on_signal(int) {
    g_run = 0;
}

int main() {
    // 라즈베리파이 4/5 등 최신 환경에서는 보통 /dev/gpiochip4를 사용합니다.
    // 만약 안 된다면 /dev/gpiochip0 등으로 변경해 보세요.
    const char* chip_path = "/dev/gpiochip4"; 
    unsigned int line_offset = 16; // BCM 16
    
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // 1. 칩 열기
    struct gpiod_chip* chip = gpiod_chip_open(chip_path);
    if (!chip) {
        std::cerr << "GPIO 칩을 열 수 없습니다: " << chip_path << "\n";
        return 1;
    }

    // 2. 라인 설정 (v2 방식)
    struct gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    
    struct gpiod_line_config* line_cfg = gpiod_line_config_new();
    if (gpiod_line_config_add_line_settings(line_cfg, &line_offset, 1, settings) < 0) {
        std::cerr << "라인 설정 추가 실패\n";
        return 1;
    }

    struct gpiod_request_config* req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "rpi_heartbeat");

    // 3. 출력 요청 및 라인 획득
    struct gpiod_line_request* request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

    // 사용한 설정 객체들 해제
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_cfg);
    gpiod_request_config_free(req_cfg);

    if (!request) {
        std::cerr << "GPIO 라인 요청 실패\n";
        gpiod_chip_close(chip);
        return 1;
    }

    enum gpiod_line_value level = GPIOD_LINE_VALUE_INACTIVE;
    std::cout << "Heartbeat 시작 (v2 API): BCM" << line_offset << "\n";

    while (g_run) {
        // 토글 로직
        level = (level == GPIOD_LINE_VALUE_INACTIVE) ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
        
        if (gpiod_line_request_set_value(request, line_offset, level) < 0) {
            std::cerr << "값 설정 실패\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // 4. 정리
    gpiod_line_request_set_value(request, line_offset, GPIOD_LINE_VALUE_INACTIVE);
    gpiod_line_request_release(request);
    gpiod_chip_close(chip);

    std::cout << "\n종료되었습니다.\n";
    return 0;
}