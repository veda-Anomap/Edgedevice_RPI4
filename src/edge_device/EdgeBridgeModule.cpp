#include "EdgeBridgeModule.h"
#include "bridge.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>

namespace {

using json = nlohmann::json;

std::string readAll(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
}

bool loadConfig(const std::string& path, edge_device::BridgeConfig& cfg, std::string& err) {
    const std::string content = readAll(path);
    std::cout << "[Debug] Attempting to open config at: " << path << std::endl;
    if (content.empty()) {
        err = "config file open/read failed";
        return false;
    }

    const json j = json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "config json parse failed";
        return false;
    }

    auto setString = [&](const char* key, std::string& out) -> bool {
        if (!j.contains(key)) return true;
        if (!j.at(key).is_string()) {
            err = std::string("invalid type for key: ") + key;
            return false;
        }
        out = j.at(key).get<std::string>();
        return true;
    };

    auto setInt = [&](const char* key, int& out) -> bool {
        if (!j.contains(key)) return true;
        if (!j.at(key).is_number_integer()) {
            err = std::string("invalid type for key: ") + key;
            return false;
        }
        out = j.at(key).get<int>();
        return true;
    };

    if (!setString("serial_port", cfg.serial_port)) return false;
    if (!setInt("serial_baud", cfg.serial_baud)) return false;
    if (!setString("server_host", cfg.server_host)) return false;

    int server_port = static_cast<int>(cfg.server_port);
    if (!setInt("server_port", server_port)) return false;
    if (server_port < 1 || server_port > 65535) {
        err = "server_port out of range";
        return false;
    }
    cfg.server_port = static_cast<uint16_t>(server_port);

    if (!setInt("uart_timeout_ms", cfg.uart_timeout_ms)) return false;
    if (!setInt("reconnect_initial_ms", cfg.reconnect_initial_ms)) return false;
    if (!setInt("reconnect_max_ms", cfg.reconnect_max_ms)) return false;
    if (!setString("log_file", cfg.log_file)) return false;

    if (cfg.serial_baud <= 0) { err = "serial_baud must be > 0"; return false; }
    if (cfg.uart_timeout_ms <= 0) { err = "uart_timeout_ms must be > 0"; return false; }
    if (cfg.reconnect_initial_ms <= 0) { err = "reconnect_initial_ms must be > 0"; return false; }
    if (cfg.reconnect_max_ms <= 0) { err = "reconnect_max_ms must be > 0"; return false; }
    if (cfg.reconnect_initial_ms > cfg.reconnect_max_ms) {
        err = "reconnect_initial_ms must be <= reconnect_max_ms";
        return false;
    }
    //config 호스트 및 포트 확인 
    std::cout << "[Check] Loaded Host: " << cfg.server_host << ", Port: " << cfg.server_port << std::endl;
    return true;
}

} // namespace

EdgeBridgeModule::EdgeBridgeModule(const std::string& config_path)
    : config_path_(config_path) {}

EdgeBridgeModule::~EdgeBridgeModule() {
    stop();
}

bool EdgeBridgeModule::start() {
    if (running_.load()) {
        std::cerr << "[EdgeBridge] already running" << std::endl;
        return false;
    }

    edge_device::BridgeConfig cfg;
    std::string err;
    if (!loadConfig(config_path_, cfg, err)) {
        std::cerr << "[EdgeBridge] config load failed: " << config_path_
                  << " (" << err << ")" << std::endl;
        return false;
    }

    bridge_ = std::make_unique<edge_device::Bridge>(cfg);
    if (!bridge_->init()) {
        std::cerr << "[EdgeBridge] bridge init failed" << std::endl;
        bridge_.reset();
        return false;
    }

    stop_flag_.store(false);
    running_.store(true);
    thread_ = std::thread(&EdgeBridgeModule::threadFunc, this);

    std::cout << "[EdgeBridge] 브릿지 스레드 시작됨." << std::endl;
    return true;
}

void EdgeBridgeModule::stop() {
    // stop_flag_로 가드: running_은 threadFunc에서 먼저 false로 될 수 있으므로
    // running_ 대신 stop_flag_와 thread_.joinable()로 판단
    if (!thread_.joinable()) return;

    std::cout << "[EdgeBridge] 브릿지 정지 중..." << std::endl;
    stop_flag_.store(true);

    thread_.join();

    bridge_.reset();
    running_.store(false);
    std::cout << "[EdgeBridge] 브릿지 정지 완료." << std::endl;
}

bool EdgeBridgeModule::isRunning() const {
    return running_.load();
}

void EdgeBridgeModule::threadFunc() {
    bridge_->run(&stop_flag_);
    running_.store(false);
}
