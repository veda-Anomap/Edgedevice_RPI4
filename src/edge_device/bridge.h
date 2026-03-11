#pragma once

#include "uart_port.h"
#include "../network/INetworkSender.h"
#include "../protocol/PacketProtocol.h"

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace edge_device {

struct BridgeConfig {
    // [CUSTOM-OPTIONAL] STM32가 기존 /dev/serial0 + 115200이면 수정 불필요
    std::string serial_port = "/dev/serial0";
    int serial_baud = 115200;

    // [CUSTOM-REQUIRED] 실제 서버 주소/포트로 변경해서 사용
    std::string server_host = "127.0.0.1";
    uint16_t server_port = 9000;

    // [CUSTOM-OPTIONAL] 환경에 따라 timeout/backoff만 조정
    int uart_timeout_ms = 1000;
    int reconnect_initial_ms = 500;
    int reconnect_max_ms = 10000;

    // [CUSTOM-OPTIONAL] 운영 환경에 맞는 로그 경로로 변경 가능
    std::string log_file = "./stm_bridge.log";
};

class Logger {
public:
    bool open(const std::string& path);
    void log(const std::string& level, const std::string& msg);

private:
    std::ofstream ofs_;
    std::mutex mu_;
};

class Bridge {
public:
    // 아래와 같이 생성자를 명시적으로 추가하여 설정값을 받도록 합니다.
    explicit Bridge(const BridgeConfig& cfg) : cfg_(cfg) {}

    void setSender(INetworkSender* sender);

    // 메인 실행 루프
    void run(std::atomic<bool>* stop_flag = nullptr);

    // 모터 제어 직접 수행 (SubCamController에서 호출)
    bool handleMotorCmd(const std::string& cmd);

private:
    bool handleStatusReq();

    static bool parseMotorCmdJson(const std::string& body_json, std::string& cmd);
    static bool validMotorCmd(const std::string& cmd);

    BridgeConfig cfg_;
    UartPort uart_;
    Logger logger_;
    INetworkSender* sender_ = nullptr;

    // [신규] 센서 데이터 수집을 위한 스레드 및 로직
    void sensorLoop(std::atomic<bool>* stop_flag);
    std::thread sensor_thread_;

    // [신규] 공유 자원 보호를 위한 뮤텍스
    std::mutex uart_mu_;   // UART 포트 보호
};

} // namespace edge_device
