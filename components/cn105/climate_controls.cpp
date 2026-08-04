#include "cn105.h"
#include "globals.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include <utility>

using namespace esphome;

namespace {
/// Map a Home Assistant climate mode onto the heatpump mode that realises it.
/// Returns nullopt for CLIMATE_MODE_OFF (handled via the power flag) and for any
/// mode this component does not expose.
std::optional<HPMode> climate_mode_to_hp_mode(climate::ClimateMode mode) {
  switch (mode) {
    case climate::CLIMATE_MODE_HEAT:
      return HPMode::HEAT;
    case climate::CLIMATE_MODE_COOL:
      return HPMode::COOL;
    case climate::CLIMATE_MODE_DRY:
      return HPMode::DRY;
    case climate::CLIMATE_MODE_FAN_ONLY:
      return HPMode::FAN;
    default:
      return std::nullopt;
  }
}
}  // namespace

void CN105Climate::check_pending_wanted_settings() {
  // Already in-flight — don't log or re-send
  if (this->wanted_settings_.has_been_sent) {
    return;
  }

  uint32_t now = CUSTOM_MILLIS;
  if (!(this->wanted_settings_.has_changed) || (now - this->wanted_settings_.last_change < this->debounce_delay_)) {
    return;
  }

  // Don't log if send_wanted_settings() will defer due to write throttle (300ms)
  if (now - this->last_send_ <= 300) {
    return;
  }

  // A control() call can be flagged as changed while every field ends up unset — for
  // instance when evaluate_fan_stop_and_ltp() suppressed a repeat command. Sending
  // then writes a SET frame with no control flags: a no-op on the wire that still
  // defers the next info cycle. Drop it instead.
  if (!this->wanted_settings_.has_payload()) {
    ESP_LOGD(LOG_ACTION_EVT_TAG, "wanted settings carry no change, nothing to send");
    this->wanted_settings_.reset_settings();
    return;
  }

  ESP_LOGI(LOG_ACTION_EVT_TAG,
           "checkPendingWantedSettings - wanted settings have changed, sending them to the heatpump...");
  this->send_wanted_settings();
}

void CN105Climate::check_pending_wanted_run_states() {
  // Already in-flight — don't log or re-send
  if (this->wanted_run_states_.has_been_sent) {
    return;
  }

  uint32_t now = CUSTOM_MILLIS;
  if (!(this->wanted_run_states_.has_changed) || (now - this->wanted_run_states_.last_change < this->debounce_delay_)) {
    return;
  }

  // Don't log if send_wanted_run_states() will defer due to write throttle (300ms)
  if (now - this->last_send_ <= 300) {
    return;
  }

  ESP_LOGI(LOG_ACTION_EVT_TAG,
           "checkPendingWantedRunStates - wanted run states have changed, sending them to the heatpump...");
  this->send_wanted_run_states();
}
void CN105Climate::control_delegate(const esphome::climate::ClimateCall &call) {
  ESP_LOGD("control", "espHome control() interface method called...");
  bool updated = false;

  if (call.get_mode().has_value()) {
    this->desired_mode_ = *call.get_mode();
    this->mode = this->desired_mode_;
    if (this->desired_mode_ != climate::CLIMATE_MODE_OFF && std::isnan(this->desired_temp_)) {
      this->desired_temp_ = 21.0f;  // Safe default fallback
    }
    updated = true;
  }
  if (call.get_target_temperature().has_value()) {
    this->desired_temp_ = *call.get_target_temperature();
    this->target_temperature = this->desired_temp_;
    updated = true;
  }

  if (updated) {
    this->evaluate_fan_stop_and_ltp();
  }

  updated = this->process_fan_change(call) || updated;
  updated = this->process_swing_change(call) || updated;

  this->finalize_control_if_updated(updated);
}

bool CN105Climate::process_fan_change(const esphome::climate::ClimateCall &call) {
  if (!call.get_fan_mode().has_value()) {
    return false;
  }
  ESP_LOGD("control", "Fan change asked");
  this->fan_mode = *call.get_fan_mode();
  this->control_fan();
  return true;
}

bool CN105Climate::process_swing_change(const esphome::climate::ClimateCall &call) {
  if (!call.get_swing_mode().has_value()) {
    return false;
  }
  ESP_LOGD("control", "Swing change asked");
  this->swing_mode = *call.get_swing_mode();
  this->control_swing();
  return true;
}

void CN105Climate::finalize_control_if_updated(bool updated) {
  if (!updated) {
    return;
  }
  ESP_LOGD(LOG_ACTION_EVT_TAG, "clim.control() -> User changed something...");
  this->wanted_settings_.has_changed = true;
  this->wanted_settings_.has_been_sent = false;
  this->wanted_settings_.last_change = CUSTOM_MILLIS;
  this->debug_settings("control (wanted_settings_)", this->wanted_settings_);
  this->publish_state();
}

void CN105Climate::control(const esphome::climate::ClimateCall &call) { this->control_delegate(call); }

/**
 * @brief Controls the swing modes based on user selection.
 *
 * This function handles the logic for CLIMATE_SWING_OFF, VERTICAL, HORIZONTAL, and BOTH.
 * It is designed to be safe for units that do not support horizontal swing (wide_vane)
 * and provides an intuitive user experience by preserving static vane settings when possible.
 */
void CN105Climate::control_swing() {
  // Check if horizontal vane (wide_vane) is supported by this unit at the beginning.
  bool wide_vane_supported = this->traits_.supports_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);
  bool vane_is_swing = this->current_settings_.vane == HPVaneMode::SWING;
  bool wide_is_swing = this->current_settings_.wide_vane == HPWideVaneMode::SWING;

  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:
      // When swing is turned OFF, conditionally set vanes to a default static position.
      // This only sets default position if swing was previously enabled
      if (vane_is_swing) {
        this->set_vane_setting("AUTO");
      }
      if (wide_vane_supported && wide_is_swing) {
        this->set_wide_vane_setting("|");
      }
      break;

    case climate::CLIMATE_SWING_VERTICAL:
      // Turn on vertical swing.
      this->set_vane_setting("SWING");
      // If horizontal swing was also on AND is supported, turn it off to a default static position.
      // This correctly handles switching from BOTH to VERTICAL, while preserving any user's
      // static horizontal setting if it wasn't swinging.
      if (wide_vane_supported && wide_is_swing) {
        this->set_wide_vane_setting("|");
      }
      break;

    case climate::CLIMATE_SWING_HORIZONTAL:
      // If vertical swing was on, turn it off to a default static position.
      // This correctly handles switching from BOTH to HORIZONTAL, while preserving any user's
      // static vertical setting if it wasn't swinging.
      if (vane_is_swing) {
        this->set_vane_setting("AUTO");
      }
      // Turn on horizontal swing, but only if the unit supports it.
      if (wide_vane_supported) {
        this->set_wide_vane_setting("SWING");
      }
      break;

    case climate::CLIMATE_SWING_BOTH:
      // Turn on vertical swing.
      this->set_vane_setting("SWING");
      // Turn on horizontal swing, but only if the unit supports it.
      if (wide_vane_supported) {
        this->set_wide_vane_setting("SWING");
      }
      break;

    default:
      ESP_LOGW(TAG, "control - received unsupported swing mode request.");
      break;
  }
}
void CN105Climate::control_fan() {
  switch (this->fan_mode.value()) {
    case climate::CLIMATE_FAN_OFF:
      // Also update the desired state, otherwise evaluate_fan_stop_and_ltp() sees a
      // mode mismatch on its next pass and turns the unit right back on.
      this->desired_mode_ = climate::CLIMATE_MODE_OFF;
      this->mode = climate::CLIMATE_MODE_OFF;
      this->set_power_setting("OFF");
      break;
    case climate::CLIMATE_FAN_QUIET:
      this->set_fan_speed("QUIET");
      break;
    case climate::CLIMATE_FAN_DIFFUSE:
      this->set_fan_speed("QUIET");
      break;
    case climate::CLIMATE_FAN_LOW:
      this->set_fan_speed("1");
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      this->set_fan_speed("2");
      break;
    case climate::CLIMATE_FAN_MIDDLE:
      this->set_fan_speed("3");
      break;
    case climate::CLIMATE_FAN_HIGH:
      this->set_fan_speed("4");
      break;
    case climate::CLIMATE_FAN_ON:
    case climate::CLIMATE_FAN_AUTO:
    default:
      this->set_fan_speed("AUTO");
      break;
  }
}

void CN105Climate::set_action_if_operating_to(climate::ClimateAction action_if_operating) {
  // Determine if stage indicates activity (for fallback logic)
  bool stage_is_active = this->use_stage_for_operating_status_ && this->current_settings_.stage != HPStage::IDLE &&
                         this->current_settings_.stage != HPStage::UNKNOWN;

  ESP_LOGD(LOG_OPERATING_STATUS_TAG,
           "Setting action (operating: %s, stage_fallback_enabled: %s, stage: %s, stage_is_active: %s)",
           this->current_status_.operating ? "true" : "false", this->use_stage_for_operating_status_ ? "yes" : "no",
           hp_stage_to_str(this->current_settings_.stage), stage_is_active ? "yes" : "no");

  // True fallback logic: operating OR (fallback enabled AND stage is active)
  // This handles cases like 2-stage heating where compressor may be off but gas heating is active
  if (this->current_status_.operating) {
    // Primary: compressor is running
    this->action = action_if_operating;
    ESP_LOGD(LOG_OPERATING_STATUS_TAG, "Action set by operating status (compressor running)");
  } else if (stage_is_active) {
    // Fallback: compressor not running but stage indicates activity (e.g., gas heating)
    this->action = action_if_operating;
    ESP_LOGD(LOG_OPERATING_STATUS_TAG, "Action set by stage fallback (stage: %s)",
             hp_stage_to_str(this->current_settings_.stage));
  } else {
    // Neither operating nor stage indicates activity
    this->action = climate::CLIMATE_ACTION_IDLE;
    ESP_LOGD(LOG_OPERATING_STATUS_TAG, "Action set to IDLE (no activity detected)");
  }
}

void CN105Climate::update_action() {
  ESP_LOGV(TAG, "updating action back to espHome...");
  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT:
      this->set_action_if_operating_to(climate::CLIMATE_ACTION_HEATING);
      break;
    case climate::CLIMATE_MODE_COOL:
      this->set_action_if_operating_to(climate::CLIMATE_ACTION_COOLING);
      break;

    case climate::CLIMATE_MODE_DRY:
      this->set_action_if_operating_to(climate::CLIMATE_ACTION_DRYING);
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      this->action = climate::CLIMATE_ACTION_FAN;
      break;
    default:
      this->action = climate::CLIMATE_ACTION_OFF;
  }

  ESP_LOGD(TAG, "Climate mode is: %i", this->mode);
  ESP_LOGD(TAG, "Climate action is: %i", this->action);
}

climate::ClimateTraits CN105Climate::traits() {
  // ESP_LOGD(LOG_SETTINGS_TAG, "traits() called (dual: %d)", traits_.get_supports_two_point_target_temperature());
  return traits_;
}

/**
 * Modify our supported traits.
 *
 * Returns:
 *   A reference to this class' supported climate::ClimateTraits.
 */
climate::ClimateTraits &CN105Climate::config_traits() { return traits_; }

void CN105Climate::set_mode_setting(const char *setting) { wanted_settings_.mode = hp_mode_from_str(setting); }

void CN105Climate::set_power_setting(const char *setting) { wanted_settings_.power = hp_power_from_str(setting); }

void CN105Climate::set_fan_speed(const char *setting) { wanted_settings_.fan = hp_fan_from_str(setting); }

void CN105Climate::set_vane_setting(const char *setting) { wanted_settings_.vane = hp_vane_from_str(setting); }

void CN105Climate::set_wide_vane_setting(const char *setting) {
  wanted_settings_.wide_vane = hp_wide_vane_from_str(setting);
}

void CN105Climate::set_airflow_control_setting(const char *setting) {
  wanted_run_states_.airflow_control = hp_airflow_control_from_str(setting);
}

void CN105Climate::set_remote_temperature(float setting) {
  if (std::isnan(setting)) {
    ESP_LOGW(LOG_REMOTE_TEMP, "Remote temperature is NaN, ignoring.");
    return;
  }

  // 0 is the documented HA/YAML-facing sentinel meaning "revert to the unit's internal
  // sensor". Internally a remote temperature is an optional, so translate it here.
  if (setting == 0.0f) {
    this->clear_remote_temperature();
    return;
  }

  // Update the internal target value
  this->remote_temperature_ = setting;

  // Reset the watchdog timeout (HA sent us a fresh value)
  this->ping_external_temperature();

  // Start keep-alive if not already running (periodic re-send like Kumo does)
  this->start_remote_temp_keep_alive();

  // Check if the 0.5°C precision byte has actually changed since the last value we sent.
  // We only skip if the byte is identical AND we have actually sent a temperature before
  // (last_remote_temp_send_ms_ > 0).
  uint8_t new_byte = cn105_protocol::encode_temperature_b(setting);
  bool byte_unchanged = this->last_remote_temp_sent_.has_value() &&
                        cn105_protocol::encode_temperature_b(*this->last_remote_temp_sent_) == new_byte;
  if (byte_unchanged && this->last_remote_temp_send_ms_ > 0) {
    ESP_LOGD(LOG_REMOTE_TEMP, "Remote temp byte unchanged (%02X). Skipping immediate write.", new_byte);
    // The unit already holds this value; drop any pending deferred write for an earlier sample.
    this->cancel_timeout("deferred_remote_temp_send");
    return;
  }

  uint32_t now = CUSTOM_MILLIS;
  uint32_t elapsed = now - this->last_remote_temp_send_ms_;

  if (this->last_remote_temp_send_ms_ == 0 || elapsed >= REMOTE_TEMP_MIN_SEND_INTERVAL_MS) {
    // Send immediately
    this->cancel_timeout("deferred_remote_temp_send");
    this->queue_remote_temperature_send_();
    ESP_LOGD(LOG_REMOTE_TEMP, "Queueing immediate remote temp write (value: %.1f)", setting);
  } else {
    // Defer the write
    uint32_t defer_ms = REMOTE_TEMP_MIN_SEND_INTERVAL_MS - elapsed;
    ESP_LOGD(LOG_REMOTE_TEMP, "Rate limit: deferring remote temp write (%.1f) by %lu ms", setting,
             (unsigned long) defer_ms);
    this->set_timeout("deferred_remote_temp_send", defer_ms, [this]() { this->send_remote_temperature_deferred(); });
  }
}

void CN105Climate::clear_remote_temperature() {
  // Revert to the unit's internal sensor: no remote temperature is held anymore.
  this->remote_temperature_.reset();
  // Disarm rather than re-arm the watchdog. Re-arming here would make the timeout
  // callback (which calls this function) reschedule itself forever, re-sending the
  // revert packet once per timeout period for the lifetime of the device.
  this->cancel_timeout(SCHEDULER_REMOTE_TEMP_TIMEOUT);
  this->stop_remote_temp_keep_alive();
  // Send the revert packet immediately (drop any pending deferred write first).
  this->cancel_timeout("deferred_remote_temp_send");
  this->queue_remote_temperature_send_();
}

void CN105Climate::queue_remote_temperature_send_() {
  // Record when the write was first queued so the loop can notice if the cycle
  // scheduler never gets around to writing it (see send_pending_remote_temperature_).
  if (!this->should_send_external_temperature_) {
    this->remote_temp_pending_since_ms_ = CUSTOM_MILLIS;
  }
  this->should_send_external_temperature_ = true;
}

void CN105Climate::send_remote_temperature_deferred() {
  // Queue the send via the same flag used by user updates and keep-alive, so the
  // packet is written at the end of an info cycle (terminate_cycle) rather than
  // mid-cycle from a timer callback. This preserves bus serialization and means
  // the update is not lost if the heatpump is momentarily disconnected when the
  // timer fires (it will be sent on the next completed cycle).
  ESP_LOGD(LOG_REMOTE_TEMP, "Deferred remote temp timer fired, queueing send of %.1f",
           this->remote_temperature_.value_or(NAN));
  this->queue_remote_temperature_send_();
}

void CN105Climate::evaluate_fan_stop_and_ltp() {
  cn105_policy::ReconcileInputs in;
  in.now_ms = CUSTOM_MILLIS;
  in.update_interval_ms = this->update_interval_;

  in.desired_power_on = this->desired_mode_ != climate::CLIMATE_MODE_OFF;
  in.desired_mode = in.desired_power_on ? climate_mode_to_hp_mode(this->desired_mode_) : std::nullopt;
  in.desired_temp = this->desired_temp_;

  in.current_power = this->current_settings_.power;
  in.current_mode = this->current_settings_.mode;
  in.current_temperature = this->current_settings_.temperature;
  in.current_temp = this->current_temperature;

  in.ltp_enabled = this->low_temp_protection_switch_ != nullptr && this->low_temp_protection_switch_->state;
  in.ltp_active = this->ltp_active_;
  in.low_temp_temp = this->low_temp_temp_;
  in.low_temp_hysteresis = this->low_temp_hysteresis_;

  in.fan_stop_enabled = this->fan_stop_switch_ != nullptr && this->fan_stop_switch_->state;
  in.hysteresis = this->hysteresis_;

  in.has_issued_command = this->has_issued_command_;
  in.last_issued = this->last_issued_command_target_;
  in.last_issued_ms = this->last_issued_command_ms_;

  const cn105_policy::ReconcileDecision decision = cn105_policy::decide_reconciliation(in);

  if (decision.ltp_active != this->ltp_active_) {
    if (decision.ltp_active) {
      ESP_LOGI("cn105", "Low temperature protection activated (current temp: %.1f°C)", in.current_temp);
    } else if (!in.ltp_enabled) {
      ESP_LOGI("cn105", "Low temperature protection disabled by switch");
    } else {
      ESP_LOGI("cn105", "Low temperature protection deactivated (current temp: %.1f°C)", in.current_temp);
    }
    this->ltp_active_ = decision.ltp_active;
  }

  // The policy works in heatpump terms; map back for the Home-Assistant-facing
  // bookkeeping. Only three outcomes are reachable: off, LTP forcing heat, or the
  // user's own mode (fan stop switches the unit off, it never changes the mode).
  climate::ClimateMode target_ha_mode = climate::CLIMATE_MODE_OFF;
  if (decision.target.power_on) {
    target_ha_mode = decision.ltp_active ? climate::CLIMATE_MODE_HEAT : this->desired_mode_;
  }

  if (decision.has_mismatch() && decision.suppressed_repeat) {
    ESP_LOGD("cn105",
             "evaluate_fan_stop_and_ltp: mismatch on an already-commanded target (%s / %.1f), waiting for readback",
             LOG_STR_ARG(climate::climate_mode_to_string(target_ha_mode)), decision.target.temperature);
  } else if (decision.should_command) {
    ESP_LOGI("cn105",
             "evaluate_fan_stop_and_ltp: mismatch detected. target_physical_mode: %s, target_physical_temp: %.1f",
             LOG_STR_ARG(climate::climate_mode_to_string(target_ha_mode)), decision.target.temperature);

    this->has_issued_command_ = true;
    this->last_issued_command_target_ = decision.target;
    this->last_issued_command_ms_ = in.now_ms;

    this->last_mode_command_time_ms_ = in.now_ms;
    this->last_commanded_real_mode_ = target_ha_mode;
    this->last_commanded_real_temp_ = decision.target.temperature;

    if (!decision.target.power_on) {
      this->set_power_setting("OFF");
    } else {
      this->set_power_setting("ON");
      if (decision.target.mode.has_value()) {
        this->wanted_settings_.mode = *decision.target.mode;
      } else {
        ESP_LOGW("cn105", "No heatpump mode maps to %s; leaving the unit's mode unchanged",
                 LOG_STR_ARG(climate::climate_mode_to_string(target_ha_mode)));
      }
      this->wanted_settings_.temperature = this->calculate_temperature_setting(decision.target.temperature);
    }

    this->wanted_settings_.has_changed = true;
    this->wanted_settings_.has_been_sent = false;
    this->wanted_settings_.last_change = in.now_ms;
  }

  // Diagnostic status text sensor
  if (this->diagnostic_sensor_ != nullptr) {
    const char *status = cn105_policy::protection_state_to_str(decision.protection);
    if (this->diagnostic_sensor_->state != status) {
      this->diagnostic_sensor_->publish_state(status);
    }
  }
}
