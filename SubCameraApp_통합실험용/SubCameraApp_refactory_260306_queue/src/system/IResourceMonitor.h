#ifndef I_RESOURCE_MONITOR_H
#define I_RESOURCE_MONITOR_H

#include <string>

// ================================================================
// IResourceMonitor: 기기 자원 모니터링 인터페이스 (DIP)
// ================================================================

struct DeviceStatus {
  double cpu_usage_percent; // 0.0 ~ 100.0
  double mem_usage_percent; // 0.0 ~ 100.0
  double temperature_c;     // 섭씨 온도
  long uptime_seconds;      // 가동 시간 (초)
  int pending_event_count;  // 대기 중인 녹화 이벤트 수 (메모리 지표)

  // JSON 형태의 문자열로 변환하는 헬퍼 메서드
  std::string toJson() const {
    return "{\"cpu\":" + std::to_string(cpu_usage_percent) +
           ",\"memory\":" + std::to_string(mem_usage_percent) +
           ",\"temp\":" + std::to_string(temperature_c) +
           ",\"uptime\":" + std::to_string(uptime_seconds) +
           ",\"pending_events\":" + std::to_string(pending_event_count) + "}";
  }
};

class IResourceMonitor {
public:
  virtual ~IResourceMonitor() = default;

  // 현재 기기의 자원 상태를 측정하여 반환
  virtual DeviceStatus getStatus() = 0;
};

#endif // I_RESOURCE_MONITOR_H
