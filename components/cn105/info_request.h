#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <cstdio>

namespace esphome {

class CN105Climate;  // forward declaration

inline constexpr const char *get_timeout_name_for_code(uint8_t code) {
  switch (code) {
    case 0x02:
      return "info_timeout_0x02";
    case 0x03:
      return "info_timeout_0x03";
    case 0x04:
      return "info_timeout_0x04";
    case 0x05:
      return "info_timeout_0x05";
    case 0x06:
      return "info_timeout_0x06";
    case 0x09:
      return "info_timeout_0x09";
    case 0x20:
      return "info_timeout_0x20";
    case 0x22:
      return "info_timeout_0x22";
    case 0x42:
      return "info_timeout_0x42";
    case 101:
      return "info_timeout_101";
    case 102:
      return "info_timeout_102";
    case 103:
      return "info_timeout_103";
    case 104:
      return "info_timeout_104";
    case 105:
      return "info_timeout_105";
    case 106:
      return "info_timeout_106";
    case 107:
      return "info_timeout_107";
    case 108:
      return "info_timeout_108";
    case 109:
      return "info_timeout_109";
    case 110:
      return "info_timeout_110";
    case 111:
      return "info_timeout_111";
    case 112:
      return "info_timeout_112";
    case 113:
      return "info_timeout_113";
    case 114:
      return "info_timeout_114";
    case 115:
      return "info_timeout_115";
    case 116:
      return "info_timeout_116";
    case 117:
      return "info_timeout_117";
    case 118:
      return "info_timeout_118";
    case 119:
      return "info_timeout_119";
    case 120:
      return "info_timeout_120";
    case 121:
      return "info_timeout_121";
    case 122:
      return "info_timeout_122";
    case 123:
      return "info_timeout_123";
    case 124:
      return "info_timeout_124";
    case 125:
      return "info_timeout_125";
    case 126:
      return "info_timeout_126";
    case 127:
      return "info_timeout_127";
    case 128:
      return "info_timeout_128";
    default:
      return "info_timeout_unknown";
  }
}

struct InfoRequest {
  const char *id;
  const char *description;
  uint8_t code;                // e.g. 0x02, 0x03, 0x06, 0x09, 0x42
  uint8_t maxFailures;         // disable after this many soft failures
  uint8_t failures;            // current failure count
  bool disabled;               // permanently disabled when not supported
  bool awaiting;               // awaiting a matching response
  uint32_t soft_timeout_ms;    // optional: skip forward on timeout without blocking cycle
  uint32_t interval_ms;        // Minimum time between requests for this specific code
  uint32_t last_request_time;  // Last time this request was sent (millis)
  const char *timeout_name;    // unique scheduler name for soft-timeout
  const char *log_tag;         // Custom log tag (optional), defaults to LOG_CYCLE_TAG logic

  // Optional condition to decide whether this request should be sent in this device/config
  std::function<bool(const CN105Climate &)> can_send;

  // Optional response handler invoked when the matching response (code) is received
  std::function<void(CN105Climate &)> on_response;

  InfoRequest(const char *id, const char *description, uint8_t code, uint8_t maxFailures = 3,
              uint32_t soft_timeout_ms = 0, uint32_t interval_ms = 0, const char *log_tag = nullptr)
      : id(id),
        description(description),
        code(code),
        maxFailures(maxFailures),
        failures(0),
        disabled(false),
        awaiting(false),
        soft_timeout_ms(soft_timeout_ms),
        interval_ms(interval_ms),
        last_request_time(0),
        timeout_name(get_timeout_name_for_code(code)),
        log_tag(log_tag),
        can_send(nullptr),
        on_response(nullptr) {}
};
}  // namespace esphome
