#include "CommandServer.h"
#include "../../config/AppConfig.h"
#include "../util/Logger.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static const std::string TAG = "CommandServer";

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

    LOG_INFO(TAG, "정지 완료.");
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

    LOG_INFO(TAG, "TCP 대기 중, 포트: " + std::to_string(AppConfig::TCP_LISTEN_PORT));

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
        LOG_INFO(TAG, "서버 연결됨!");

        if (on_connection_changed_) {
            on_connection_changed_(true, new_socket);
        }
        
        // --- 하이브리드 수신 루프 시작 ---
        char buffer[2048]; 
        while (is_running_) {
            // 1. 일단 데이터의 앞부분을 읽어 형식을 파악함 (MSG_PEEK: 데이터를 소켓에서 제거하지 않고 보기만 함)
            ssize_t peek_read = recv(new_socket, buffer, 1, MSG_PEEK); 
            if (peek_read <= 0) break;

            uint8_t first_byte = static_cast<uint8_t>(buffer[0]);

            // [판별 로직] MessageType 범위(0x00~0x0A)를 벗어나면 텍스트 명령으로 간주
            // 'S'(START_STREAM)의 ASCII 값은 0x53이므로 이 조건문에서 텍스트로 분류됨
            if (first_byte > 0x0A) {
                // CASE A: 기존 텍스트 방식 (CommandServer_before 로직)
                memset(buffer, 0, sizeof(buffer));
                ssize_t valread = read(new_socket, buffer, sizeof(buffer) - 1);
                std::string cmd(buffer);
                LOG_DEBUG(TAG, "수신: " + cmd);

                if (cmd.find("START_STREAM:") != std::string::npos) {
                    try {
                        size_t pos = cmd.find("START_STREAM:");
                        int port = std::stoi(cmd.substr(pos + 13));
                        std::string server_ip = inet_ntoa(address.sin_addr);
                        if (on_command_received_) {
                            on_command_received_(server_ip, port, cmd);
                        }
                    } catch (...) {
                        LOG_ERROR(TAG, "포트 파싱 실패");
                    }
                }
            } 
            else {
                // CASE B: 바이너리 패킷 방식 (CommandServer_after 로직)
                uint8_t header_buf[5];
                ssize_t h_read = 0;
            
                // 1. 헤더 (5바이트) 수신
                while (h_read < 5) {
                    ssize_t n = read(new_socket, header_buf + h_read, 5 - h_read);
                    if (n <= 0) { h_read = -1; break; }
                    h_read += n;
                }

                if (h_read < 0) {
                    LOG_ERROR(TAG, "연결 끊김 (헤더 수신 중)");
                    break;
                }

                uint8_t type = header_buf[0];
                uint32_t body_len_be;
                memcpy(&body_len_be, header_buf + 1, 4);
                uint32_t body_len = ntohl(body_len_be);

                if (body_len > 10 * 1024 * 1024) { // 10MB 초과 시 방어적 종료
                    LOG_ERROR(TAG, "패킷 크기 초과: " + std::to_string(body_len));
                    break;
                }

                // 2. 본문 (body_len 만큼) 수신
                std::string body;
                if (body_len > 0) {
                    body.resize(body_len);
                    ssize_t b_read = 0;
                    while (b_read < body_len) {
                        ssize_t n = read(new_socket, &body[0] + b_read, body_len - b_read);
                        if (n <= 0) { b_read = -1; break; }
                        b_read += n;
                    }
                    if (b_read < 0) break;
                }

              // 3. 수신 데이터 처리 (로그 출력 및 모든 데이터를 Controller로 전달)
                LOG_DEBUG(TAG, "바이너리 수신 Type: " + std::to_string((int)type) + ", Len: " + std::to_string(body_len));
                
                if (body_len > 0) {
                    LOG_DEBUG(TAG, "수신: " + body);
                }

                // [수정 핵심] 어떤 명령(모터 JSON 등)이든 일단 Controller로 넘깁니다.
                if (on_command_received_) {
                    std::string server_ip = inet_ntoa(address.sin_addr);
                    int target_port = AppConfig::TCP_LISTEN_PORT; // 기본값

                    // 만약 스트리밍 시작 명령이라면 포트 파싱 시도
                    if (body.find("START_STREAM:") != std::string::npos) {
                        try {
                            size_t pos = body.find("START_STREAM:");
                            target_port = std::stoi(body.substr(pos + 13));
                        } catch (...) {}
                    }
                    
                    // 추출된 IP와 포트, 그리고 본문(JSON 포함)을 Controller로 던집니다.
                    on_command_received_(server_ip, target_port, body);
                }
            }// else (바이너리 방식) 종료
        } // 수신 루프 (while is_running_) 종료
    

        // 연결 종료 처리
        if (on_connection_changed_) {
            on_connection_changed_(false, -1);
        }
        close(new_socket);
        client_socket_fd_ = -1;
        LOG_INFO(TAG, "클라이언트 연결 종료.");
    } // 메인 접속 대기 루프 종료
}