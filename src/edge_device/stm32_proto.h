#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace edge_device {

class UartPort;

struct StmFrame {
    uint8_t cmd;
    std::string payload_json;
};

struct StatusData {
    double tmp = 0.0;
    double hum = 0.0;
    std::string dir;
    double tilt = 0.0;
};

struct MotorAckData {
    int ok = 0;
    std::string mode;
    std::string cmd;
};

class Stm32Proto {
public:
    static constexpr uint8_t CMD_MOTOR = 0x04;
    static constexpr uint8_t CMD_STATUS = 0x05;
    static constexpr uint32_t MAX_PAYLOAD = 4096;

    // Build/parse UART frame: [CMD:1][LEN:4, BE][PAYLOAD:JSON bytes]
    static std::vector<uint8_t> buildFrame(uint8_t cmd, const std::string& payload_json);
    static bool sendFrame(UartPort& uart, uint8_t cmd, const std::string& payload_json);
    static bool readFrame(UartPort& uart, int timeout_ms, StmFrame& frame, std::string& err);

    // Parse STM JSON payloads; returns nullopt on parse/type/field validation failure.
    static std::optional<StatusData> parseStatusJson(const std::string& payload_json);
    static std::optional<MotorAckData> parseMotorAckJson(const std::string& payload_json);
};

} // namespace edge_device
