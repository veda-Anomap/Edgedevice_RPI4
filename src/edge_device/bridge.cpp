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

void Bridge::run(std::atomic<bool>* stop_flag) {
    int backoff = cfg_.reconnect_initial_ms;

    // [제안 반영] 백그라운드 센서 폴링 스레드 시작
    sensor_thread_ = std::thread(&Bridge::sensorLoop, this, stop_flag);

    while (true) {
        if (stop_flag && stop_flag->load()) {
            logger_.log("INFO", "stop signal received, exiting bridge loop");
            break;
        }

        if (!server_.isConnected()) {
            std::string err;
            if (!server_.connectTo(cfg_.server_host, cfg_.server_port, err)) {
                logger_.log("ERROR", "server connect fail: " + err);
                for (int elapsed = 0; elapsed < backoff; elapsed += 100) {
                    if (stop_flag && stop_flag->load()) break;
                    int sleep_ms = std::min(100, backoff - elapsed);
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                }
                backoff = std::min(backoff * 2, cfg_.reconnect_max_ms);
                continue;
            }
            backoff = cfg_.reconnect_initial_ms;
            logger_.log("INFO", "server connected");
        }

        clearPendingPackets();
        std::atomic<bool> disconnected{false};
        std::thread reader(&Bridge::readerLoop, this, std::ref(disconnected));

        while (true) {  
            if (stop_flag && stop_flag->load()) {
                disconnected.store(true);
                server_.close();
                queue_cv_.notify_all();
                break;
            }

            PendingPacket pkt;
            if (!popPacket(pkt, disconnected, stop_flag)) {
                if (disconnected.load()) break;
                continue;
            }

            if (disconnected.load()) {
                clearPendingPackets();
                break;
            }

            if (!handleServerPacket(pkt.type, pkt.body)) {
                logger_.log("ERROR", "failed to handle server packet");
            }
        }

        if (reader.joinable()) reader.join();
        server_.close();

        if (stop_flag && stop_flag->load()) break;
    }

    // [제안 반영] 종료 시 센서 스레드 대기
    if (sensor_thread_.joinable()) sensor_thread_.join();

    server_.close();
    uart_.closePort();
    logger_.log("INFO", "bridge stopped");
}

// [제안 반영] 1초 주기 폴링 및 5초 주기 서버 전송 핵심 로직
void Bridge::sensorLoop(std::atomic<bool>* stop_flag) {
    std::vector<json> sensor_buffer;
    logger_.log("INFO", "Sensor polling thread started");

    while (stop_flag && !stop_flag->load()) {
        auto next_run = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        // 1. STM32에서 센서 데이터 1개 읽기
        if (uart_.isOpen()) {
            StmFrame frame;
            std::string ferr;
            bool success = false;

            {
                // UART 자원 선점 방지를 위한 뮤텍스 락
                std::lock_guard<std::mutex> lock(uart_mu_);
                if (Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_STATUS, "")) {
                    success = Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr);
                }
            }

            // [추가] UART 응답 확인을 위한 디버그 출력
            if (success) {
                // 초록색 텍스트로 UART 수신 성공 표시
                std::cout << "\033[1;32m[UART-RECV]\033[0m Raw Payload: " << frame.payload_json << std::endl;
            } else if (!ferr.empty()) {
                // 빨간색 텍스트로 에러 표시
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
                        {"ts", std::time(nullptr)} // 타임스탬프 추가
                    });
                }
            }
        }

        // 2. 5개가 쌓였는지 확인 (5초 경과)
        if (sensor_buffer.size() >= 5) {
            std::cout << "\033[1;36m[SENSOR-BATCH]\033[0m 5 points collected. Attempting to send to server..." << std::endl;

            std::string out = json{{"sensor_batch", sensor_buffer}}.dump();
            std::string serr;

            {
                // 서버 전송 자원 보호를 위한 뮤텍스 락
                std::lock_guard<std::mutex> lock(server_mu_);
                if (server_.isConnected()) {
                    if (server_.sendPacket(MessageType::AVAILABLE, out, serr)) {
                        logger_.log("INFO", "Batch sensor data (5pts) sent to server");
                    } else {
                        logger_.log("ERROR", "Batch send fail: " + serr);
                    }
                }
                else {
                    // [추가] 서버 미연결 시 경고
                    std::cout << "\033[1;33m[SERVER-OFFLINE]\033[0m Batch collected but server not connected." << std::endl;
                }
            }
            sensor_buffer.clear();
        }

        // 정확히 1초 주기를 유지하기 위한 정밀 sleep
        std::this_thread::sleep_until(next_run);
    }
}

void Bridge::readerLoop(std::atomic<bool>& disconnected) {
    while (!disconnected.load()) {
        MessageType type = MessageType::FAIL;
        std::string body;
        std::string err;
        if (!server_.readPacket(type, body, err)) {
            logger_.log("ERROR", "server read packet fail: " + err);
            disconnected.store(true);
            server_.close();
            clearPendingPackets();
            queue_cv_.notify_all();
            return;
        }
        enqueuePacket(type, std::move(body));
    }
}

void Bridge::enqueuePacket(MessageType type, std::string body) {
    std::lock_guard<std::mutex> lock(queue_mu_);
    if (pending_packets_.size() >= MAX_PENDING_PACKETS) {
        pending_packets_.pop_front();
        logger_.log("ERROR", "packet queue overflow: dropped oldest packet");
    }
    pending_packets_.push_back(PendingPacket{type, std::move(body)});
    queue_cv_.notify_one();
}

bool Bridge::popPacket(PendingPacket& out, const std::atomic<bool>& disconnected,
                       const std::atomic<bool>* stop_flag) {
    std::unique_lock<std::mutex> lock(queue_mu_);
    queue_cv_.wait_for(lock, std::chrono::milliseconds(200), [&] {
        return !pending_packets_.empty() || disconnected.load() ||
               (stop_flag && stop_flag->load());
    });

    if (pending_packets_.empty()) return false;

    out = std::move(pending_packets_.front());
    pending_packets_.pop_front();
    return true;
}

void Bridge::clearPendingPackets() {
    std::lock_guard<std::mutex> lock(queue_mu_);
    pending_packets_.clear();
}

bool Bridge::handleServerPacket(MessageType type, const std::string& body) {
    if (type == MessageType::DEVICE) {
        std::string cmd;
        if (!parseMotorCmdJson(body, cmd)) return sendServerError("invalid DEVICE body json");
        if (!validMotorCmd(cmd)) return sendServerError("invalid motor cmd");
        return handleMotorCmd(cmd);
    }
    if (type == MessageType::AVAILABLE) {
        return handleStatusReq();
    }
    return sendServerError("unsupported message type");
}

bool Bridge::handleMotorCmd(const std::string& cmd) {
    const std::string payload = json{{"motor", cmd}}.dump();
    StmFrame frame;
    std::string ferr;
    bool success = false;

    // [제안 반영] UART 접근 시 뮤텍스 사용
    {
        std::lock_guard<std::mutex> lock(uart_mu_);
        if (Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_MOTOR, payload)) {
            success = Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr);
        }
    }

    if (!success) return sendServerError("uart motor cmd failed: " + ferr);
    if (frame.cmd != Stm32Proto::CMD_MOTOR) return sendServerError("unexpected cmd in motor ack");

    auto ack = Stm32Proto::parseMotorAckJson(frame.payload_json);
    if (!ack) return sendServerError("invalid motor ack json");

    const std::string out = json{{"ok", ack->ok}, {"mode", ack->mode}, {"cmd", ack->cmd}}.dump();
    std::string err;

    // [제안 반영] 서버 전송 시 뮤텍스 사용
    {
        std::lock_guard<std::mutex> lock(server_mu_);
        if (!server_.sendPacket(MessageType::ACK, out, err)) {
            logger_.log("ERROR", "server send ACK fail: " + err);
            server_.close();
            return false;
        }
    }

    logger_.log("INFO", "motor relay ok: " + cmd);
    return true;
}

bool Bridge::handleStatusReq() {
    StmFrame frame;
    std::string ferr;
    bool success = false;

    // [제안 반영] UART 접근 시 뮤텍스 사용
    {
        std::lock_guard<std::mutex> lock(uart_mu_);
        if (Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_STATUS, "")) {
            success = Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr);
        }
    }

    if (!success) return sendServerError("uart read status failed: " + ferr);
    if (frame.cmd != Stm32Proto::CMD_STATUS) return sendServerError("unexpected cmd in status frame");

    auto st = Stm32Proto::parseStatusJson(frame.payload_json);
    if (!st) return sendServerError("invalid status json");

    const std::string out = json{{"tmp", st->tmp}, {"hum", st->hum}, {"dir", st->dir}, {"tilt", st->tilt}}.dump();
    std::string err;

    // [제안 반영] 서버 전송 시 뮤텍스 사용
    {
        std::lock_guard<std::mutex> lock(server_mu_);
        if (!server_.sendPacket(MessageType::AVAILABLE, out, err)) {
            logger_.log("ERROR", "server send AVAILABLE fail: " + err);
            server_.close();
            return false;
        }
    }

    logger_.log("INFO", "status relay ok (on-demand)");
    return true;
}

bool Bridge::sendServerError(const std::string& reason) {
    const std::string out = json{{"reason", reason}}.dump();
    std::string err;
    {
        std::lock_guard<std::mutex> lock(server_mu_);
        if (!server_.sendPacket(MessageType::FAIL, out, err)) {
            logger_.log("ERROR", "server send FAIL fail: " + err);
            server_.close();
            return false;
        }
    }
    logger_.log("ERROR", reason);
    return false;
}

bool Bridge::parseMotorCmdJson(const std::string& body_json, std::string& cmd) {
    const auto j = json::parse(body_json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
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
    static const std::set<std::string> allowed = {"w", "a", "s", "d", "auto", "manual", "on", "off"};
    return allowed.find(cmd) != allowed.end();
}

} // namespace edge_device