#pragma once

#include <cstdint>
#include <string>

// Server protocol message type (1 byte)
enum class MessageType : uint8_t {
    ACK       = 0x00,
    LOGIN     = 0x01,
    SUCCESS   = 0x02,
    FAIL      = 0x03,
    DEVICE    = 0x04,
    AVAILABLE = 0x05,
    AI        = 0x06,
    CAMERA    = 0x07,
    ASSIGN    = 0x08,
    META      = 0x09,
    IMAGE     = 0x0a,
};

#pragma pack(push, 1)
struct PacketHeader {
    MessageType type;
    uint32_t body_length;  // network byte order
};
#pragma pack(pop)

class ServerClient {
public:
    ServerClient();
    ~ServerClient();

    ServerClient(const ServerClient&) = delete;
    ServerClient& operator=(const ServerClient&) = delete;

    bool connectTo(const std::string& host, uint16_t port, std::string& err);
    void close();
    bool isConnected() const;

    bool readPacket(MessageType& type, std::string& body, std::string& err);
    bool sendPacket(MessageType type, const std::string& body, std::string& err);

private:
    bool recvAll(uint8_t* dst, size_t bytes, std::string& err);
    bool sendAll(const uint8_t* src, size_t bytes, std::string& err);

    int sockfd_;
    static constexpr uint32_t MAX_BODY_LEN = 1024 * 1024;
};
