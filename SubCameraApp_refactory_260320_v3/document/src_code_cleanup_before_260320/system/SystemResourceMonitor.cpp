#include "SystemResourceMonitor.h"
#include <fstream>
#include <iostream>
#include <sstream>


SystemResourceMonitor::SystemResourceMonitor() {
  // 최초 상태 저장 (초기 차등 계산 용)
  getCpuUsage();
}

DeviceStatus SystemResourceMonitor::getStatus() {
  DeviceStatus stat;
  stat.cpu_usage_percent = getCpuUsage();
  stat.mem_usage_percent = getMemUsage();
  stat.temperature_c = getTemperature();
  stat.uptime_seconds = getUptime();
  return stat;
}

// 1. CPU (Linux /proc/stat 파싱)
// 파일 I/O를 1줄만 읽어 가볍게 파싱합니다.
double SystemResourceMonitor::getCpuUsage() {
  std::ifstream file("/proc/stat");
  if (!file.is_open())
    return 0.0;

  std::string line;
  std::getline(file, line); // 첫 줄(cpu 전체)만 읽음
  file.close();

  std::istringstream iss(line);
  std::string cpu_label;
  unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
  iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >>
      softirq >> steal;

  unsigned long long current_idle = idle + iowait;
  unsigned long long current_non_idle =
      user + nice + system + irq + softirq + steal;
  unsigned long long current_total = current_idle + current_non_idle;

  // 이전 상태와의 차등분으로 점유율 계산
  unsigned long long totald = current_total - prev_total_;
  unsigned long long idled = current_idle - prev_idle_;

  double cpu_percentage = 0.0;
  if (totald > 0) {
    cpu_percentage = (double)(totald - idled) / totald * 100.0;
  }

  prev_total_ = current_total;
  prev_idle_ = current_idle;

  return cpu_percentage;
}

// 2. RAM (Linux /proc/meminfo 파싱)
double SystemResourceMonitor::getMemUsage() {
  std::ifstream file("/proc/meminfo");
  if (!file.is_open())
    return 0.0;

  std::string line;
  long total_mem = 0, free_mem = 0, buffers = 0, cached = 0;
  int items_found = 0;

  while (std::getline(file, line) && items_found < 4) {
    if (line.compare(0, 9, "MemTotal:") == 0) {
      std::sscanf(line.c_str(), "%*s %ld", &total_mem);
      items_found++;
    } else if (line.compare(0, 8, "MemFree:") == 0) {
      std::sscanf(line.c_str(), "%*s %ld", &free_mem);
      items_found++;
    } else if (line.compare(0, 8, "Buffers:") == 0) {
      std::sscanf(line.c_str(), "%*s %ld", &buffers);
      items_found++;
    } else if (line.compare(0, 7, "Cached:") == 0) {
      std::sscanf(line.c_str(), "%*s %ld", &cached);
      items_found++;
    }
  }
  file.close();

  if (total_mem == 0)
    return 0.0;

  // 실제 사용량 = 전체 - (여유 + 버퍼 + 캐시)
  long used_mem = total_mem - free_mem - buffers - cached;
  return ((double)used_mem / total_mem) * 100.0;
}

// 3. 온도 (Raspberry Pi: /sys/class/thermal/thermal_zone0/temp)
double SystemResourceMonitor::getTemperature() {
  std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
  if (!file.is_open())
    return 0.0;

  long temp_millicelsius;
  file >> temp_millicelsius;
  file.close();

  return temp_millicelsius / 1000.0; // 섭씨 변환
}

// 4. 업타임 (Linux /proc/uptime 파싱)
long SystemResourceMonitor::getUptime() {
  std::ifstream file("/proc/uptime");
  if (!file.is_open())
    return 0;

  double uptime;
  file >> uptime;
  file.close();

  return static_cast<long>(uptime);
}
