#pragma once

#include <cstdint>
#include <functional>

namespace esphome {

class CN105Climate;  // forward declaration

struct InfoRequest {
  const char *id;
  const char *description;
  uint8_t code;                // e.g. 0x02, 0x03, 0x06, 0x09
  bool disabled;               // permanently disabled when not supported
  bool awaiting;               // awaiting a matching response
  uint32_t interval_ms;        // Minimum time between requests for this specific code
  uint32_t last_request_time;  // Last time this request was sent (millis)
  const char *log_tag;         // Custom log tag (optional), defaults to LOG_CYCLE_TAG logic

  // Optional condition to decide whether this request should be sent in this device/config
  std::function<bool(const CN105Climate &)> can_send;

  // Optional response handler invoked when the matching response (code) is received
  std::function<void(CN105Climate &)> on_response;

  InfoRequest(const char *id, const char *description, uint8_t code, uint32_t interval_ms = 0,
              const char *log_tag = nullptr)
      : id(id),
        description(description),
        code(code),
        disabled(false),
        awaiting(false),
        interval_ms(interval_ms),
        last_request_time(0),
        log_tag(log_tag),
        can_send(nullptr),
        on_response(nullptr) {}
};
}  // namespace esphome
