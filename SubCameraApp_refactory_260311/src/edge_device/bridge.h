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

    // [CUSTOM-OPTIONAL] 센서 패킷 전송 방식 (배치 vs 단일)
    // 1이면 안 모아서 즉시 전송, 5면 5개 모아서 전송
    int sensor_batch_size = 1;

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

    // [신규] 비동기 UART 처리를 위한 단일 스레드 구조
    void uartLoop(std::atomic<bool>* stop_flag);
    std::thread uart_thread_;

    // [신규] 스레드 안전한 명령 큐
    struct UartCommand {
        enum class Type { SENSOR_POLL, MOTOR_CMD };
        Type type;
        std::string payload; // MOTOR_CMD 일 때만 사용
    };

    std::deque<UartCommand> cmd_queue_;
    std::mutex queue_mu_;
    std::condition_variable queue_cv_;

    // 더 이상 uart_mu_ 로 외부에서 잠그지 않음 (모든 UART 접근은 uartLoop 내부에서만 수행)
};

} // namespace edge_device
