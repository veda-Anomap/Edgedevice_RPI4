#include "bridge.h"
#include "stm32_proto.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip>
#include <set>
#include <thread>
#include <vector>
#include <iostream>

namespace edge_device {

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

void Bridge::setSender(INetworkSender* sender) {
    sender_ = sender;
}

void Bridge::run(std::atomic<bool>* stop_flag) {
    if (!logger_.open(cfg_.log_file)) {
        std::cerr << "[Bridge] Failed to open log file: " << cfg_.log_file << std::endl;
    }

    if (!uart_.openPort(cfg_.serial_port, cfg_.serial_baud)) {
        logger_.log("ERROR", "failed to open uart: " + cfg_.serial_port);
        return;
    }
    logger_.log("INFO", "uart opened: " + cfg_.serial_port);

    // [DIP] 더 이상 deviceInfoLoop를 직접 실행하지 않음 (SubCamController가 담당)
    sensor_thread_ = std::thread(&Bridge::sensorLoop, this, stop_flag);

    while (stop_flag && !stop_flag->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (sensor_thread_.joinable()) sensor_thread_.join();

    uart_.closePort();
    logger_.log("INFO", "bridge stopped");
}

void Bridge::sensorLoop(std::atomic<bool>* stop_flag) {
    std::vector<json> sensor_buffer;
    logger_.log("INFO", "Sensor polling thread started");

    while (stop_flag && !stop_flag->load()) {
        auto next_run = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        if (uart_.isOpen()) {
            StmFrame frame;
            std::string ferr;
            bool success = false;

            {
                std::lock_guard<std::mutex> lock(uart_mu_);
                if (Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_STATUS, "")) {
                    success = Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr);
                }
            }

            if (success) {
                std::cout << "\033[1;32m[UART-RECV]\033[0m Raw Payload: " << frame.payload_json << std::endl;
            } else if (!ferr.empty()) {
                std::cerr << "\033[1;31m[UART-ERROR]\033[0m Fail to read: " << ferr << std::endl;
            }

            if (success && frame.cmd == Stm32Proto::CMD_STATUS) {
                auto st = Stm32Proto::parseStatusJson(frame.payload_json);
                if (st) {
                    sensor_buffer.push_back({
                        {"tmp", st->tmp},
                        {"hum", st->hum},
                        {"dir", st->dir},
                        {"tilt", st->tilt},
                        {"light", st->light},
                        {"ts", std::time(nullptr)}
                    });
                }
            }
        }

        if (sensor_buffer.size() >= 5) {
            std::cout << "\033[1;36m[SENSOR-BATCH]\033[0m 5 points collected. Forwarding to NetworkFacade..." << std::endl;

            if (sender_) {
                std::string out = json{{"sensor_batch", sensor_buffer}}.dump();
                sender_->sendSensorData(out);
                logger_.log("INFO", "Batch sensor data (5pts) forwarded to sender (META)");
            } else {
                std::cout << "\033[1;33m[BRIDGE-DIP]\033[0m Sender not set. Data dropped." << std::endl;
            }
            sensor_buffer.clear();
        }

        std::this_thread::sleep_until(next_run);
    }
}

bool Bridge::handleMotorCmd(const std::string& cmd) {
    if (!validMotorCmd(cmd)) {
        logger_.log("ERROR", "Invalid motor command received: " + cmd);
        return false;
    }

    const std::string payload = json{{"motor", cmd}}.dump();
    StmFrame frame;
    std::string ferr;
    bool success = false;

    {
        std::lock_guard<std::mutex> lock(uart_mu_);
        if (Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_MOTOR, payload)) {
            success = Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr);
        }
    }

    if (!success) {
        logger_.log("ERROR", "UART motor cmd failed: " + ferr);
        return false;
    }
    
    logger_.log("INFO", "Motor command relay OK: " + cmd);
    return true;
}

bool Bridge::handleStatusReq() {
    // [DIP] 이 메서드는 이제 사용되지 않거나 필요 시 NetworkFacade를 통해 응답하도록 변경 가능
    // 현재는 주기적 배치가 주 목적이므로 스텁으로 둠
    return true;
}

bool Bridge::parseMotorCmdJson(const std::string& body_json, std::string& cmd) {
    const auto j = json::parse(body_json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    if (j.contains("motor") && j.at("motor").is_string()) {
        cmd = j.at("motor").get<std::string>();
        return true;
    }
    return false;
}

bool Bridge::validMotorCmd(const std::string& cmd) {
    static const std::set<std::string> allowed = {"w", "a", "s", "d", "auto", "manual", "on", "off"};
    return allowed.find(cmd) != allowed.end();
}

} // namespace edge_device