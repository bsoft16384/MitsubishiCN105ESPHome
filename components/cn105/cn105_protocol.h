/// cn105_protocol.h — Pure protocol functions for the Mitsubishi CN105 UART protocol.
/// Role: Decoupled, testable logic for checksum, temperature encoding/decoding, and byte-map lookups.
/// Deps: <cstdint>, <cmath>, <cstring>, <optional> (no ESPHome dependency)
#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <optional>

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

// ════════════════════════════════════════════════════════════════
// Byte-map lookups — fallback variant (legacy compatibility)
// ════════════════════════════════════════════════════════════════

/// Look up a mapped value from a parallel byte-map / value-map pair.
/// Scans the byte_map for a matching byte_value and returns the corresponding entry in values_map.
/// Returns values_map[0] if no match is found (safe fallback for protocol continuity).
///
/// @tparam T         Value type (typically const char* or int).
/// @param values_map  Array of mapped values.
/// @param byte_map    Array of protocol byte codes (same length as values_map).
/// @param len        Number of entries in both arrays.
/// @param byte_value  The raw protocol byte to look up.
/// @return           The corresponding value, or values_map[0] if not found.
template<typename T> inline T lookup_value(const T values_map[], const uint8_t byte_map[], int len, uint8_t byte_value) {
  for (int i = 0; i < len; i++) {
    if (byte_map[i] == byte_value) {
      return values_map[i];
    }
  }
  return values_map[0];
}

/// Look up the index of a value in a value-map.
/// Returns the index (usable as an offset into the parallel byte-map), or -1 if not found.
///
/// @param values_map   Array of mapped values (int variant).
/// @param len         Number of entries.
/// @param lookup_value The value to search for.
/// @return            Index of the match, or -1 if not found.
template<typename T> inline int lookup_index(const T values_map[], int len, T lookup_value) {
  for (int i = 0; i < len; i++) {
    if (values_map[i] == lookup_value) {
      return i;
    }
  }
  return -1;
}

/// Look up the index of a string value in a value-map (case-insensitive).
///
/// @param values_map   Array of mapped string values.
/// @param len         Number of entries.
/// @param lookup_value The string to search for.
/// @return            Index of the match, or -1 if not found.
inline int lookup_index(const char *values_map[], int len, const char *lookup_value) {
  for (int i = 0; i < len; i++) {
    if (strcasecmp(values_map[i], lookup_value) == 0) {
      return i;
    }
  }
  return -1;
}

// ════════════════════════════════════════════════════════════════
// Byte-map lookups — std::optional variant (graceful degradation)
// ════════════════════════════════════════════════════════════════

/// Look up a mapped value, returning std::nullopt on miss.
/// Unlike lookup_value(), this does NOT silently fall back to index 0.
/// Callers can decide how to handle unknown bytes (keep previous value, log, etc.).
///
/// @tparam T         Value type (typically const char* or int).
/// @param values_map  Array of mapped values.
/// @param byte_map    Array of protocol byte codes (same length as values_map).
/// @param len        Number of entries in both arrays.
/// @param byte_value  The raw protocol byte to look up.
/// @return           The corresponding value, or std::nullopt if not found.
template<typename T>
inline std::optional<T> lookup_value_opt(const T values_map[], const uint8_t byte_map[], int len, uint8_t byte_value) {
  for (int i = 0; i < len; i++) {
    if (byte_map[i] == byte_value) {
      return values_map[i];
    }
  }
  return std::nullopt;
}

/// Look up the index of a value in a value-map, returning std::nullopt on miss.
///
/// @param values_map   Array of mapped values (int variant).
/// @param len         Number of entries.
/// @param lookup_value The value to search for.
/// @return            Index of the match, or std::nullopt if not found.
inline std::optional<int> lookup_index_opt(const int values_map[], int len, int lookup_value) {
  for (int i = 0; i < len; i++) {
    if (values_map[i] == lookup_value) {
      return i;
    }
  }
  return std::nullopt;
}

/// Look up the index of a string value (case-insensitive), returning std::nullopt on miss.
///
/// @param values_map   Array of mapped string values.
/// @param len         Number of entries.
/// @param lookup_value The string to search for.
/// @return            Index of the match, or std::nullopt if not found.
inline std::optional<int> lookup_index_opt(const char *values_map[], int len, const char *lookup_value) {
  for (int i = 0; i < len; i++) {
    if (strcasecmp(values_map[i], lookup_value) == 0) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace cn105_protocol
