#include "bridge.h"
#include "stm32_proto.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <thread>
#include <vector>


namespace edge_device {

using json = nlohmann::json;

bool Logger::open(const std::string &path) {
  ofs_.open(path, std::ios::app);
  return ofs_.is_open();
}

void Logger::log(const std::string &level, const std::string &msg) {
  std::lock_guard<std::mutex> lock(mu_);

  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tmv;
  localtime_r(&tt, &tmv);

  if (ofs_.is_open()) {
    ofs_ << std::put_time(&tmv, "%F %T") << " [" << level << "] " << msg
         << '\n';
    ofs_.flush();
  }
}

void Bridge::setSender(INetworkSender *sender) { sender_ = sender; }

void Bridge::run(std::atomic<bool> *stop_flag) {
  if (!logger_.open(cfg_.log_file)) {
    std::cerr << "[Bridge] Failed to open log file: " << cfg_.log_file
              << std::endl;
  }

  if (!uart_.openPort(cfg_.serial_port, cfg_.serial_baud)) {
    logger_.log("ERROR", "failed to open uart: " + cfg_.serial_port);
    return;
  }
  logger_.log("INFO", "uart opened: " + cfg_.serial_port);

  // [DIP] 백그라운드 UART 처리 스레드 시작
  uart_thread_ = std::thread(&Bridge::uartLoop, this, stop_flag);

  while (stop_flag && !stop_flag->load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  // 종료 시 큐 깨우기
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    queue_cv_.notify_all();
  }

  if (uart_thread_.joinable())
    uart_thread_.join();

  uart_.closePort();
  logger_.log("INFO", "bridge stopped");
}

void Bridge::uartLoop(std::atomic<bool> *stop_flag) {
  std::vector<json> sensor_buffer;
  logger_.log("INFO",
              "UART processing thread started (Queue-based, Batch size: " +
                  std::to_string(cfg_.sensor_batch_size) + ")");

  // 1초 주기를 관리하기 위한 기준 시간
  auto next_sensor_poll =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);

  while (stop_flag && !stop_flag->load()) {
    UartCommand cmd;
    bool has_cmd = false;

    {
      std::unique_lock<std::mutex> lock(queue_mu_);

      // 큐가 비어있고 센서 측정 주기 전이라면 대기
      auto now = std::chrono::steady_clock::now();
      if (cmd_queue_.empty() && now < next_sensor_poll) {
        queue_cv_.wait_until(lock, next_sensor_poll);
      }

      // 종료 확인
      if (stop_flag && stop_flag->load())
        break;

      if (!cmd_queue_.empty()) {
        cmd = cmd_queue_.front();
        cmd_queue_.pop_front();
        has_cmd = true;
      } else {
        // 큐는 비어있으나 주기 도래 -> 센서 읽기 예약
        cmd.type = UartCommand::Type::SENSOR_POLL;
        has_cmd = true;
        next_sensor_poll =
            std::chrono::steady_clock::now() + std::chrono::seconds(1);
      }
    }

    if (!has_cmd || (!uart_.isOpen()))
      continue;

    // ----- 1. 모터 명령 처리 -----
    if (cmd.type == UartCommand::Type::MOTOR_CMD) {
      StmFrame frame;
      std::string ferr;
      
      std::cout << "\033[1;33m[UART-SEND]\033[0m Motor Payload: " << cmd.payload << std::endl;
      
      uart_.flush(); // [DIP 최적화] 이전 통신 지연으로 남은 쓰레기 버퍼 비우기
      if (Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_MOTOR, cmd.payload)) {
        if (!Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr)) {
          logger_.log("ERROR", "UART motor cmd failed: " + ferr);
          std::cerr << "\033[1;31m[UART-ERROR]\033[0m Motor Read Fail: " << ferr << std::endl;
        } else {
          logger_.log("INFO", "Motor command relay OK");
          std::cout << "\033[1;32m[UART-RECV]\033[0m Motor Ack Payload: " << frame.payload_json << std::endl;
        }
      } else {
        logger_.log("ERROR", "UART motor send failed");
        std::cerr << "\033[1;31m[UART-ERROR]\033[0m Motor Send Fail" << std::endl;
      }
    }
    // ----- 2. 센서 수집 처리 -----
    else if (cmd.type == UartCommand::Type::SENSOR_POLL) {
      StmFrame frame;
      std::string ferr;
      bool success = false;

      // 센서 수집 요청 (Payload 없음)
      std::cout << "\033[1;33m[UART-SEND]\033[0m Sensor Poll Request" << std::endl;

      uart_.flush(); // [DIP 최적화] 버퍼의 만료된 응답이나 깨진 패킷의 헤더 잔재 비우기
      if (Stm32Proto::sendFrame(uart_, Stm32Proto::CMD_STATUS, "")) {
        success =
            Stm32Proto::readFrame(uart_, cfg_.uart_timeout_ms, frame, ferr);
      }

      if (success) {
        std::cout << "\033[1;32m[UART-RECV]\033[0m Sensor Data Payload: " << frame.payload_json << std::endl;
      } else if (!ferr.empty()) {
        std::cerr << "\033[1;31m[UART-ERROR]\033[0m Sensor Read Fail: " << ferr << std::endl;
      }

      if (success && frame.cmd == Stm32Proto::CMD_STATUS) {
        auto st = Stm32Proto::parseStatusJson(frame.payload_json);
        if (st) {
          sensor_buffer.push_back({{"tmp", st->tmp},
                                   {"hum", st->hum},
                                   {"dir", st->dir},
                                   {"tilt", st->tilt},
                                   {"light", st->light},
                                   {"ts", std::time(nullptr)}});
        }
      }

      // 설정된 배치 크기(cfg_.sensor_batch_size)만큼 모였으면 전송
      if (sensor_buffer.size() >= static_cast<size_t>(cfg_.sensor_batch_size)) {
        if (sender_) {
          std::string out = json{{"sensor_batch", sensor_buffer}}.dump();
          sender_->sendSensorData(out);
          // 콘솔 출력
          if (cfg_.sensor_batch_size == 1) {
            std::cout << "\033[1;36m[SENSOR]\033[0m 1 point forwarded to "
                         "NetworkFacade"
                      << std::endl;
          } else {
            std::cout << "\033[1;36m[SENSOR-BATCH]\033[0m "
                      << sensor_buffer.size() << " points forwarded."
                      << std::endl;
          }
        } else {
          std::cout
              << "\033[1;33m[BRIDGE-DIP]\033[0m Sender not set. Data dropped."
              << std::endl;
        }
        sensor_buffer.clear();
      }
    }
  }
}

bool Bridge::handleMotorCmd(const std::string &cmd) {
  if (!validMotorCmd(cmd)) {
    logger_.log("ERROR", "Invalid motor command: " + cmd);
    return false;
  }

  const std::string payload = json{{"motor", cmd}}.dump();

  // 비동기 큐에 모터 제어 명령 삽입 및 스레드 깨우기 (Non-Blocking)
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    cmd_queue_.push_back({UartCommand::Type::MOTOR_CMD, payload});
    queue_cv_.notify_one();
  }

  std::cout << "\033[1;35m[MOTOR-CMD]\033[0m Queued command: " << cmd
            << std::endl;
  return true;
}

bool Bridge::handleStatusReq() {
  // [DIP] 이 메서드는 이제 사용되지 않거나 필요 시 NetworkFacade를 통해
  // 응답하도록 변경 가능 현재는 주기적 배치가 주 목적이므로 스텁으로 둠
  return true;
}

bool Bridge::parseMotorCmdJson(const std::string &body_json, std::string &cmd) {
  const auto j = json::parse(body_json, nullptr, false);
  if (j.is_discarded() || !j.is_object())
    return false;
  if (j.contains("motor") && j.at("motor").is_string()) {
    cmd = j.at("motor").get<std::string>();
    return true;
  }
  return false;
}

bool Bridge::validMotorCmd(const std::string &cmd) {
  static const std::set<std::string> allowed = {"w",    "a",      "s",  "d",
                                                "auto", "unauto", "manual", "on", "off"};
  return allowed.find(cmd) != allowed.end();
}

} // namespace edge_device