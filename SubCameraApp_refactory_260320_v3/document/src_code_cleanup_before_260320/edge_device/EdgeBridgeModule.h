#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class INetworkSender;

namespace edge_device {

class Bridge;
struct BridgeConfig;

} // namespace edge_device

// =============================================sud
// EdgeBridgeModule: Edgedevice_RPI4 Bridge를
// 별도 스레드에서 실행하는 래퍼 (SRP)
// SubCamController에서 생성 → start() → stop()
// =============================================
class EdgeBridgeModule {
public:
    // config_path: edge_device_config.json 경로
    explicit EdgeBridgeModule(const std::string& config_path = "../config/edge_device_config.json");
    ~EdgeBridgeModule();

    EdgeBridgeModule(const EdgeBridgeModule&) = delete;
    EdgeBridgeModule& operator=(const EdgeBridgeModule&) = delete;

    // 브릿지 초기화 및 백그라운드 스레드 시작
    bool start();

    // 브릿지 정지 (스레드 합류)
    void stop();

    // 현재 실행 중인지 여부
    bool isRunning() const;

    // [DIP] 네트워크 송신기 주입
    void setNetworkSender(INetworkSender* sender);

    // [DIP] 모터 제어 명령 전달
    bool handleMotorCmd(const std::string& cmd);

private:
    void threadFunc();

    std::string config_path_;
    std::unique_ptr<edge_device::Bridge> bridge_;
    std::thread thread_;
    std::atomic<bool> stop_flag_{false};
    std::atomic<bool> running_{false};
    INetworkSender* sender_ = nullptr;
};
