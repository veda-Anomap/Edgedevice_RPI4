#include "server_client.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

ServerClient::ServerClient() : sockfd_(-1) {}

ServerClient::~ServerClient() {
    close();
}

bool ServerClient::connectTo(const std::string& host, uint16_t port, std::string& err) {
    close();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    const int rv = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (rv != 0) {
        err = gai_strerror(rv);
        return false;
    }

    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            sockfd_.store(fd);
            freeaddrinfo(res);
            return true;
        }

        ::close(fd);
    }

    freeaddrinfo(res);
    err = std::strerror(errno);
    return false;
}

void ServerClient::close() {
    const int fd = sockfd_.exchange(-1);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

bool ServerClient::isConnected() const {
    return sockfd_.load() >= 0;
}

bool ServerClient::recvAll(uint8_t* dst, size_t bytes, std::string& err) {
    const int fd = sockfd_.load();
    if (fd < 0) {
        err = "not connected";
        return false;
    }

    size_t got = 0;
    while (got < bytes) {
        const ssize_t n = ::recv(fd, dst + got, bytes - got, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = std::strerror(errno);
            return false;
        }
        if (n == 0) {
            err = "server disconnected";
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

bool ServerClient::sendAll(const uint8_t* src, size_t bytes, std::string& err) {
    const int fd = sockfd_.load();
    if (fd < 0) {
        err = "not connected";
        return false;
    }

    size_t sent = 0;
    while (sent < bytes) {
        const ssize_t n = ::send(fd, src + sent, bytes - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = std::strerror(errno);
            return false;
        }
        if (n == 0) {
            err = "send returned 0";
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool ServerClient::readPacket(MessageType& type, std::string& body, std::string& err) {
    body.clear();

    uint8_t header[5];
    if (!recvAll(header, sizeof(header), err)) {
        return false;
    }

    type = static_cast<MessageType>(header[0]);

    uint32_t len_be = 0;
    std::memcpy(&len_be, header + 1, sizeof(len_be));
    const uint32_t len = ntohl(len_be);

    if (len > MAX_BODY_LEN) {
        err = "packet body too large";
        return false;
    }

    if (len == 0) {
        return true;
    }

    body.resize(len);
    return recvAll(reinterpret_cast<uint8_t*>(&body[0]), len, err);
}

bool ServerClient::sendPacket(MessageType type, const std::string& body, std::string& err) {
    if (body.size() > MAX_BODY_LEN) {
        err = "packet body too large";
        return false;
    }

    uint8_t header[5];
    header[0] = static_cast<uint8_t>(type);

    const uint32_t len_be = htonl(static_cast<uint32_t>(body.size()));
    std::memcpy(header + 1, &len_be, sizeof(len_be));

    if (!sendAll(header, sizeof(header), err)) {
        return false;
    }

    if (!body.empty()) {
        return sendAll(reinterpret_cast<const uint8_t*>(body.data()), body.size(), err);
    }

    return true;
}
