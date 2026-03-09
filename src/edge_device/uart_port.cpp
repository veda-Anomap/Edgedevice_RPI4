#include "uart_port.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

namespace edge_device {

namespace {

speed_t toBaud(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return B115200;
    }
}

} // namespace

UartPort::UartPort() : fd_(-1) {}

UartPort::~UartPort() {
    closePort();
}

bool UartPort::openPort(const std::string& device, int baud) {
    closePort();

    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        return false;
    }

    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd_, &tty) != 0) {
        closePort();
        return false;
    }

    cfmakeraw(&tty);
    const speed_t spd = toBaud(baud);
    cfsetospeed(&tty, spd);
    cfsetispeed(&tty, spd);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        closePort();
        return false;
    }

    tcflush(fd_, TCIOFLUSH);
    return true;
}

void UartPort::closePort() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool UartPort::isOpen() const {
    return fd_ >= 0;
}

bool UartPort::writeAll(const std::vector<uint8_t>& data) {
    if (fd_ < 0) {
        return false;
    }

    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::write(fd_, data.data() + sent, data.size() - sent);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<size_t>(n);
    }

    return true;
}

bool UartPort::readExact(std::vector<uint8_t>& out, size_t bytes, int timeout_ms) {
    out.clear();
    out.reserve(bytes);

    if (fd_ < 0) {
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(timeout_ms);

    while (out.size() < bytes) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            return false;
        }
        const auto remain_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout - elapsed).count();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);

        struct timeval tv;
        tv.tv_sec = remain_ms / 1000;
        tv.tv_usec = (remain_ms % 1000) * 1000;

        const int rv = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rv == 0) {
            return false;
        }

        uint8_t buf[256];
        const size_t need = bytes - out.size();
        const size_t chunk = need < sizeof(buf) ? need : sizeof(buf);
        const ssize_t n = ::read(fd_, buf, chunk);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }

        out.insert(out.end(), buf, buf + n);
    }

    return true;
}

} // namespace edge_device
