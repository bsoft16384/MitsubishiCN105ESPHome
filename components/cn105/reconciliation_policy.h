/// reconciliation_policy.h — Pure decision logic for the desired-state reconciler.
/// Role: given what the user asked for, what the unit last reported, and the
///       fan-stop / low-temperature-protection configuration, decide what
///       physical target the unit should be driven to and whether to command it.
/// Deps: cn105_types.h (enums), scheduling_policy.h (repeat-suppression window)
///
/// Why this is separate: the reconciler runs every 5 s and on every control()
/// call, and a wrong answer either fights the user, fights the IR remote, or
/// re-commands the unit forever without converging. It has four interacting
/// inputs (LTP state machine, fan-stop hysteresis band, mismatch detection,
/// repeat suppression) and no natural place to observe it on a live device, so
/// it is written as a pure function over explicit state.
///
/// Home Assistant's climate enums deliberately do not appear here. The caller
/// maps its ClimateMode onto HPMode (and the OFF case onto power_on == false)
/// before calling, which keeps this header free of any ESPHome dependency.
#pragma once

#include <cstdint>
#include <cmath>
#include <optional>
#include "cn105_types.h"
#include "cn105_protocol.h"
#include "scheduling_policy.h"

namespace cn105_policy {

/// Setpoints closer together than this are the same setpoint. The wire format
/// carries half degrees, so anything under a quarter degree is rounding noise.
inline constexpr float SETPOINT_EPSILON_C = 0.25f;

/// What the reconciler is currently doing, surfaced by the diagnostic sensor.
enum class ProtectionState : uint8_t {
  NORMAL,
  FAN_STOP_ACTIVE,
  LOW_TEMP_PROTECTION,
};

inline const char *protection_state_to_str(ProtectionState s) {
  switch (s) {
    case ProtectionState::LOW_TEMP_PROTECTION:
      return "Low Temp Protection";
    case ProtectionState::FAN_STOP_ACTIVE:
      return "Fan Stop Active";
    case ProtectionState::NORMAL:
    default:
      return "Normal";
  }
}

/// The physical state the unit should be driven to. `mode` is meaningless when
/// power_on is false, and may be empty when the desired Home Assistant mode has
/// no heatpump equivalent (in which case the unit's mode is left alone).
struct PhysicalTarget {
  bool power_on = false;
  std::optional<HPMode> mode;
  float temperature = NAN;

  /// Same physical target, in the sense used to suppress repeat commands.
  bool same_as(const PhysicalTarget &other) const {
    if (power_on != other.power_on) {
      return false;
    }
    if (!power_on) {
      return true;  // "off" is "off"; setpoint and mode are irrelevant
    }
    if (mode != other.mode) {
      return false;
    }
    if (std::isnan(temperature) || std::isnan(other.temperature)) {
      return false;
    }
    return std::fabs(temperature - other.temperature) < SETPOINT_EPSILON_C;
  }
};

struct ReconcileInputs {
  uint32_t now_ms = 0;
  uint32_t update_interval_ms = 0;

  // --- What the user asked for ---
  bool desired_power_on = false;
  /// Empty when the desired Home Assistant mode maps to no heatpump mode.
  std::optional<HPMode> desired_mode;
  float desired_temp = NAN;

  // --- What the unit last reported ---
  HPPower current_power = HPPower::UNKNOWN;
  HPMode current_mode = HPMode::UNKNOWN;
  std::optional<float> current_temperature;
  /// Room temperature as currently believed. NAN disables both protections.
  float current_temp = NAN;

  // --- Low temperature protection ---
  bool ltp_enabled = false;
  /// Latched state carried over from the previous evaluation.
  bool ltp_active = false;
  float low_temp_temp = 8.0f;
  float low_temp_hysteresis = 4.0f;

  // --- Fan stop ---
  bool fan_stop_enabled = false;
  float hysteresis = 0.5f;

  // --- Repeat suppression ---
  bool has_issued_command = false;
  PhysicalTarget last_issued;
  uint32_t last_issued_ms = 0;
};

struct ReconcileDecision {
  /// New latched LTP state; the caller stores this back.
  bool ltp_active = false;
  PhysicalTarget target;
  bool mode_mismatch = false;
  bool temp_mismatch = false;
  /// A mismatch exists but the same target was commanded too recently to have
  /// been confirmed by an info cycle yet.
  bool suppressed_repeat = false;
  /// The single output the caller acts on.
  bool should_command = false;
  ProtectionState protection = ProtectionState::NORMAL;

  bool has_mismatch() const { return mode_mismatch || temp_mismatch; }
};

/// Advance the low-temperature-protection latch.
///
/// Engages below low_temp_temp and only releases above low_temp_temp +
/// low_temp_hysteresis, so a room hovering at the threshold does not chatter.
/// An unknown room temperature neither engages nor releases it; disabling the
/// switch always releases it.
inline bool next_ltp_active(bool enabled, bool currently_active, float current_temp, float low_temp_temp,
                            float low_temp_hysteresis) {
  if (!enabled) {
    return false;
  }
  if (std::isnan(current_temp)) {
    return currently_active;
  }
  if (!currently_active && current_temp < low_temp_temp) {
    return true;
  }
  if (currently_active && current_temp > (low_temp_temp + low_temp_hysteresis)) {
    return false;
  }
  return currently_active;
}

/// Decide whether fan stop wants the unit running, for a single direction.
///
/// Outside the deadband the answer is unambiguous. Inside it, hold whatever the
/// unit is physically doing — that is what makes it a deadband rather than a
/// threshold, and it is why current_power is an input here.
inline bool fan_stop_wants_running(HPMode mode, float current_temp, float desired_temp, float hysteresis,
                                   HPPower current_power) {
  const bool heating = (mode == HPMode::HEAT);
  const float satisfied_at = heating ? desired_temp + hysteresis : desired_temp - hysteresis;
  const float demand_at = heating ? desired_temp - hysteresis : desired_temp + hysteresis;

  if (heating ? (current_temp >= satisfied_at) : (current_temp <= satisfied_at)) {
    return false;
  }
  if (heating ? (current_temp <= demand_at) : (current_temp >= demand_at)) {
    return true;
  }
  return current_power != HPPower::OFF;  // inside the deadband: hold
}

/// Resolve the physical target: what the unit should actually be set to, after
/// low-temperature protection and fan stop have had their say.
///
/// Precedence is LTP first — it exists to stop pipes freezing and must win over
/// a comfort feature that deliberately switches the unit off.
inline PhysicalTarget resolve_physical_target(const ReconcileInputs &in, bool ltp_active) {
  PhysicalTarget target;
  target.power_on = in.desired_power_on;
  target.mode = in.desired_power_on ? in.desired_mode : std::nullopt;
  target.temperature = in.desired_temp;

  if (ltp_active) {
    target.power_on = true;
    target.mode = HPMode::HEAT;
    // Aim above the release threshold, or at the user's setpoint if that is higher,
    // so protection does not fight a warmer request.
    const float floor_temp = in.low_temp_temp + in.low_temp_hysteresis;
    target.temperature = std::isnan(in.desired_temp) ? floor_temp : std::fmax(floor_temp, in.desired_temp);
    return target;
  }

  // Fan stop only governs the two modes where "satisfied" is well defined.
  const bool mode_is_governed =
      in.desired_power_on && in.desired_mode.has_value() &&
      (*in.desired_mode == HPMode::HEAT || *in.desired_mode == HPMode::COOL);
  if (!in.fan_stop_enabled || !mode_is_governed || std::isnan(in.current_temp) || std::isnan(in.desired_temp)) {
    return target;
  }

  if (!fan_stop_wants_running(*in.desired_mode, in.current_temp, in.desired_temp, in.hysteresis, in.current_power)) {
    target.power_on = false;
    target.mode = std::nullopt;
  }
  return target;
}

/// Full reconciler decision. Pure: the caller applies `ltp_active` and, when
/// `should_command` is set, writes `target` into the wanted settings.
inline ReconcileDecision decide_reconciliation(const ReconcileInputs &in) {
  ReconcileDecision out;

  out.ltp_active =
      next_ltp_active(in.ltp_enabled, in.ltp_active, in.current_temp, in.low_temp_temp, in.low_temp_hysteresis);
  out.target = resolve_physical_target(in, out.ltp_active);

  // --- Does the unit already match the target? ---
  if (!out.target.power_on) {
    out.mode_mismatch = (in.current_power != HPPower::OFF);
  } else {
    if (in.current_power != HPPower::ON) {
      out.mode_mismatch = true;
    }
    if (out.target.mode.has_value() && in.current_mode != *out.target.mode) {
      out.mode_mismatch = true;
    }
    const float normalized = cn105_protocol::normalize_setpoint(out.target.temperature);
    if (!in.current_temperature.has_value() ||
        std::fabs(*in.current_temperature - normalized) >= SETPOINT_EPSILON_C) {
      out.temp_mismatch = true;
    }
  }

  // --- Would re-commanding help? ---
  // current_* is only refreshed by an info cycle, and issuing a command defers the
  // next one. Repeating a target that has not had time to be confirmed cannot
  // resolve the mismatch; it only pushes the confirming cycle further out.
  const bool same_target = in.has_issued_command && out.target.same_as(in.last_issued);
  out.suppressed_repeat = should_suppress_repeat_command(in.has_issued_command, same_target, in.now_ms,
                                                         in.last_issued_ms, in.update_interval_ms);
  out.should_command = out.has_mismatch() && !out.suppressed_repeat;

  // --- Diagnostic ---
  if (out.ltp_active) {
    out.protection = ProtectionState::LOW_TEMP_PROTECTION;
  } else if (in.fan_stop_enabled && in.current_power == HPPower::OFF && in.desired_power_on) {
    out.protection = ProtectionState::FAN_STOP_ACTIVE;
  } else {
    out.protection = ProtectionState::NORMAL;
  }

  return out;
}

}  // namespace cn105_policy
