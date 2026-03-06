#include "bridge.h"
#include "stm32_proto.h"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <set>
#include <thread>

using json = nlohmann::json;

bool Logger::open(const std::string& path) {
    ofs_.open(path, std::ios::app);
    return ofs_.is_open();
}

void Logger::log(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mu_);

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv;
    localtime_r(&tt, &tmv);

    if (ofs_.is_open()) {
        ofs_ << std::put_time(&tmv, "%F %T") << " [" << level << "] " << msg << '\n';
        ofs_.flush();
    }
}

Bridge::Bridge(BridgeConfig cfg)
    : cfg_(std::move(cfg)), server_() {}

bool Bridge::init() {
    if (!logger_.open(cfg_.log_file)) {
        return false;
    }

    if (!uart_.openPort(cfg_.serial_port, cfg_.serial_baud)) {
        logger_.log("ERROR", "failed to open uart: " + cfg_.serial_port);
        return false;
    }

    logger_.log("INFO", "uart opened: " + cfg_.serial_port);
    return true;
}

void Bridge::run() {
    int backoff = cfg_.reconnect_initial_ms;

    // Main loop:
    // 1) keep TCP connection alive (reconnect with exponential backoff)
    // 2) receive one server packet
    // 3) relay to STM32 and send response packet back
    while (true) {
        if (!server_.isConnected()) {
            std::string err;
            if (!server_.connectTo(cfg_.server_host, cfg_.server_port, err)) {
                logger_.log("ERROR", "server connect fail: " + err);
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                backoff = std::min(backoff * 2, cfg_.reconnect_max_ms);
                continue;
            }
            backoff = cfg_.reconnect_initial_ms;
            logger_.log("INFO", "server connected");
        }

        MessageType type = MessageType::FAIL;
        std::string body;
        std::string err;
        if (!server_.readPacket(type, body, err)) {
            logger_.log("ERROR", "server read packet fail: " + err);
            server_.close();
            continue;
        }

        if (!handleServerPacket(type, body)) {
            logger_.log("ERROR", "failed to handle server packet");
        }
    }
}

bool Bridge::handleServerPacket(MessageType type, const std::string& body) {
    if (type == MessageType::DEVICE) {
        std::string cmd;
        if (!parseMotorCmdJson(body, cmd)) {
            return sendServerError("invalid DEVICE body json");
        }
        if (!validMotorCmd(cmd)) {
            return sendServerError("invalid motor cmd");
        }
        return handleMotorCmd(cmd);
    }

    if (type == MessageType::AVAILABLE) {
        return handleStatusReq();
    }

    return sendServerError("unsupported message type");
}

bool Bridge::handleMotorCmd(const std::string& cmd) {
    // Server DEVICE -> STM32 CMD_MOTOR frame.
    const std::string payload = json{{"motor", cmd}}.dump();
    if (!Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_MOTOR, payload)) {
        return sendServerError("uart write motor failed");
    }

    StmFrame frame;
    std::string ferr;
    if (!Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr)) {
        return sendServerError("uart read ack failed: " + ferr);
    }

    if (frame.cmd != Stm32Proto::CMD_MOTOR) {
        return sendServerError("unexpected cmd in motor ack");
    }

    auto ack = Stm32Proto::parseMotorAckJson(frame.payload_json);
    if (!ack) {
        return sendServerError("invalid motor ack json");
    }

    const std::string out = json{
        {"ok", ack->ok},
        {"mode", ack->mode},
        {"cmd", ack->cmd}
    }.dump();

    std::string err;
    if (!server_.sendPacket(MessageType::ACK, out, err)) {
        logger_.log("ERROR", "server send ACK fail: " + err);
        server_.close();
        return false;
    }

    logger_.log("INFO", "motor relay ok: " + cmd);
    return true;
}

bool Bridge::handleStatusReq() {
    // Server AVAILABLE request -> STM32 CMD_STATUS frame.
    if (!Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_STATUS, "")) {
        return sendServerError("uart write status_req failed");
    }

    StmFrame frame;
    std::string ferr;
    if (!Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr)) {
        return sendServerError("uart read status failed: " + ferr);
    }

    if (frame.cmd != Stm32Proto::CMD_STATUS) {
        return sendServerError("unexpected cmd in status frame");
    }

    auto st = Stm32Proto::parseStatusJson(frame.payload_json);
    if (!st) {
        return sendServerError("invalid status json");
    }

    const std::string out = json{
        {"tmp", st->tmp},
        {"hum", st->hum},
        {"dir", st->dir},
        {"tilt", st->tilt}
    }.dump();

    std::string err;
    if (!server_.sendPacket(MessageType::AVAILABLE, out, err)) {
        logger_.log("ERROR", "server send AVAILABLE fail: " + err);
        server_.close();
        return false;
    }

    logger_.log("INFO", "status relay ok");
    return true;
}

bool Bridge::sendServerError(const std::string& reason) {
    const std::string out = json{{"reason", reason}}.dump();

    std::string err;
    if (!server_.sendPacket(MessageType::FAIL, out, err)) {
        logger_.log("ERROR", "server send FAIL fail: " + err);
        server_.close();
        return false;
    }

    logger_.log("ERROR", reason);
    return false;
}

bool Bridge::parseMotorCmdJson(const std::string& body_json, std::string& cmd) {
    // Accept both {"motor":"w"} and {"cmd":"w"}.
    const auto j = json::parse(body_json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return false;
    }

    if (j.contains("motor") && j.at("motor").is_string()) {
        cmd = j.at("motor").get<std::string>();
        return true;
    }

    if (j.contains("cmd") && j.at("cmd").is_string()) {
        cmd = j.at("cmd").get<std::string>();
        return true;
    }

    return false;
}

bool Bridge::validMotorCmd(const std::string& cmd) {
    static const std::set<std::string> allowed = {
        "w", "a", "s", "d", "auto", "manual", "on", "off"
    };
    return allowed.find(cmd) != allowed.end();
}
