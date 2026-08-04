/// decoder.h — Pure decoders for CN105 response payloads.
/// Role: turn the payload bytes of a 0x62 response into decoded values, with no
///       ESPHome dependency and no side effects, so the wire format can be
///       exercised directly by the unit test suite against captured frames.
/// Deps: cn105_types.h (enums + wire tables), <optional>, <cmath>
///
/// Split of responsibilities:
///   - This header answers "what do these bytes say?". Every field that the unit
///     may report as an unrecognised byte is a std::optional, empty on a miss —
///     these decoders never silently substitute a value.
///   - The caller answers "what do we do about it?": resolving an empty optional
///     against the value already held (resolve_or_keep below), deciding which
///     entities exist to publish to, and logging.
///
/// Payload indexing note: `data` points at the payload, i.e. offset 5 of the
/// frame, so data[0] is the response code (0x02, 0x03, 0x06, ...).
#pragma once

#include <cstdint>
#include <cmath>
#include <optional>
#include "cn105_types.h"

namespace cn105_decoder {

/// Minimum payload length each decoder needs. A response shorter than this is
/// truncated or from a model that does not implement the field set, and the
/// matching decode function returns nullopt rather than reading past the end.
inline constexpr int SETTINGS_MIN_LEN = 15;
inline constexpr int ROOM_TEMP_MIN_LEN = 7;
inline constexpr int ROOM_TEMP_RUNTIME_LEN = 14;
inline constexpr int STATUS_MIN_LEN = 9;
inline constexpr int POWER_MIN_LEN = 6;
inline constexpr int ERROR_INFO_MIN_LEN = 6;

/// Bounds-checked payload read. Out-of-range indices yield default_val rather
/// than reading past the end of the frame buffer.
inline uint8_t payload_byte(const uint8_t *data, int length, int index, uint8_t default_val = 0) {
  if (data == nullptr || index < 0 || index >= length) {
    return default_val;
  }
  return data[index];
}

/// Resolve a decoded field against what is already known.
///
/// An unrecognised byte is not evidence that the setting changed, so the value
/// already held wins. The per-field fallback only applies before anything has
/// been decoded successfully. Relies on every mapped enum having an UNKNOWN
/// sentinel to mean "nothing known yet".
template<typename E> inline E resolve_or_keep(std::optional<E> decoded, E current, E fallback) {
  if (decoded.has_value()) {
    return *decoded;
  }
  return (current != E::UNKNOWN) ? current : fallback;
}

// ════════════════════════════════════════════════════════════════
// 0x02 — Settings
// ════════════════════════════════════════════════════════════════

struct DecodedSettings {
  std::optional<HPPower> power;
  uint8_t power_byte = 0;

  /// The unit's own mode, with i-See folded out of the byte. HPMode::AUTO here
  /// is the unit's internal auto mode; mapping that onto something Home
  /// Assistant understands is the caller's decision, not the wire format's.
  std::optional<HPMode> mode;
  uint8_t mode_byte = 0;
  bool i_see = false;

  /// Empty when the unit uses the legacy encoding (data[11] == 0x00) and so has
  /// no high-precision setpoint to report; legacy_temperature_encoding says which.
  std::optional<float> temperature;
  bool legacy_temperature_encoding = false;

  std::optional<HPFanMode> fan;
  uint8_t fan_byte = 0;

  std::optional<HPVaneMode> vane;
  uint8_t vane_byte = 0;

  /// data[10] == 0 means this unit does not report a horizontal vane at all.
  bool wide_vane_present = false;
  std::optional<HPWideVaneMode> wide_vane;
  uint8_t wide_vane_byte = 0;
  bool wide_vane_adjustment = false;

  /// Percentage, only on models that populate data[12]. Empty when the byte is
  /// 0x00 (unsupported) or outside 1..100 (which humidity_byte then carries).
  std::optional<uint8_t> target_humidity;
  uint8_t humidity_byte = 0;

  /// Airflow control is only meaningful when data[10] == 0x80 *and* i-See is
  /// active; otherwise the unit is spreading air evenly regardless of data[14].
  bool airflow_control_reported = false;
  std::optional<HPAirflowControl> airflow_control;
  uint8_t airflow_byte = 0;
};

inline std::optional<DecodedSettings> decode_settings(const uint8_t *data, int length) {
  if (data == nullptr || length < SETTINGS_MIN_LEN) {
    return std::nullopt;
  }
  DecodedSettings out;

  out.power_byte = payload_byte(data, length, 3);
  out.power = hp_power_from_wire(out.power_byte);

  // Bit 3 of data[4] flags an i-See-equipped unit; the mode occupies the low bits.
  const uint8_t raw_mode = payload_byte(data, length, 4);
  out.i_see = raw_mode > 0x08;
  out.mode_byte = out.i_see ? static_cast<uint8_t>(raw_mode - 0x08) : raw_mode;
  out.mode = hp_mode_from_wire(out.mode_byte);

  const uint8_t temp_byte = payload_byte(data, length, 11);
  if (temp_byte != 0x00) {
    out.temperature = static_cast<float>(static_cast<int>(temp_byte) - 128) / 2.0f;
  } else {
    out.legacy_temperature_encoding = true;
  }

  out.fan_byte = payload_byte(data, length, 6);
  out.fan = hp_fan_from_wire(out.fan_byte);

  out.vane_byte = payload_byte(data, length, 7);
  out.vane = hp_vane_from_wire(out.vane_byte);

  const uint8_t wide_vane_raw = payload_byte(data, length, 10);
  out.wide_vane_present = wide_vane_raw != 0;
  if (out.wide_vane_present) {
    out.wide_vane_byte = wide_vane_raw & 0x0F;
    out.wide_vane = hp_wide_vane_from_wire(out.wide_vane_byte);
    out.wide_vane_adjustment = (wide_vane_raw & 0xF0) == 0x80;
  }

  out.humidity_byte = payload_byte(data, length, 12);
  if (out.humidity_byte > 0 && out.humidity_byte <= 100) {
    out.target_humidity = out.humidity_byte;
  }

  out.airflow_byte = payload_byte(data, length, 14);
  out.airflow_control_reported = (wide_vane_raw == 0x80) && out.i_see;
  if (out.airflow_control_reported) {
    out.airflow_control = hp_airflow_control_from_wire(out.airflow_byte);
  } else {
    // Not reporting directed airflow means air is being spread evenly. This also
    // covers data[10] == 0x80 without i-See active, which some units do; the real
    // mode is unknowable there, and EVEN is the observed behaviour.
    out.airflow_control = HPAirflowControl::EVEN;
  }

  return out;
}

// ════════════════════════════════════════════════════════════════
// 0x03 — Room temperature
// ════════════════════════════════════════════════════════════════

struct DecodedRoomTemperature {
  /// NAN when the unit reports no outside sensor (data[5] <= 1).
  float outside_air_temperature = NAN;
  /// Empty when neither encoding produced a usable reading.
  std::optional<float> room_temperature;
  /// True when the value came from the coarse 1 °C data[3] map rather than the
  /// half-degree data[6] encoding.
  bool room_temperature_is_legacy = false;
  uint8_t legacy_room_temp_byte = 0;
  /// Only present on payloads long enough to carry the operating-minutes counter.
  std::optional<float> runtime_hours;
};

inline std::optional<DecodedRoomTemperature> decode_room_temperature(const uint8_t *data, int length) {
  if (data == nullptr || length < ROOM_TEMP_MIN_LEN) {
    return std::nullopt;
  }
  DecodedRoomTemperature out;

  const uint8_t outside_byte = payload_byte(data, length, 5);
  out.outside_air_temperature = outside_byte > 1 ? (static_cast<int>(outside_byte) - 128) / 2.0f : NAN;

  const uint8_t room_byte = payload_byte(data, length, 6);
  if (room_byte != 0x00) {
    out.room_temperature = (static_cast<int>(room_byte) - 128) / 2.0f;
  } else {
    // Older encoding: data[3] is an index into a 10..41 °C table.
    out.room_temperature_is_legacy = true;
    out.legacy_room_temp_byte = payload_byte(data, length, 3);
    if (out.legacy_room_temp_byte <= 31) {
      out.room_temperature = static_cast<float>(10 + out.legacy_room_temp_byte);
    }
  }

  if (length >= ROOM_TEMP_RUNTIME_LEN) {
    const uint32_t minutes = (static_cast<uint32_t>(payload_byte(data, length, 11)) << 16) |
                             (static_cast<uint32_t>(payload_byte(data, length, 12)) << 8) |
                             static_cast<uint32_t>(payload_byte(data, length, 13));
    out.runtime_hours = static_cast<float>(minutes) / 60.0f;
  }

  return out;
}

/// Heuristic for whether the unit has adopted the remote temperature we feed it.
///
/// The unit never tells us which sensor it is using, so we compare the room
/// temperature it echoes back against the value we sent: within the margin means
/// it took ours. Only meaningful while we are actively sending one.
inline bool unit_adopted_remote_temperature(std::optional<float> reported_room_temp,
                                            std::optional<float> remote_temp, bool keepalive_active, float margin) {
  if (!keepalive_active || !remote_temp.has_value() || !reported_room_temp.has_value()) {
    return false;
  }
  if (std::isnan(*reported_room_temp) || std::isnan(*remote_temp)) {
    return false;
  }
  return std::fabs(*reported_room_temp - *remote_temp) <= margin;
}

// ════════════════════════════════════════════════════════════════
// 0x06 — Status (operating / compressor / energy)
// ════════════════════════════════════════════════════════════════

struct DecodedStatus {
  bool operating = false;
  /// Forced to 0 while not operating: several models emit noise on this field
  /// when the compressor is stopped.
  float compressor_frequency = 0.0f;
  /// Raw 16-bit counters, before any unit conversion (see convert_* below).
  uint16_t raw_input_power = 0;
  uint16_t raw_energy_usage = 0;
};

inline std::optional<DecodedStatus> decode_status(const uint8_t *data, int length) {
  if (data == nullptr || length < STATUS_MIN_LEN) {
    return std::nullopt;
  }
  DecodedStatus out;
  out.operating = payload_byte(data, length, 4) != 0;
  out.compressor_frequency = out.operating ? static_cast<float>(payload_byte(data, length, 3)) : 0.0f;
  out.raw_input_power = static_cast<uint16_t>((payload_byte(data, length, 5) << 8) | payload_byte(data, length, 6));
  out.raw_energy_usage = static_cast<uint16_t>((payload_byte(data, length, 7) << 8) | payload_byte(data, length, 8));
  return out;
}

/// Convert the raw input-power counter to Watts.
/// The protocol normally reports native Watts; some firmwares (e.g. certain
/// MSZ-LN models) encode BTU/s instead, selected by `power_unit_is_btu`.
///   1 W = 1 J/s, 1 BTU = 1055.056 J  =>  BTU/s * (3600 / 1055.056) = W
inline float convert_input_power_to_w(float raw_input_power, bool power_unit_is_btu) {
  if (power_unit_is_btu) {
    constexpr float conv_factor = 3600.0f / 1055.05558262f;
    return raw_input_power * conv_factor;
  }
  return raw_input_power;
}

/// Convert the raw energy counter to kWh.
/// Normally encoded as kWh * 10; in BTU mode the counter is kBTU.
inline float convert_energy_usage_to_kwh(float raw_energy_usage, bool power_unit_is_btu) {
  if (power_unit_is_btu) {
    constexpr float conv_factor = 1055.05585262f / 3600000.0f;
    return 1000.0f * raw_energy_usage * conv_factor;
  }
  return raw_energy_usage / 10.0f;
}

// ════════════════════════════════════════════════════════════════
// 0x09 — Sub-modes (stage / sub-mode / auto sub-mode)
// ════════════════════════════════════════════════════════════════

struct DecodedSubModes {
  std::optional<HPStage> stage;
  uint8_t stage_byte = 0;
  std::optional<HPSubMode> sub_mode;
  uint8_t sub_mode_byte = 0;
  std::optional<HPAutoSubMode> auto_sub_mode;
  uint8_t auto_sub_mode_byte = 0;
};

inline std::optional<DecodedSubModes> decode_sub_modes(const uint8_t *data, int length) {
  if (data == nullptr || length < POWER_MIN_LEN) {
    return std::nullopt;
  }
  DecodedSubModes out;
  out.stage_byte = payload_byte(data, length, 4);
  out.stage = hp_stage_from_wire(out.stage_byte);
  out.sub_mode_byte = payload_byte(data, length, 3);
  out.sub_mode = hp_sub_mode_from_wire(out.sub_mode_byte);
  out.auto_sub_mode_byte = payload_byte(data, length, 5);
  out.auto_sub_mode = hp_auto_sub_mode_from_wire(out.auto_sub_mode_byte);
  return out;
}

// ════════════════════════════════════════════════════════════════
// 0x04 — Error info
// ════════════════════════════════════════════════════════════════

struct DecodedErrorInfo {
  /// Bit 7 of data[4] is an "error reporting available" protocol flag, not part
  /// of the code, so it is masked off here.
  uint8_t code = 0;
  uint8_t sub_code = 0;
  bool no_error = true;
};

inline std::optional<DecodedErrorInfo> decode_error_info(const uint8_t *data, int length) {
  if (data == nullptr || length < ERROR_INFO_MIN_LEN) {
    return std::nullopt;
  }
  DecodedErrorInfo out;
  out.code = payload_byte(data, length, 4) & 0x7F;
  out.sub_code = payload_byte(data, length, 5);
  out.no_error = (out.code == 0x00 && out.sub_code == 0x00);
  return out;
}

}  // namespace cn105_decoder
