#ifndef SYSTEM_RESOURCE_MONITOR_H
#define SYSTEM_RESOURCE_MONITOR_H

#include "IResourceMonitor.h"
#include <chrono>

// ================================================================
// SystemResourceMonitor: Linux 환경(Raspberry Pi) 시스템 자원 측정기
//
// 리소스 절약 전략:
// 1. 파일 I/O 최소화 (std::ifstream 캐싱 / 문자열 파싱 최소화)
// 2. CPU 사용률 계산 시 이전 상태 기록을 유지하여 델타값만 연산
// ================================================================

class SystemResourceMonitor : public IResourceMonitor {
public:
  SystemResourceMonitor();
  ~SystemResourceMonitor() override = default;

  DeviceStatus getStatus() override;

private:
  double getCpuUsage();
  double getMemUsage();
  double getTemperature();
  long getUptime();

  // CPU 사용량 계산을 위한 이전 상태 저장
  unsigned long long prev_idle_ = 0;
  unsigned long long prev_total_ = 0;
};

#endif // SYSTEM_RESOURCE_MONITOR_H
