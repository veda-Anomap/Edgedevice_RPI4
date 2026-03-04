#ifndef I_COMMAND_RECEIVER_H
#define I_COMMAND_RECEIVER_H

#include <string>
#include <functional>

// =============================================
// 인터페이스: 서버 명령 수신 (ISP 원칙)
// 컨트롤러는 start/stop + 콜백만 알면 됨
// =============================================

// 서버가 명령을 보냈을 때 호출할 함수 타입 (IP, Port)
using CommandCallback = std::function<void(std::string, int)>;

class ICommandReceiver {
public:
    virtual ~ICommandReceiver() = default;

    // 명령 수신 시작 (콜백 등록)
    virtual void start(CommandCallback callback) = 0;

    // 명령 수신 종료
    virtual void stop() = 0;
};

#endif // I_COMMAND_RECEIVER_H
