#include "bridge.h"
#include "json.hpp"

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
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

bool loadConfig(const std::string& path, BridgeConfig& cfg, std::string& err) {
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

    // Policy: keep defaults from BridgeConfig, and override only when valid typed keys exist.
    auto setString = [&](const char* key, std::string& out) -> bool {
        if (!j.contains(key)) {
            return true;
        }
        if (!j.at(key).is_string()) {
            err = std::string("invalid type for key: ") + key;
            return false;
        }
        out = j.at(key).get<std::string>();
        return true;
    };

    auto setInt = [&](const char* key, int& out) -> bool {
        if (!j.contains(key)) {
            return true;
        }
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

    return true;
}

} // namespace

int main(int argc, char** argv) {
    // [CUSTOM] 기본 설정 파일 경로. 필요 시 실행 인자로 다른 경로를 전달.
    std::string config_path = "./config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    // Entry flow: load config -> initialize bridge resources -> run bridge loop.
    BridgeConfig cfg;
    std::string err;
    if (!loadConfig(config_path, cfg, err)) {
        std::cerr << "Failed to load config: " << config_path << " (" << err << ")\n";
        return 1;
    }

    Bridge bridge(cfg);
    if (!bridge.init()) {
        std::cerr << "Bridge init failed\n";
        return 1;
    }

    bridge.run();
    return 0;
}
