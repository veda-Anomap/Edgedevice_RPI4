#pragma once

#include <cstdint>
#include <string>

class ServerClient {
public:
    ServerClient();
    ~ServerClient();

    ServerClient(const ServerClient&) = delete;
    ServerClient& operator=(const ServerClient&) = delete;

    bool connectTo(const std::string& host, uint16_t port, std::string& err);
    void close();
    bool isConnected() const;

    bool readLine(std::string& line, std::string& err);
    bool sendJsonLine(const std::string& json, std::string& err);

private:
    int sockfd_;
    std::string read_buf_;
};
