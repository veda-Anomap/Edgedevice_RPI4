#ifndef BEACON_SERVICE_H
#define BEACON_SERVICE_H

#include <thread>
#include <atomic>
#include <string>

// =============================================
// BeaconService: UDP 브로드캐스트 전용 (SRP)
// "나 여기 있어요!" 신호만 책임
// =============================================

class BeaconService {
public:
    BeaconService();
    ~BeaconService();

    // 비콘 스레드 시작/정지
    void start();
    void stop();

    // 연결 상태 설정 (연결 시 비콘 중지)
    void setConnected(bool connected);

private:
    void beaconLoop();

    std::thread beacon_thread_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_connected_{false};
};

#endif // BEACON_SERVICE_H
