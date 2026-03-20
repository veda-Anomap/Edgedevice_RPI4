#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include "../../config/AppConfig.h"
#include <ctime>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void log(LogLevel level, const std::string& tag, const std::string& message) {
        if (level == LogLevel::DEBUG && !AppConfig::ENABLE_DEBUG_LOG) {
            return;
        }

        // [신규] 카테고리별 필터링
        if (tag == "PERF_PRE" && !AppConfig::ENABLE_LOG_PERF_PRE) return;
        if (tag == "PERF_AI" && !AppConfig::ENABLE_LOG_PERF_AI) return;
        if (tag == "FPS" && !AppConfig::ENABLE_LOG_FPS) return;

        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::cout << "[" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") 
                  << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
                  << levelToString(level) << " [" << tag << "] " << message << std::endl;
    }

private:
    static inline std::mutex mutex_;

    static std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default:              return "UNKNOWN";
        }
    }
};

#define LOG_DEBUG(tag, msg) Logger::log(LogLevel::DEBUG, tag, msg)
#define LOG_INFO(tag, msg)  Logger::log(LogLevel::INFO, tag, msg)
#define LOG_WARN(tag, msg)  Logger::log(LogLevel::WARN, tag, msg)
#define LOG_ERROR(tag, msg) Logger::log(LogLevel::ERROR, tag, msg)

#endif // LOGGER_H
