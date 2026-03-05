#include "bridge.h"

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

namespace {

std::string readAll(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

bool parseString(const std::string& src, const std::string& key, std::string& out) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (!std::regex_search(src, m, re) || m.size() < 2) {
        return false;
    }
    out = m[1].str();
    return true;
}

bool parseInt(const std::string& src, const std::string& key, int& out) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch m;
    if (!std::regex_search(src, m, re) || m.size() < 2) {
        return false;
    }
    try {
        out = std::stoi(m[1].str());
        return true;
    } catch (...) {
        return false;
    }
}

bool loadConfig(const std::string& path, BridgeConfig& cfg) {
    const std::string content = readAll(path);
    if (content.empty()) {
        return false;
    }

    std::string s;
    int n;

    if (parseString(content, "serial_port", s)) cfg.serial_port = s;
    if (parseInt(content, "serial_baud", n)) cfg.serial_baud = n;
    if (parseString(content, "server_host", s)) cfg.server_host = s;
    if (parseInt(content, "server_port", n)) cfg.server_port = static_cast<uint16_t>(n);
    if (parseInt(content, "uart_timeout_ms", n)) cfg.uart_timeout_ms = n;
    if (parseInt(content, "reconnect_initial_ms", n)) cfg.reconnect_initial_ms = n;
    if (parseInt(content, "reconnect_max_ms", n)) cfg.reconnect_max_ms = n;
    if (parseString(content, "log_file", s)) cfg.log_file = s;

    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path = "./config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    BridgeConfig cfg;
    if (!loadConfig(config_path, cfg)) {
        std::cerr << "Failed to load config: " << config_path << '\n';
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
