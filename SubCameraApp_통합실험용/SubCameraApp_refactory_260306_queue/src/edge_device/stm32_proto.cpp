#include "stm32_proto.h"
#include "uart_port.h"

#include <cstring>
#include <nlohmann/json.hpp>

namespace edge_device {

using json = nlohmann::json;

#if defined(__GNUC__) && __GNUC__ >= 11
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

std::vector<uint8_t> Stm32Proto::buildFrame(uint8_t cmd,
                                            const std::string &payload_json) {
  const uint32_t len = static_cast<uint32_t>(payload_json.size());
  const size_t total_size = 5 + payload_json.size();

  std::vector<uint8_t> out(total_size);
  uint8_t *p = out.data();

  p[0] = cmd;
  p[1] = static_cast<uint8_t>((len >> 24) & 0xFF);
  p[2] = static_cast<uint8_t>((len >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((len >> 8) & 0xFF);
  p[4] = static_cast<uint8_t>(len & 0xFF);

  if (len > 0) {
    std::memcpy(p + 5, payload_json.data(), len);
  }

  return out;
}

#if defined(__GNUC__) && __GNUC__ >= 11
#pragma GCC diagnostic pop
#endif

bool Stm32Proto::sendFrame(UartPort &uart, uint8_t cmd,
                           const std::string &payload_json) {
  if (payload_json.size() > MAX_PAYLOAD) {
    return false;
  }
  return uart.writeAll(buildFrame(cmd, payload_json));
}

bool Stm32Proto::readFrame(UartPort &uart, int timeout_ms, StmFrame &frame,
                           std::string &err) {
  std::vector<uint8_t> header;
  if (!uart.readExact(header, 5, timeout_ms)) {
    err = "uart read timeout/header fail";
    return false;
  }

  const uint8_t cmd = header[0];
  // LEN is encoded in network (big-endian) order.
  const uint32_t len = (static_cast<uint32_t>(header[1]) << 24) |
                       (static_cast<uint32_t>(header[2]) << 16) |
                       (static_cast<uint32_t>(header[3]) << 8) |
                       static_cast<uint32_t>(header[4]);

  if (len > MAX_PAYLOAD) {
    err = "invalid payload length";
    return false;
  }

  std::string payload;
  if (len > 0) {
    std::vector<uint8_t> body;
    if (!uart.readExact(body, len, timeout_ms)) {
      err = "uart read timeout/body fail";
      return false;
    }
    payload.assign(body.begin(), body.end());
  }

  frame.cmd = cmd;
  frame.payload_json = payload;
  return true;
}

std::optional<StatusData>
Stm32Proto::parseStatusJson(const std::string &payload_json) {
  const auto j = json::parse(payload_json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return std::nullopt;
  }

  if (!j.contains("tmp") || !j.at("tmp").is_number())
    return std::nullopt;
  if (!j.contains("hum") || !j.at("hum").is_number())
    return std::nullopt;
  if (!j.contains("dir") || !j.at("dir").is_string())
    return std::nullopt;
  if (!j.contains("tilt") || !j.at("tilt").is_number())
    return std::nullopt;

  StatusData d;
  d.tmp = j.at("tmp").get<double>();
  d.hum = j.at("hum").get<double>();
  d.dir = j.at("dir").get<std::string>();
  d.tilt = j.at("tilt").get<double>();
  return d;
}

std::optional<MotorAckData>
Stm32Proto::parseMotorAckJson(const std::string &payload_json) {
  const auto j = json::parse(payload_json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return std::nullopt;
  }

  if (!j.contains("ok") || !j.at("ok").is_number_integer())
    return std::nullopt;
  if (!j.contains("mode") || !j.at("mode").is_string())
    return std::nullopt;
  if (!j.contains("cmd") || !j.at("cmd").is_string())
    return std::nullopt;

  MotorAckData d;
  d.ok = j.at("ok").get<int>();
  d.mode = j.at("mode").get<std::string>();
  d.cmd = j.at("cmd").get<std::string>();
  return d;
}

} // namespace edge_device
