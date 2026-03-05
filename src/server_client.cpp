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
            sockfd_ = fd;
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
    if (sockfd_ >= 0) {
        ::shutdown(sockfd_, SHUT_RDWR);
        ::close(sockfd_);
        sockfd_ = -1;
    }
    read_buf_.clear();
}

bool ServerClient::isConnected() const {
    return sockfd_ >= 0;
}

bool ServerClient::readLine(std::string& line, std::string& err) {
    line.clear();
    if (sockfd_ < 0) {
        err = "not connected";
        return false;
    }

    while (true) {
        const std::size_t pos = read_buf_.find('\n');
        if (pos != std::string::npos) {
            line = read_buf_.substr(0, pos);
            read_buf_.erase(0, pos + 1);
            return true;
        }

        char buf[1024];
        const ssize_t n = ::recv(sockfd_, buf, sizeof(buf), 0);
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

        read_buf_.append(buf, static_cast<std::size_t>(n));
    }
}

bool ServerClient::sendJsonLine(const std::string& json, std::string& err) {
    if (sockfd_ < 0) {
        err = "not connected";
        return false;
    }

    std::string out = json;
    if (out.empty() || out.back() != '\n') {
        out.push_back('\n');
    }

    std::size_t sent = 0;
    while (sent < out.size()) {
        const ssize_t n = ::send(sockfd_, out.data() + sent, out.size() - sent, 0);
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
        sent += static_cast<std::size_t>(n);
    }

    return true;
}
