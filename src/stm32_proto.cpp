#include "stm32_proto.h"
#include "uart_port.h"

#include <regex>

std::vector<uint8_t> Stm32Proto::buildFrame(uint8_t cmd, const std::string& payload_json) {
    const uint32_t len = static_cast<uint32_t>(payload_json.size());
    std::vector<uint8_t> out;
    out.reserve(1 + 4 + payload_json.size());

    out.push_back(cmd);
    out.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(len & 0xFF));
    out.insert(out.end(), payload_json.begin(), payload_json.end());

    return out;
}

bool Stm32Proto::sendFrame(UartPort& uart, uint8_t cmd, const std::string& payload_json) {
    if (payload_json.size() > MAX_PAYLOAD) {
        return false;
    }
    return uart.writeAll(buildFrame(cmd, payload_json));
}

bool Stm32Proto::readFrame(UartPort& uart, int timeout_ms, StmFrame& frame, std::string& err) {
    std::vector<uint8_t> header;
    if (!uart.readExact(header, 5, timeout_ms)) {
        err = "uart read timeout/header fail";
        return false;
    }

    const uint8_t cmd = header[0];
    const uint32_t len = (static_cast<uint32_t>(header[1]) << 24) |
                         (static_cast<uint32_t>(header[2]) << 16) |
                         (static_cast<uint32_t>(header[3]) << 8) |
                         static_cast<uint32_t>(header[4]);

    if (len > MAX_PAYLOAD) {
        err = "invalid payload length";
        return false;
    }

    std::string payload;
    if (len > 0) {
        std::vector<uint8_t> body;
        if (!uart.readExact(body, len, timeout_ms)) {
            err = "uart read timeout/body fail";
            return false;
        }
        payload.assign(body.begin(), body.end());
    }

    frame.cmd = cmd;
    frame.payload_json = payload;
    return true;
}

std::optional<std::string> Stm32Proto::jsonString(const std::string& src, const std::string& key) {
    const std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (!std::regex_search(src, m, re) || m.size() < 2) {
        return std::nullopt;
    }
    return m[1].str();
}

std::optional<double> Stm32Proto::jsonNumber(const std::string& src, const std::string& key) {
    const std::regex re("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch m;
    if (!std::regex_search(src, m, re) || m.size() < 2) {
        return std::nullopt;
    }
    try {
        return std::stod(m[1].str());
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> Stm32Proto::jsonInt(const std::string& src, const std::string& key) {
    const std::regex re("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (!std::regex_search(src, m, re) || m.size() < 2) {
        return std::nullopt;
    }
    try {
        return std::stoi(m[1].str());
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<StatusData> Stm32Proto::parseStatusJson(const std::string& json) {
    auto tmp = jsonNumber(json, "tmp");
    auto hum = jsonNumber(json, "hum");
    auto dir = jsonString(json, "dir");
    auto tilt = jsonNumber(json, "tilt");

    if (!tmp || !hum || !dir || !tilt) {
        return std::nullopt;
    }

    StatusData d;
    d.tmp = *tmp;
    d.hum = *hum;
    d.dir = *dir;
    d.tilt = *tilt;
    return d;
}

std::optional<MotorAckData> Stm32Proto::parseMotorAckJson(const std::string& json) {
    auto ok = jsonInt(json, "ok");
    auto mode = jsonString(json, "mode");
    auto cmd = jsonString(json, "cmd");

    if (!ok || !mode || !cmd) {
        return std::nullopt;
    }

    MotorAckData d;
    d.ok = *ok;
    d.mode = *mode;
    d.cmd = *cmd;
    return d;
}
