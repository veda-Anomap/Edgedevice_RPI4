#ifndef COMMAND_SERVER_H
#define COMMAND_SERVER_H

#include "ICommandReceiver.h"

#include <thread>
#include <atomic>
#include <functional>
#include <string>

// =============================================
// 연결 상태 변경 콜백 타입
// =============================================
using ConnectionCallback = std::function<void(bool connected, int client_fd)>;

// =============================================
// CommandServer: TCP 명령 수신 전용 (SRP)
// 서버 접속 대기 + 명령 파싱만 책임
// =============================================

class CommandServer : public ICommandReceiver {
public:
    CommandServer();
    ~CommandServer() override;

    // ICommandReceiver 인터페이스 구현
    void start(CommandCallback callback) override;
    void stop() override;

    // 연결 상태 변경 시 알림 콜백 등록
    void setConnectionCallback(ConnectionCallback cb);

    // 현재 클라이언트 소켓 FD 조회 (메시지 송신용)
    int getClientSocketFd() const;

private:
    void commandLoop();

    std::thread command_thread_;
    std::atomic<bool> is_running_{false};

    CommandCallback on_command_received_;
    ConnectionCallback on_connection_changed_;

    int server_socket_fd_ = -1;
    std::atomic<int> client_socket_fd_{-1};
};

#endif // COMMAND_SERVER_H
