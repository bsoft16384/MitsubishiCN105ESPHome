#include "cn105.h"
#include "Globals.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include <utility>

using namespace esphome;


void CN105Climate::checkPendingWantedSettings() {
    // Already in-flight — don't log or re-send
    if (this->wantedSettings.hasBeenSent) {
        return;
    }

    long now = CUSTOM_MILLIS;
    if (!(this->wantedSettings.hasChanged) || (now - this->wantedSettings.lastChange < this->debounce_delay_)) {
        return;
    }

    // Don't log if sendWantedSettings() will defer due to write throttle (300ms)
    if (now - this->lastSend <= 300) {
        return;
    }

    ESP_LOGI(LOG_ACTION_EVT_TAG, "checkPendingWantedSettings - wanted settings have changed, sending them to the heatpump...");
    this->sendWantedSettings();
}

void CN105Climate::checkPendingWantedRunStates() {
    // Already in-flight — don't log or re-send
    if (this->wantedRunStates.hasBeenSent) {
        return;
    }

    long now = CUSTOM_MILLIS;
    if (!(this->wantedRunStates.hasChanged) || (now - this->wantedRunStates.lastChange < this->debounce_delay_)) {
        return;
    }

    // Don't log if sendWantedRunStates() will defer due to write throttle (300ms)
    if (now - this->lastSend <= 300) {
        return;
    }

    ESP_LOGI(LOG_ACTION_EVT_TAG, "checkPendingWantedRunStates - wanted run states have changed, sending them to the heatpump...");
    this->sendWantedRunStates();
}

void logCheckWantedSettingsMutex(wantedHeatpumpSettings& settings) {

    if (settings.hasBeenSent) {
        ESP_LOGE("control", "Mutex lock faillure: wantedSettings should be locked while sending.");
        ESP_LOGD("control", "-- This is an assertion test on wantedSettings.hasBeenSent");
        ESP_LOGD("control", "-- wantedSettings.hasBeenSent = true is unexpected");
        ESP_LOGD("control", "-- should be false because mutex should prevent running this while sending");
        ESP_LOGD("control", "-- and mutex should be released only when hasBeenSent is false");
    }

}
void CN105Climate::controlDelegate(const esphome::climate::ClimateCall& call) {
    ESP_LOGD("control", "espHome control() interface method called...");
    bool updated = false;

    logCheckWantedSettingsMutex(this->wantedSettings);

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

    updated = this->processFanChange(call) || updated;
    updated = this->processSwingChange(call) || updated;

    this->finalizeControlIfUpdated(updated);
}

bool CN105Climate::processModeChange(const esphome::climate::ClimateCall& call) {
    if (!call.get_mode().has_value()) {
        return false;
    }

    ESP_LOGD("control", "Mode change asked");
    this->mode = *call.get_mode();
    this->controlMode();
    this->controlTemperature();
    return true;
}



bool CN105Climate::processTemperatureChange(const esphome::climate::ClimateCall& call) {
    if (!call.get_target_temperature().has_value()) {
        return false;
    }
    float temp_single = this->fahrenheitSupport_.normalizeUiTemperatureToHeatpumpTemperature(*call.get_target_temperature());
    this->setTargetTemperature(temp_single);
    ESP_LOGI("control", "Setting heatpump setpoint : %.1f", this->getTargetTemperature());

    this->controlTemperature();
    ESP_LOGD("control", "controlled temperature to: %.1f", this->wantedSettings.temperature);
    return true;
}

bool CN105Climate::processFanChange(const esphome::climate::ClimateCall& call) {
    if (!call.get_fan_mode().has_value()) {
        return false;
    }
    ESP_LOGD("control", "Fan change asked");
    this->fan_mode = *call.get_fan_mode();
    this->controlFan();
    return true;
}

bool CN105Climate::processSwingChange(const esphome::climate::ClimateCall& call) {
    if (!call.get_swing_mode().has_value()) {
        return false;
    }
    ESP_LOGD("control", "Swing change asked");
    this->swing_mode = *call.get_swing_mode();
    this->controlSwing();
    return true;
}

void CN105Climate::finalizeControlIfUpdated(bool updated) {
    if (!updated) {
        return;
    }
    ESP_LOGD(LOG_ACTION_EVT_TAG, "clim.control() -> User changed something...");
    logCheckWantedSettingsMutex(this->wantedSettings);
    this->wantedSettings.hasChanged = true;
    this->wantedSettings.hasBeenSent = false;
    this->wantedSettings.lastChange = CUSTOM_MILLIS;
    this->debugSettings("control (wantedSettings)", this->wantedSettings);
    this->publish_state();
}

void CN105Climate::control(const esphome::climate::ClimateCall& call) {

    std::lock_guard<std::mutex> guard(wantedSettingsMutex);
    this->controlDelegate(call);

}


/**
 * @brief Controls the swing modes based on user selection.
 *
 * This function handles the logic for CLIMATE_SWING_OFF, VERTICAL, HORIZONTAL, and BOTH.
 * It is designed to be safe for units that do not support horizontal swing (wideVane)
 * and provides an intuitive user experience by preserving static vane settings when possible.
 */
void CN105Climate::controlSwing() {
    // Check if horizontal vane (wideVane) is supported by this unit at the beginning.
    bool wideVaneSupported = this->traits_.supports_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);
    bool vane_is_swing = this->currentSettings.vane == HPVaneMode::SWING;
    bool wide_is_swing = this->currentSettings.wideVane == HPWideVaneMode::SWING;

    switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:
        // When swing is turned OFF, conditionally set vanes to a default static position.
        // This only sets default position if swing was previously enabled
        if (vane_is_swing) {
            this->setVaneSetting("AUTO");
        }
        if (wideVaneSupported && wide_is_swing) {
            this->setWideVaneSetting("|");
        }
        break;

    case climate::CLIMATE_SWING_VERTICAL:
        // Turn on vertical swing.
        this->setVaneSetting("SWING");
        // If horizontal swing was also on AND is supported, turn it off to a default static position.
        // This correctly handles switching from BOTH to VERTICAL, while preserving any user's
        // static horizontal setting if it wasn't swinging.
        if (wideVaneSupported && wide_is_swing) {
            this->setWideVaneSetting("|");
        }
        break;

    case climate::CLIMATE_SWING_HORIZONTAL:
        // If vertical swing was on, turn it off to a default static position.
        // This correctly handles switching from BOTH to HORIZONTAL, while preserving any user's
        // static vertical setting if it wasn't swinging.
        if (vane_is_swing) {
            this->setVaneSetting("AUTO");
        }
        // Turn on horizontal swing, but only if the unit supports it.
        if (wideVaneSupported) {
            this->setWideVaneSetting("SWING");
        }
        break;

    case climate::CLIMATE_SWING_BOTH:
        // Turn on vertical swing.
        this->setVaneSetting("SWING");
        // Turn on horizontal swing, but only if the unit supports it.
        if (wideVaneSupported) {
            this->setWideVaneSetting("SWING");
        }
        break;

    default:
        ESP_LOGW(TAG, "control - received unsupported swing mode request.");
        break;
    }
}
void CN105Climate::controlFan() {

    switch (this->fan_mode.value()) {
    case climate::CLIMATE_FAN_OFF:
        this->setPowerSetting("OFF");
        break;
    case climate::CLIMATE_FAN_QUIET:
        this->setFanSpeed("QUIET");
        break;
    case climate::CLIMATE_FAN_DIFFUSE:
        this->setFanSpeed("QUIET");
        break;
    case climate::CLIMATE_FAN_LOW:
        this->setFanSpeed("1");
        break;
    case climate::CLIMATE_FAN_MEDIUM:
        this->setFanSpeed("2");
        break;
    case climate::CLIMATE_FAN_MIDDLE:
        this->setFanSpeed("3");
        break;
    case climate::CLIMATE_FAN_HIGH:
        this->setFanSpeed("4");
        break;
    case climate::CLIMATE_FAN_ON:
    case climate::CLIMATE_FAN_AUTO:
    default:
        this->setFanSpeed("AUTO");
        break;
    }
}


void CN105Climate::controlTemperature() {
    float setting = this->getTargetTemperature();
    setting = this->calculateTemperatureSetting(setting);
    this->wantedSettings.temperature = setting;
    ESP_LOGI("control", "setting wanted temperature to %.1f", setting);
}



void CN105Climate::controlMode() {

    switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
        ESP_LOGI("control", "changing mode to COOL");
        this->setModeSetting("COOL");
        this->setPowerSetting("ON");
        break;
    case climate::CLIMATE_MODE_HEAT:
        ESP_LOGI("control", "changing mode to HEAT");
        this->setModeSetting("HEAT");
        this->setPowerSetting("ON");

        break;
    case climate::CLIMATE_MODE_DRY:
        ESP_LOGI("control", "changing mode to DRY");
        this->setModeSetting("DRY");
        this->setPowerSetting("ON");

        break;




    case climate::CLIMATE_MODE_FAN_ONLY:
        ESP_LOGI("control", "changing mode to FAN_ONLY");
        this->setModeSetting("FAN");
        this->setPowerSetting("ON");
        break;
    case climate::CLIMATE_MODE_OFF:
        ESP_LOGI("control", "changing mode to OFF");
        this->setPowerSetting("OFF");
        break;
    default:
        ESP_LOGW("control", "unsupported mode");
    }
}


void CN105Climate::setActionIfOperatingTo(climate::ClimateAction action_if_operating) {

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

void CN105Climate::updateAction() {
    ESP_LOGV(TAG, "updating action back to espHome...");
    switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT:
        this->setActionIfOperatingTo(climate::CLIMATE_ACTION_HEATING);
        break;
    case climate::CLIMATE_MODE_COOL:
        this->setActionIfOperatingTo(climate::CLIMATE_ACTION_COOLING);
        break;




    case climate::CLIMATE_MODE_DRY:
        this->setActionIfOperatingTo(climate::CLIMATE_ACTION_DRYING);
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


void CN105Climate::setModeSetting(const char* setting) {
    wantedSettings.mode = hp_mode_from_str(setting);
}

void CN105Climate::setPowerSetting(const char* setting) {
    wantedSettings.power = hp_power_from_str(setting);
}

void CN105Climate::setFanSpeed(const char* setting) {
    wantedSettings.fan = hp_fan_from_str(setting);
}

void CN105Climate::setVaneSetting(const char* setting) {
    wantedSettings.vane = hp_vane_from_str(setting);
}

void CN105Climate::setWideVaneSetting(const char* setting) {
    wantedSettings.wideVane = hp_wide_vane_from_str(setting);
}

void CN105Climate::setAirflowControlSetting(const char* setting) {
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
    this->pingExternalTemperature();

    // Manage keep-alive timer based on temperature value
    if (setting > 0) {
        // Start keep-alive if not already running (periodic re-send like Kumo does)
        this->startRemoteTempKeepAlive();
    } else {
        // Stop keep-alive when reverting to internal sensor
        this->stopRemoteTempKeepAlive();
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
        float normalized_target_temp = this->calculateTemperatureSetting(target_physical_temp);
        if (std::isnan(this->currentSettings.temperature) || fabsf(this->currentSettings.temperature - normalized_target_temp) >= 0.25f) {
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
            this->setPowerSetting("OFF");
        } else {
            this->setPowerSetting("ON");
            const char* target_mode_str = "AUTO";
            if (target_physical_mode == climate::CLIMATE_MODE_HEAT) target_mode_str = "HEAT";
            else if (target_physical_mode == climate::CLIMATE_MODE_COOL) target_mode_str = "COOL";
            else if (target_physical_mode == climate::CLIMATE_MODE_DRY) target_mode_str = "DRY";
            else if (target_physical_mode == climate::CLIMATE_MODE_FAN_ONLY) target_mode_str = "FAN";
            this->setModeSetting(target_mode_str);

            float setting = this->calculateTemperatureSetting(target_physical_temp);
            this->wantedSettings.temperature = setting;
        }

        this->wantedSettings.hasChanged = true;
        this->wantedSettings.hasBeenSent = false;
        this->wantedSettings.lastChange = CUSTOM_MILLIS;
    }

    // 3. Update diagnostic status text sensor
    if (this->diagnostic_sensor_ != nullptr) {
        std::string status = "Normal";
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
