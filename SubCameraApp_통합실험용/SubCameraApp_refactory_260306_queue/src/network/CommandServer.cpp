#include "CommandServer.h"
#include "../../config/AppConfig.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

CommandServer::CommandServer() {}

CommandServer::~CommandServer() {
    stop();
}

void CommandServer::start(CommandCallback callback) {
    on_command_received_ = callback;
    is_running_ = true;
    command_thread_ = std::thread(&CommandServer::commandLoop, this);
}

void CommandServer::stop() {
    is_running_ = false;

    // 서버 소켓 (Listening) 강제 종료
    if (server_socket_fd_ != -1) {
        shutdown(server_socket_fd_, SHUT_RDWR);
        close(server_socket_fd_);
        server_socket_fd_ = -1;
    }

    // 클라이언트 소켓 (Connected) 강제 종료
    int cfd = client_socket_fd_.load();
    if (cfd != -1) {
        shutdown(cfd, SHUT_RDWR);
        close(cfd);
        client_socket_fd_ = -1;
    }

    if (command_thread_.joinable()) command_thread_.join();

    std::cout << "[CommandServer] 정지 완료." << std::endl;
}

void CommandServer::setConnectionCallback(ConnectionCallback cb) {
    on_connection_changed_ = cb;
}

int CommandServer::getClientSocketFd() const {
    return client_socket_fd_.load();
}

void CommandServer::commandLoop() {
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    // 소켓 생성
    if ((server_socket_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        return;
    }

    // 포트 재사용 (재실행 시 에러 방지)
    setsockopt(server_socket_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
               &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(AppConfig::TCP_LISTEN_PORT);

    if (bind(server_socket_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return;
    }

    if (listen(server_socket_fd_, 3) < 0) {
        perror("Listen failed");
        return;
    }

    std::cout << "[CommandServer] TCP 대기 중, 포트: "
              << AppConfig::TCP_LISTEN_PORT << std::endl;

    while (is_running_) {
        // 클라이언트 접속 대기 (Blocking)
        int new_socket = accept(server_socket_fd_,
                                (struct sockaddr*)&address,
                                (socklen_t*)&addrlen);
        if (new_socket < 0) {
            if (!is_running_) break;
            continue;
        }

        // 접속 성공 → 연결 콜백 알림
        client_socket_fd_ = new_socket;
        std::cout << "[CommandServer] 서버 연결됨!" << std::endl;

        if (on_connection_changed_) {
            on_connection_changed_(true, new_socket);
        }

        // 명령 수신 루프 (연결 유지)
        char buffer[1024] = {0};
        while (is_running_) {
            int valread = read(new_socket, buffer, 1024);
            std::cout << valread << '\n';

            // 연결 끊김 (0) 또는 에러 (-1)
            if (valread <= 0) {
                std::cout << "[CommandServer] 서버 연결 끊김." << std::endl;

                if (on_connection_changed_) {
                    on_connection_changed_(false, -1);
                }

                close(new_socket);
                client_socket_fd_ = -1;
                break;
            }

            // 명령 처리
            std::string cmd(buffer);
            std::cout << "[CommandServer] 수신: " << cmd << std::endl;

            // 예: "START_STREAM:15001"
            if (cmd.find("START_STREAM:") != std::string::npos) {
                int port = std::stoi(cmd.substr(13));
                std::string server_ip = inet_ntoa(address.sin_addr);

                // 컨트롤러에게 알림 (Callback)
                if (on_command_received_) {
                    on_command_received_(server_ip, port);
                }
            }

            memset(buffer, 0, sizeof(buffer));
        }
    }
}
