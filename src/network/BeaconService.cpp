#include "BeaconService.h"
#include "../../config/AppConfig.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

BeaconService::BeaconService() {}

BeaconService::~BeaconService() {
    stop();
}

void BeaconService::start() {
    is_running_ = true;
    beacon_thread_ = std::thread(&BeaconService::beaconLoop, this);
}

void BeaconService::stop() {
    is_running_ = false;
    if (beacon_thread_.joinable()) beacon_thread_.join();
}

void BeaconService::setConnected(bool connected) {
    is_connected_ = connected;
}

void BeaconService::beaconLoop() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(AppConfig::UDP_BEACON_PORT);
    addr.sin_addr.s_addr = INADDR_BROADCAST;

    const std::string& msg = AppConfig::BEACON_MESSAGE;

    while (is_running_) {
        // 연결이 안 되어 있을 때만 비콘 전송
        if (!is_connected_) {
            sendto(sock, msg.c_str(), msg.size(), 0,
                   (struct sockaddr*)&addr, sizeof(addr));
        }

        std::this_thread::sleep_for(
            std::chrono::seconds(AppConfig::BEACON_INTERVAL_SEC));
    }

    close(sock);
}
