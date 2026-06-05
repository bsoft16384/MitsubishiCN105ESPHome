#include "cn105.h"
#include "Globals.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include <utility>

using namespace esphome;


void CN105Climate::check_pending_wanted_settings() {
    // Already in-flight — don't log or re-send
    if (this->wantedSettings.hasBeenSent) {
        return;
    }

    long now = CUSTOM_MILLIS;
    if (!(this->wantedSettings.hasChanged) || (now - this->wantedSettings.lastChange < this->debounce_delay_)) {
        return;
    }

    // Don't log if send_wanted_settings() will defer due to write throttle (300ms)
    if (now - this->lastSend <= 300) {
        return;
    }

    ESP_LOGI(LOG_ACTION_EVT_TAG, "checkPendingWantedSettings - wanted settings have changed, sending them to the heatpump...");
    this->send_wanted_settings();
}

void CN105Climate::check_pending_wanted_run_states() {
    // Already in-flight — don't log or re-send
    if (this->wantedRunStates.hasBeenSent) {
        return;
    }

    long now = CUSTOM_MILLIS;
    if (!(this->wantedRunStates.hasChanged) || (now - this->wantedRunStates.lastChange < this->debounce_delay_)) {
        return;
    }

    // Don't log if send_wanted_run_states() will defer due to write throttle (300ms)
    if (now - this->lastSend <= 300) {
        return;
    }

    ESP_LOGI(LOG_ACTION_EVT_TAG, "checkPendingWantedRunStates - wanted run states have changed, sending them to the heatpump...");
    this->send_wanted_run_states();
}
void CN105Climate::control_delegate(const esphome::climate::ClimateCall& call) {
    ESP_LOGD("control", "espHome control() interface method called...");
    bool updated = false;

    if (call.get_mode().has_value()) {
        this->desired_mode_ = *call.get_mode();
        this->mode = this->desired_mode_;
        if (this->desired_mode_ != climate::CLIMATE_MODE_OFF && std::isnan(this->desired_temp_)) {
            this->desired_temp_ = 21.0f; // Safe default fallback
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

bool CN105Climate::process_mode_change(const esphome::climate::ClimateCall& call) {
    if (!call.get_mode().has_value()) {
        return false;
    }

    ESP_LOGD("control", "Mode change asked");
    this->mode = *call.get_mode();
    this->control_mode();
    this->control_temperature();
    return true;
}



bool CN105Climate::process_temperature_change(const esphome::climate::ClimateCall& call) {
    if (!call.get_target_temperature().has_value()) {
        return false;
    }
    float temp_single = *call.get_target_temperature();
    this->set_target_temperature(temp_single);
    ESP_LOGI("control", "Setting heatpump setpoint : %.1f", this->get_target_temperature());

    this->control_temperature();
    ESP_LOGD("control", "controlled temperature to: %.1f", this->wantedSettings.temperature.value_or(0.0f));
    return true;
}

bool CN105Climate::process_fan_change(const esphome::climate::ClimateCall& call) {
    if (!call.get_fan_mode().has_value()) {
        return false;
    }
    ESP_LOGD("control", "Fan change asked");
    this->fan_mode = *call.get_fan_mode();
    this->control_fan();
    return true;
}

bool CN105Climate::process_swing_change(const esphome::climate::ClimateCall& call) {
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
    this->wantedSettings.hasChanged = true;
    this->wantedSettings.hasBeenSent = false;
    this->wantedSettings.lastChange = CUSTOM_MILLIS;
    this->debug_settings("control (wantedSettings)", this->wantedSettings);
    this->publish_state();
}

void CN105Climate::control(const esphome::climate::ClimateCall& call) {
    this->set_timeout("control_deferred", 0, [this, call]() {
        this->control_delegate(call);
    });
}


/**
 * @brief Controls the swing modes based on user selection.
 *
 * This function handles the logic for CLIMATE_SWING_OFF, VERTICAL, HORIZONTAL, and BOTH.
 * It is designed to be safe for units that do not support horizontal swing (wideVane)
 * and provides an intuitive user experience by preserving static vane settings when possible.
 */
void CN105Climate::control_swing() {
    // Check if horizontal vane (wideVane) is supported by this unit at the beginning.
    bool wideVaneSupported = this->traits_.supports_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);
    bool vane_is_swing = this->currentSettings.vane == HPVaneMode::SWING;
    bool wide_is_swing = this->currentSettings.wideVane == HPWideVaneMode::SWING;

    switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:
        // When swing is turned OFF, conditionally set vanes to a default static position.
        // This only sets default position if swing was previously enabled
        if (vane_is_swing) {
            this->set_vane_setting("AUTO");
        }
        if (wideVaneSupported && wide_is_swing) {
            this->set_wide_vane_setting("|");
        }
        break;

    case climate::CLIMATE_SWING_VERTICAL:
        // Turn on vertical swing.
        this->set_vane_setting("SWING");
        // If horizontal swing was also on AND is supported, turn it off to a default static position.
        // This correctly handles switching from BOTH to VERTICAL, while preserving any user's
        // static horizontal setting if it wasn't swinging.
        if (wideVaneSupported && wide_is_swing) {
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
        if (wideVaneSupported) {
            this->set_wide_vane_setting("SWING");
        }
        break;

    case climate::CLIMATE_SWING_BOTH:
        // Turn on vertical swing.
        this->set_vane_setting("SWING");
        // Turn on horizontal swing, but only if the unit supports it.
        if (wideVaneSupported) {
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


void CN105Climate::control_temperature() {
    float setting = this->get_target_temperature();
    setting = this->calculate_temperature_setting(setting);
    this->wantedSettings.temperature = setting;
    ESP_LOGI("control", "setting wanted temperature to %.1f", setting);
}



void CN105Climate::control_mode() {

    switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
        ESP_LOGI("control", "changing mode to COOL");
        this->set_mode_setting("COOL");
        this->set_power_setting("ON");
        break;
    case climate::CLIMATE_MODE_HEAT:
        ESP_LOGI("control", "changing mode to HEAT");
        this->set_mode_setting("HEAT");
        this->set_power_setting("ON");

        break;
    case climate::CLIMATE_MODE_DRY:
        ESP_LOGI("control", "changing mode to DRY");
        this->set_mode_setting("DRY");
        this->set_power_setting("ON");

        break;




    case climate::CLIMATE_MODE_FAN_ONLY:
        ESP_LOGI("control", "changing mode to FAN_ONLY");
        this->set_mode_setting("FAN");
        this->set_power_setting("ON");
        break;
    case climate::CLIMATE_MODE_OFF:
        ESP_LOGI("control", "changing mode to OFF");
        this->set_power_setting("OFF");
        break;
    default:
        ESP_LOGW("control", "unsupported mode");
    }
}


void CN105Climate::set_action_if_operating_to(climate::ClimateAction action_if_operating) {

    // Determine if stage indicates activity (for fallback logic)
    bool stage_is_active = this->use_stage_for_operating_status_ &&
        this->currentSettings.stage != HPStage::IDLE &&
        this->currentSettings.stage != HPStage::UNKNOWN;

    ESP_LOGD(LOG_OPERATING_STATUS_TAG, "Setting action (operating: %s, stage_fallback_enabled: %s, stage: %s, stage_is_active: %s)",
        this->currentStatus.operating ? "true" : "false",
        this->use_stage_for_operating_status_ ? "yes" : "no",
        hp_stage_to_str(this->currentSettings.stage),
        stage_is_active ? "yes" : "no");

    // True fallback logic: operating OR (fallback enabled AND stage is active)
    // This handles cases like 2-stage heating where compressor may be off but gas heating is active
    if (this->currentStatus.operating) {
        // Primary: compressor is running
        this->action = action_if_operating;
        ESP_LOGD(LOG_OPERATING_STATUS_TAG, "Action set by operating status (compressor running)");
    } else if (stage_is_active) {
        // Fallback: compressor not running but stage indicates activity (e.g., gas heating)
        this->action = action_if_operating;
        ESP_LOGD(LOG_OPERATING_STATUS_TAG, "Action set by stage fallback (stage: %s)", hp_stage_to_str(this->currentSettings.stage));
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
    //ESP_LOGD(LOG_SETTINGS_TAG, "traits() called (dual: %d)", traits_.get_supports_two_point_target_temperature());
    return traits_;
}


/**
 * Modify our supported traits.
 *
 * Returns:
 *   A reference to this class' supported climate::ClimateTraits.
 */
climate::ClimateTraits& CN105Climate::config_traits() {
    return traits_;
}


void CN105Climate::set_mode_setting(const char* setting) {
    wantedSettings.mode = hp_mode_from_str(setting);
}

void CN105Climate::set_power_setting(const char* setting) {
    wantedSettings.power = hp_power_from_str(setting);
}

void CN105Climate::set_fan_speed(const char* setting) {
    wantedSettings.fan = hp_fan_from_str(setting);
}

void CN105Climate::set_vane_setting(const char* setting) {
    wantedSettings.vane = hp_vane_from_str(setting);
}

void CN105Climate::set_wide_vane_setting(const char* setting) {
    wantedSettings.wideVane = hp_wide_vane_from_str(setting);
}

void CN105Climate::set_airflow_control_setting(const char* setting) {
    wantedRunStates.airflow_control = hp_airflow_control_from_str(setting);
}

void CN105Climate::set_remote_temperature(float setting) {
    if (std::isnan(setting)) {
        ESP_LOGW(LOG_REMOTE_TEMP, "Remote temperature is NaN, ignoring.");
        return;
    }

    // Toujours renvoyer la température distante lorsqu’un nouvel échantillon arrive,
    // même si la valeur n’a pas changé, afin d’éviter que l’unité Mitsubishi
    // ne repasse sur la sonde interne faute de mise à jour régulière (#474).
    this->remoteTemperature_ = setting;
    this->shouldSendExternalTemperature_ = true;
    ESP_LOGD(LOG_REMOTE_TEMP, "setting remote temperature to %f", this->remoteTemperature_);

    // Reset the watchdog timeout (HA sent us a fresh value)
    this->ping_external_temperature();

    // Manage keep-alive timer based on temperature value
    if (setting > 0) {
        // Start keep-alive if not already running (periodic re-send like Kumo does)
        this->start_remote_temp_keep_alive();
    } else {
        // Stop keep-alive when reverting to internal sensor
        this->stop_remote_temp_keep_alive();
    }
}


void CN105Climate::evaluate_fan_stop_and_ltp() {
    float current_temp = this->current_temperature;

    // 1. Handle Low Temperature Protection (LTP) state machine
    if (this->low_temp_protection_switch_ != nullptr && this->low_temp_protection_switch_->state) {
        if (!this->ltp_active_ && !std::isnan(current_temp) && current_temp < this->low_temp_temp_) {
            this->ltp_active_ = true;
            ESP_LOGI("cn105", "Low temperature protection activated (current temp: %.1f°C)", current_temp);
        }
        if (this->ltp_active_ && !std::isnan(current_temp) && current_temp > (this->low_temp_temp_ + this->low_temp_hysteresis_)) {
            this->ltp_active_ = false;
            ESP_LOGI("cn105", "Low temperature protection deactivated (current temp: %.1f°C)", current_temp);
        }
    } else {
        if (this->ltp_active_) {
            this->ltp_active_ = false;
            ESP_LOGI("cn105", "Low temperature protection disabled by switch");
        }
    }

    climate::ClimateMode target_physical_mode = this->desired_mode_;
    float target_physical_temp = this->desired_temp_;

    if (this->ltp_active_) {
        // LTP has priority. Force heat mode and target temperature of max(low_temp_temp + low_temp_hysteresis, desired_temp_)
        target_physical_mode = climate::CLIMATE_MODE_HEAT;
        target_physical_temp = std::max(this->low_temp_temp_ + this->low_temp_hysteresis_, this->desired_temp_);
    } else if (this->fan_stop_switch_ != nullptr && this->fan_stop_switch_->state && !std::isnan(current_temp) && !std::isnan(this->desired_temp_)) {
        // Fan stop mode logic
        if (this->desired_mode_ == climate::CLIMATE_MODE_HEAT) {
            if (current_temp >= this->desired_temp_ + this->hysteresis_) {
                target_physical_mode = climate::CLIMATE_MODE_OFF;
            } else if (current_temp <= this->desired_temp_ - this->hysteresis_) {
                target_physical_mode = climate::CLIMATE_MODE_HEAT;
            } else {
                // Inside hysteresis deadband, maintain current physical state
                if (this->currentSettings.power == HPPower::OFF) {
                    target_physical_mode = climate::CLIMATE_MODE_OFF;
                } else {
                    target_physical_mode = climate::CLIMATE_MODE_HEAT;
                }
            }
        } else if (this->desired_mode_ == climate::CLIMATE_MODE_COOL) {
            if (current_temp <= this->desired_temp_ - this->hysteresis_) {
                target_physical_mode = climate::CLIMATE_MODE_OFF;
            } else if (current_temp >= this->desired_temp_ + this->hysteresis_) {
                target_physical_mode = climate::CLIMATE_MODE_COOL;
            } else {
                // Inside hysteresis deadband, maintain current physical state
                if (this->currentSettings.power == HPPower::OFF) {
                    target_physical_mode = climate::CLIMATE_MODE_OFF;
                } else {
                    target_physical_mode = climate::CLIMATE_MODE_COOL;
                }
            }
        }
    }
 
    // 2. Perform command if mismatch exists
    bool mode_mismatch = false;
    bool temp_mismatch = false;
 
    // Check power/mode mismatch
    if (target_physical_mode == climate::CLIMATE_MODE_OFF) {
        if (this->currentSettings.power != HPPower::OFF) {
            mode_mismatch = true;
        }
    } else {
        if (this->currentSettings.power != HPPower::ON) {
            mode_mismatch = true;
        }
        HPMode target_mode_enum = HPMode::AUTO;
        if (target_physical_mode == climate::CLIMATE_MODE_HEAT) target_mode_enum = HPMode::HEAT;
        else if (target_physical_mode == climate::CLIMATE_MODE_COOL) target_mode_enum = HPMode::COOL;
        else if (target_physical_mode == climate::CLIMATE_MODE_DRY) target_mode_enum = HPMode::DRY;
        else if (target_physical_mode == climate::CLIMATE_MODE_FAN_ONLY) target_mode_enum = HPMode::FAN;
        
        if (this->currentSettings.mode != target_mode_enum) {
            mode_mismatch = true;
        }
    }

    // Check temperature mismatch
    if (target_physical_mode != climate::CLIMATE_MODE_OFF) {
        float normalized_target_temp = this->calculate_temperature_setting(target_physical_temp);
        if (!this->currentSettings.temperature.has_value() || fabsf(*this->currentSettings.temperature - normalized_target_temp) >= 0.25f) {
            temp_mismatch = true;
        }
    }

    if (mode_mismatch || temp_mismatch) {
        ESP_LOGI("cn105", "evaluate_fan_stop_and_ltp: mismatch detected. target_physical_mode: %s, target_physical_temp: %.1f", 
                 climate::climate_mode_to_string(target_physical_mode), target_physical_temp);

        this->last_mode_command_time_ms_ = CUSTOM_MILLIS;
        this->last_commanded_real_mode_ = target_physical_mode;
        this->last_commanded_real_temp_ = target_physical_temp;

        if (target_physical_mode == climate::CLIMATE_MODE_OFF) {
            this->set_power_setting("OFF");
        } else {
            this->set_power_setting("ON");
            const char* target_mode_str = "AUTO";
            if (target_physical_mode == climate::CLIMATE_MODE_HEAT) target_mode_str = "HEAT";
            else if (target_physical_mode == climate::CLIMATE_MODE_COOL) target_mode_str = "COOL";
            else if (target_physical_mode == climate::CLIMATE_MODE_DRY) target_mode_str = "DRY";
            else if (target_physical_mode == climate::CLIMATE_MODE_FAN_ONLY) target_mode_str = "FAN";
            this->set_mode_setting(target_mode_str);

            float setting = this->calculate_temperature_setting(target_physical_temp);
            this->wantedSettings.temperature = setting;
        }

        this->wantedSettings.hasChanged = true;
        this->wantedSettings.hasBeenSent = false;
        this->wantedSettings.lastChange = CUSTOM_MILLIS;
    }

    // 3. Update diagnostic status text sensor
    if (this->diagnostic_sensor_ != nullptr) {
        const char* status = "Normal";
        if (this->ltp_active_) {
            status = "Low Temp Protection";
        } else if (this->fan_stop_switch_ != nullptr && this->fan_stop_switch_->state &&
                   this->currentSettings.power == HPPower::OFF &&
                   this->desired_mode_ != climate::CLIMATE_MODE_OFF) {
            status = "Fan Stop Active";
        }

        if (this->diagnostic_sensor_->state != status) {
            this->diagnostic_sensor_->publish_state(status);
        }
    }
}
