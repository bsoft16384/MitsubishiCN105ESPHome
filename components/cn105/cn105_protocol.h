/// cn105_protocol.h — Pure protocol functions for the Mitsubishi CN105 UART protocol.
/// Role: Decoupled, testable logic for checksum and temperature encoding/decoding.
/// Deps: <cstdint>, <cmath> (no ESPHome dependency)
#pragma once

#include <cstdint>
#include <cmath>

namespace cn105_protocol {

// ════════════════════════════════════════════════════════════════
// Checksum
// ════════════════════════════════════════════════════════════════

/// Compute the CN105 packet checksum.
/// The checksum is: (0xFC - sum_of_all_bytes) & 0xFF.
/// @param bytes  Pointer to the full packet (header + payload), excluding the checksum byte itself.
/// @param len    Number of bytes to sum (packet length minus the final checksum byte).
/// @return       The expected checksum byte.
inline uint8_t checksum(const uint8_t *bytes, int len) {
  uint8_t sum = 0;
  for (int i = 0; i < len; i++) {
    sum += bytes[i];
  }
  return (0xfc - sum) & 0xff;
}

// ════════════════════════════════════════════════════════════════
// Temperature decoding / encoding
// ════════════════════════════════════════════════════════════════

/// Target setpoint limits accepted by the unit. Commanding outside this range is
/// pointless: the unit clamps it and reports the clamped value back, so anything
/// comparing a commanded setpoint against the readback would never converge.
inline constexpr float SETPOINT_MIN_C = 16.0f;
inline constexpr float SETPOINT_MAX_C = 31.0f;
inline constexpr float SETPOINT_STEP_C = 0.5f;

/// Round a setpoint to the nearest half-degree and clamp it into the range the
/// unit will actually accept.
inline float normalize_setpoint(float temperature) {
  const float rounded = std::round(temperature * 2.0f) / 2.0f;
  if (rounded < SETPOINT_MIN_C)
    return SETPOINT_MIN_C;
  if (rounded > SETPOINT_MAX_C)
    return SETPOINT_MAX_C;
  return rounded;
}

/// Encode a target temperature into the encoding B format (half-degree precision).
/// Formula: byte = round(temp * 2) + 128
///
/// @param temperature  Target temperature in °C.
/// @return             The encoded byte value for encoding B.
inline uint8_t encode_temperature_b(float temperature) {
  return static_cast<uint8_t>(std::round(temperature * 2.0f) + 128);
}

/// Encode a remote temperature into the two-byte format used by SET remote temp packets.
/// Byte 1 (encoding A legacy): round(temp * 2) - 16
/// Byte 2 (encoding B):        round(temp * 2) + 128
///
/// @param temperature  Remote temperature in °C.
/// @param[out] enc_a   Output: encoding A byte for the remote temp packet.
/// @param[out] enc_b   Output: encoding B byte for the remote temp packet.
inline void encode_remote_temperature(float temperature, uint8_t &enc_a, uint8_t &enc_b) {
  if (temperature < 8.0f) {
    temperature = 8.0f;
  } else if (temperature > 37.5f) {
    temperature = 37.5f;
  }
  float rounded = std::round(temperature * 2.0f);
  enc_a = static_cast<uint8_t>(rounded - 16);
  enc_b = static_cast<uint8_t>(rounded + 128);
}

}  // namespace cn105_protocol
