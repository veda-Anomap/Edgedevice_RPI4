#pragma once

#include <cstdint>
#include <string>
#include <vector>

class UartPort {
public:
    UartPort();
    ~UartPort();

    UartPort(const UartPort&) = delete;
    UartPort& operator=(const UartPort&) = delete;

    bool openPort(const std::string& device, int baud);
    void closePort();
    bool isOpen() const;

    bool writeAll(const std::vector<uint8_t>& data);
    bool readExact(std::vector<uint8_t>& out, size_t bytes, int timeout_ms);

private:
    int fd_;
};
