#ifndef I_NETWORK_SENDER_H
#define I_NETWORK_SENDER_H

#include "../system/IResourceMonitor.h"
#include <cstdint>
#include <string>
#include <vector>


// =============================================
// 인터페이스: 네트워크 메시지 송신 (ISP 원칙)
// 소비자는 sendMessage()만 알면 됨
// =============================================

class INetworkSender {
public:
  virtual ~INetworkSender() = default;

  // 서버로 텍스트 메시지 송신 (AI 결과 등)
  virtual void sendMessage(const std::string &msg) = 0;

  // 서버로 이미지 및 메타데이터 송신
  virtual void sendImage(const std::string &metadata,
                         const std::vector<uint8_t> &jpeg_data) = 0;

  // 서버로 기기 자원 상태(AVAILABLE) 송신
  virtual void sendDeviceStatus(const DeviceStatus &status) = 0;

  // 서버로 센서 데이터(META) 송신
  virtual void sendSensorData(const std::string &json_str) = 0;
};

#endif // I_NETWORK_SENDER_H
