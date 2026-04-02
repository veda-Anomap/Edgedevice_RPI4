#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <chrono>
#include <string>
#include <iomanip>
#include <sstream>
#include <vector>
#include "Logger.h"

/**
 * @brief 성능 지표(FPS, Latency 등)를 측정하고 주기적으로 포맷팅하여 로그를 남기는 유틸리티
 * SOLID 원칙 중 SRP(단일 책임 원칙)를 준수하기 위해 StreamPipeline에서 로깅 형식을 분리함.
 */
class PerformanceMonitor {
public:
    enum class Unit { MS, FPS, US };

    PerformanceMonitor(const std::string& tag, const std::string& label, 
                       Unit unit, int log_interval_ms = 5000)
        : tag_(tag), label_(label), unit_(unit), log_interval_ms_(log_interval_ms) {
        last_log_time_ = std::chrono::steady_clock::now();
    }

    void update(double value) {
        // 이동 평균 계산 (Alpha = 0.1)
        if (avg_value_ == 0) {
            avg_value_ = value;
        } else {
            avg_value_ = avg_value_ * 0.9 + value * 0.1;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_time_).count() >= log_interval_ms_) {
            log(value);
            last_log_time_ = now;
        }
    }

private:
    void log(double recent_value) {
        std::ostringstream ss;
        ss << std::fixed;

        if (unit_ == Unit::FPS) {
            ss << std::setprecision(1);
            double interval_ms = 1000.0 / (avg_value_ + 0.0001);
            ss << label_ << ": " << avg_value_ << " FPS (" << std::setprecision(2) << interval_ms << " ms/frame)";
        } else if (unit_ == Unit::MS) {
            ss << std::setprecision(2);
            ss << label_ << ": " << avg_value_ << " ms (Recent: " << (int)recent_value << " ms)";
        } else if (unit_ == Unit::US) {
            ss << std::setprecision(2);
            ss << label_ << ": " << (avg_value_ / 1000.0) << " ms";
        }

        LOG_DEBUG(tag_, ss.str());
    }

    std::string tag_;
    std::string label_;
    Unit unit_;
    int log_interval_ms_;
    double avg_value_ = 0;
    std::chrono::steady_clock::time_point last_log_time_;
};

#endif // PERFORMANCE_MONITOR_H
