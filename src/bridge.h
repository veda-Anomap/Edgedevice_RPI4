#pragma once

#include "server_client.h"
#include "uart_port.h"

#include <fstream>
#include <mutex>
#include <string>

struct BridgeConfig {
    std::string serial_port = "/dev/serial0";
    int serial_baud = 115200;

    std::string server_host = "127.0.0.1";
    uint16_t server_port = 9000;

    int uart_timeout_ms = 1000;
    int reconnect_initial_ms = 500;
    int reconnect_max_ms = 10000;

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
    explicit Bridge(BridgeConfig cfg);
    bool init();
    void run();

private:
    bool handleServerLine(const std::string& line);
    bool handleMotorCmd(const std::string& cmd);
    bool handleStatusReq();

    bool sendServerError(const std::string& reason);

    static std::string escapeJson(const std::string& s);
    static std::string jsonStrField(const std::string& key, const std::string& val);
    static std::string jsonNumField(const std::string& key, double val);
    static std::string jsonIntField(const std::string& key, int val);

    static bool parseServerCommand(const std::string& line, std::string& type, std::string& cmd);
    static bool validMotorCmd(const std::string& cmd);

    BridgeConfig cfg_;
    ServerClient server_;
    UartPort uart_;
    Logger logger_;
};
