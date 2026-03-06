#pragma once

#include "server_client.h"
#include "uart_port.h"

#include <fstream>
#include <mutex>
#include <string>

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
    explicit Bridge(BridgeConfig cfg);
    bool init();
    void run();

private:
    bool handleServerPacket(MessageType type, const std::string& body);
    bool handleMotorCmd(const std::string& cmd);
    bool handleStatusReq();

    bool sendServerError(const std::string& reason);

    static bool parseMotorCmdJson(const std::string& body_json, std::string& cmd);
    static bool validMotorCmd(const std::string& cmd);

    BridgeConfig cfg_;
    ServerClient server_;
    UartPort uart_;
    Logger logger_;
};
