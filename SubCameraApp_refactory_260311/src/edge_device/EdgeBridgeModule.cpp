#include "EdgeBridgeModule.h"
#include "bridge.h"
#include "../util/Logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>

static const std::string TAG = "EdgeBridge";

namespace {

using json = nlohmann::json;

std::string readAll(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

bool loadConfig(const std::string& path, edge_device::BridgeConfig& cfg, std::string& err) {
    const std::string content = readAll(path);
    if (content.empty()) {
        err = "config file open/read failed";
        return false;
    }

    const json j = json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "config json parse failed";
        return false;
    }

    auto setString = [&](const char* key, std::string& out) {
        if (j.contains(key) && j.at(key).is_string()) out = j.at(key).get<std::string>();
    };
    auto setInt = [&](const char* key, int& out) {
        if (j.contains(key) && j.at(key).is_number_integer()) out = j.at(key).get<int>();
    };

    setString("serial_port", cfg.serial_port);
    setInt("serial_baud", cfg.serial_baud);
    setInt("uart_timeout_ms", cfg.uart_timeout_ms);
    setString("log_file", cfg.log_file);

    return true;
}

} // namespace

EdgeBridgeModule::EdgeBridgeModule(const std::string& config_path)
    : config_path_(config_path) {}

EdgeBridgeModule::~EdgeBridgeModule() {
    stop();
}

bool EdgeBridgeModule::start() {
    if (running_.load()) return false;

    edge_device::BridgeConfig cfg;
    std::string err;
    if (!loadConfig(config_path_, cfg, err)) {
        LOG_ERROR(TAG, "config load failed: " + config_path_);
        return false;
    }

    bridge_ = std::make_unique<edge_device::Bridge>(cfg);
    
    // [Refactor] Bridge::init() 이전에 NetworkSender가 설정되어야 할 수도 있으나 
    // 현재 구조상 UART만 초기화함
    // [수정] 이제 sender_ 멤버 변수를 사용할 수 있습니다.
    if (sender_) {
        bridge_->setSender(sender_);
    }
    // // UART 초기화
    // if (!bridge_->run_init_only()) { // 브릿지 내부에서 UART만 여는 헬퍼가 필요할 수 있음
    //     // 사실 Bridge::run() 내부에서 UART를 열도록 되어있으므로 여기선 스킵 가능
    // }

    stop_flag_.store(false);
    running_.store(true);
    thread_ = std::thread(&EdgeBridgeModule::threadFunc, this);

    return true;
}

void EdgeBridgeModule::stop() {
    if (!thread_.joinable()) return;
    stop_flag_.store(true);
    thread_.join();
    bridge_.reset();
    running_.store(false);
}

bool EdgeBridgeModule::isRunning() const {
    return running_.load();
}

void EdgeBridgeModule::setNetworkSender(INetworkSender* sender) {
    this->sender_ = sender; // 멤버 변수에 저장
    if (bridge_) {
        bridge_->setSender(sender);
    }
}

bool EdgeBridgeModule::handleMotorCmd(const std::string& cmd) {
    if (bridge_) {
        return bridge_->handleMotorCmd(cmd);
    }
    return false;
}

void EdgeBridgeModule::threadFunc() {
    bridge_->run(&stop_flag_);
    running_.store(false);
}
