#include "NetworkFacade.h"
#include "../protocol/PacketProtocol.h"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

NetworkFacade::NetworkFacade() {
  beacon_ = std::make_unique<BeaconService>();
  command_server_ = std::make_unique<CommandServer>();

  // CommandServer의 연결 상태 변경을 BeaconService에 전달
  command_server_->setConnectionCallback(
      [this](bool connected, int /*client_fd*/) {
        beacon_->setConnected(connected);
        if (connected) {
          std::cout << "[NetworkFacade] 연결됨 → 비콘 중지" << std::endl;
        } else {
          std::cout << "[NetworkFacade] 연결 끊김 → 비콘 재개" << std::endl;
        }
      });
}

NetworkFacade::~NetworkFacade() { stop(); }

void NetworkFacade::start(CommandCallback callback) {
  beacon_->start();
  command_server_->start(callback);
  std::cout << "[NetworkFacade] 네트워크 시작됨." << std::endl;
}

void NetworkFacade::stop() {
  if (command_server_)
    command_server_->stop();
  if (beacon_)
    beacon_->stop();
  std::cout << "[NetworkFacade] 네트워크 정지됨." << std::endl;
}

void NetworkFacade::sendMessage(const std::string &msg) {
  int client_fd = command_server_->getClientSocketFd();
  if (client_fd == -1)
    return;

  // 본문 크기
  uint32_t body_len = static_cast<uint32_t>(msg.size());

  // 헤더 세팅
  PacketHeader header;
  header.type = MessageType::AI;
  header.body_length = htonl(body_len);

  // 1. 헤더 (5 Byte) 전송
  send(client_fd, &header, sizeof(header), 0);

  // 2. 본문 전송
  if (body_len > 0) {
    send(client_fd, msg.c_str(), body_len, 0);
  }
}

void NetworkFacade::sendImage(const std::string &metadata,
                              const std::vector<uint8_t> &jpeg_data) {
  int client_fd = command_server_->getClientSocketFd();
  if (client_fd == -1)
    return;

  uint32_t json_len = static_cast<uint32_t>(metadata.size());

  // 헤더 세팅: body_length = JSON 길이만 (서버 규격)
  PacketHeader header;
  header.type = MessageType::IMAGE;
  header.body_length = htonl(json_len);

  // 1. 헤더 (5 Byte) 전송
  send(client_fd, &header, sizeof(header), 0);

  // 2. JSON 메타데이터 전송 (json_len 바이트, '{' 로 시작)
  if (json_len > 0) {
    send(client_fd, metadata.c_str(), json_len, 0);
  }

  // 3. JPEG 바이너리 전송 (서버는 JSON의 jpeg_size 필드로 길이를 알고 있음)
  if (!jpeg_data.empty()) {
    send(client_fd, jpeg_data.data(), jpeg_data.size(), 0);
  }
}

void NetworkFacade::sendDeviceStatus(const DeviceStatus &status) {
  int client_fd = command_server_->getClientSocketFd();
  if (client_fd == -1)
    return;

  std::string json_str = status.toJson();
  uint32_t body_len = static_cast<uint32_t>(json_str.size());

  PacketHeader header;
  header.type = MessageType::AVAILABLE; // 0x05
  header.body_length = htonl(body_len);

  send(client_fd, &header, sizeof(header), 0);
  if (body_len > 0) {
    send(client_fd, json_str.c_str(), body_len, 0);
  }
}
