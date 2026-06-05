#include "cn105.h"

#include <map>

using namespace esphome;

/**
 * processInput: reads available bytes from UART and feeds them to the FrameParser.
 * When a complete frame is detected, delegates to processDataPacket().
 */


bool CN105Climate::processInput(void) {
    bool processed = false;
    while (this->get_hw_serial_()->available()) {
        processed = true;
        uint8_t inputData;
        if (this->get_hw_serial_()->read_byte(&inputData)) {
            ESP_LOGV("Decoder", "--> %02X", inputData);
            this->parser_.feed(inputData);
            if (this->parser_.frame_complete()) {
                this->processDataPacket();
                this->parser_.reset();
            }
        }
    }
    return processed;
}

/**
 * processDataPacket: called when the FrameParser has assembled a complete frame.
 * Validates checksum, sets the data pointer, and dispatches to processCommand().
 */
void CN105Climate::processDataPacket() {

    ESP_LOGV(TAG, "processing data packet...");

    // Point data at the payload section of the parser buffer
    // Note: cast away const because downstream code uses non-const data pointer
    this->data = const_cast<uint8_t*>(this->parser_.data());

    this->hpPacketDebug(this->parser_.raw(), this->parser_.frame_size(), "READ");

    // During handshake, log every received frame for diagnostics
    if (!this->isHeatpumpConnected()) {
        ESP_LOGD(LOG_CONN_TAG, "RX during handshake (cmd=0x%02X len=%d)",
            this->parser_.command(), this->parser_.data_length());
        this->hpPacketDebug(this->parser_.raw(), this->parser_.frame_size(), LOG_CONN_TAG);
    }

    if (this->parser_.checksum_valid()) {
        ESP_LOGD("chkSum", "OK");
        // checkpoint of a heatpump response
        this->lastResponseMs = CUSTOM_MILLIS;

        // processing the specific command
        processCommand();
    } else {
        ESP_LOGW("chkSum", "KO -> checksum mismatch (cmd=0x%02X len=%d)",
            this->parser_.command(), this->parser_.data_length());
        if (!this->isHeatpumpConnected()) {
            ESP_LOGD(LOG_CONN_TAG, "Checksum KO during handshake");
            this->hpPacketDebug(this->parser_.raw(), this->parser_.frame_size(), LOG_CONN_TAG);
        }
    }
}



void CN105Climate::getAutoModeStateFromResponsePacket() {
    heatpumpSettings receivedSettings{};

    if (data[10] == 0x00) {
        ESP_LOGD("Decoder", "[0x10 is 0x00]");

    } else if (data[10] == 0x01) {
        ESP_LOGD("Decoder", "[0x10 is 0x01]");

    } else if (data[10] == 0x02) {
        ESP_LOGD("Decoder", "[0x10 is 0x02]");

    } else {
        ESP_LOGD("Decoder", "[0x10 is unknown]");

    }
}

void CN105Climate::getPowerFromResponsePacket() {
    ESP_LOGD("Decoder", "[0x09 is sub modes]");

    heatpumpSettings receivedSettings{};
 
    // Use std::optional lookups — keep previous value on unknown bytes
    auto stage_opt = hp_stage_from_wire(data[4]);
    if (stage_opt) {
        receivedSettings.stage = *stage_opt;
    } else {
        ESP_LOGW("Decoder", "Unknown stage byte 0x%02X — keeping previous value", data[4]);
        receivedSettings.stage = (this->currentSettings.stage != HPStage::UNKNOWN)
            ? this->currentSettings.stage
            : HPStage::IDLE;  // default to "IDLE" when no prior value exists
    }
 
    auto sub_mode_opt = hp_sub_mode_from_wire(data[3]);
    if (sub_mode_opt) {
        receivedSettings.sub_mode = *sub_mode_opt;
    } else {
        ESP_LOGW("Decoder", "Unknown sub_mode byte 0x%02X — keeping previous value", data[3]);
        receivedSettings.sub_mode = (this->currentSettings.sub_mode != HPSubMode::UNKNOWN)
            ? this->currentSettings.sub_mode
            : HPSubMode::NORMAL;  // default to "NORMAL" when no prior value exists
    }
 
    auto auto_sub_mode_opt = hp_auto_sub_mode_from_wire(data[5]);
    if (auto_sub_mode_opt) {
        receivedSettings.auto_sub_mode = *auto_sub_mode_opt;
    } else {
        ESP_LOGW("Decoder", "Unknown auto_sub_mode byte 0x%02X — keeping previous value", data[5]);
        receivedSettings.auto_sub_mode = (this->currentSettings.auto_sub_mode != HPAutoSubMode::UNKNOWN)
            ? this->currentSettings.auto_sub_mode
            : HPAutoSubMode::AUTO_OFF;  // default to "AUTO_OFF" when no prior value exists
    }
 
    ESP_LOGD("Decoder", "[Stage : %s]", hp_stage_to_str(receivedSettings.stage));
    ESP_LOGD("Decoder", "[Sub Mode  : %s]", hp_sub_mode_to_str(receivedSettings.sub_mode));
    ESP_LOGD("Decoder", "[Auto Mode Sub Mode  : %s]", hp_auto_sub_mode_to_str(receivedSettings.auto_sub_mode));
 
    //this->heatpumpUpdate(receivedSettings);
    if (this->stage_sensor_ != nullptr) {
        if (receivedSettings.stage != this->currentSettings.stage) {
            this->currentSettings.stage = receivedSettings.stage;
            this->stage_sensor_->publish_state(hp_stage_to_str(receivedSettings.stage));
 
            // If using stage as operating fallback, update action immediately when stage changes
            // and publish to Home Assistant
            if (this->use_stage_for_operating_status_) {
                this->updateAction();
                this->publish_state();
            }
        }
    }
    if (this->Sub_mode_sensor_ != nullptr && receivedSettings.sub_mode != this->currentSettings.sub_mode) {
        this->currentSettings.sub_mode = receivedSettings.sub_mode;
        this->Sub_mode_sensor_->publish_state(hp_sub_mode_to_str(receivedSettings.sub_mode));
    }
    if (this->Auto_sub_mode_sensor_ != nullptr && receivedSettings.auto_sub_mode != this->currentSettings.auto_sub_mode) {
        this->currentSettings.auto_sub_mode = receivedSettings.auto_sub_mode;
        this->Auto_sub_mode_sensor_->publish_state(hp_auto_sub_mode_to_str(receivedSettings.auto_sub_mode));
    }
}

void CN105Climate::getSettingsFromResponsePacket() {
    heatpumpSettings receivedSettings{};
    heatpumpRunStates receivedRunStates{};
    ESP_LOGD("Decoder", "[0x02 is settings]");

    auto power_opt = hp_power_from_wire(data[3]);
    if (power_opt) {
        receivedSettings.power = *power_opt;
    } else {
        ESP_LOGW("Decoder", "Unknown power byte 0x%02X — keeping previous value", data[3]);
        receivedSettings.power = (this->currentSettings.power != HPPower::UNKNOWN)
            ? this->currentSettings.power
            : HPPower::OFF;  // default to "OFF" when no prior value exists
    }
 
    receivedSettings.iSee = data[4] > 0x08 ? true : false;
    uint8_t modeByte = receivedSettings.iSee ? (data[4] - 0x08) : data[4];
    auto mode_opt = hp_mode_from_wire(modeByte);
    if (mode_opt) {
        receivedSettings.mode = *mode_opt;
    } else {
        ESP_LOGW("Decoder", "Unknown mode byte 0x%02X — keeping previous value", modeByte);
        receivedSettings.mode = (this->currentSettings.mode != HPMode::UNKNOWN)
            ? this->currentSettings.mode
            : HPMode::AUTO;  // default to "AUTO" when no prior value exists
    }
 
    ESP_LOGD("Decoder", "[Power : %s]", hp_power_to_str(receivedSettings.power));
    ESP_LOGD("Decoder", "[iSee  : %d]", receivedSettings.iSee);
    ESP_LOGD("Decoder", "[Mode  : %s]", hp_mode_to_str(receivedSettings.mode));
 
    if (data[11] != 0x00) {
        int temp = data[11];
        temp -= 128;
        receivedSettings.temperature = (float)temp / 2;
        this->use_temperature_encoding_b_ = true;
    } else {
        uint8_t temp_byte = data[5];
        if (temp_byte <= 15) {
            receivedSettings.temperature = static_cast<float>(31 - temp_byte);
        } else {
            ESP_LOGW("Decoder", "Unknown temperature byte 0x%02X — keeping previous value", data[5]);
            receivedSettings.temperature = this->currentSettings.temperature;
        }
    }
 
    ESP_LOGD("Decoder", "[Temp °C: %f]", receivedSettings.temperature);
 
    auto fan_opt = hp_fan_from_wire(data[6]);
    if (fan_opt) {
        receivedSettings.fan = *fan_opt;
    } else {
        ESP_LOGW("Decoder", "Unknown fan byte 0x%02X — keeping previous value", data[6]);
        receivedSettings.fan = (this->currentSettings.fan != HPFanMode::UNKNOWN)
            ? this->currentSettings.fan
            : HPFanMode::AUTO;  // default to "AUTO" when no prior value exists
    }
    ESP_LOGD("Decoder", "[Fan: %s]", hp_fan_to_str(receivedSettings.fan));
 
    auto vane_opt = hp_vane_from_wire(data[7]);
    if (vane_opt) {
        receivedSettings.vane = *vane_opt;
    } else {
        ESP_LOGW("Decoder", "Unknown vane byte 0x%02X — keeping previous value", data[7]);
        receivedSettings.vane = (this->currentSettings.vane != HPVaneMode::UNKNOWN)
            ? this->currentSettings.vane
            : HPVaneMode::AUTO;  // default to "AUTO" when no prior value exists
    }
    ESP_LOGD("Decoder", "[Vane: %s]", hp_vane_to_str(receivedSettings.vane));
 
    // --- START OF MODIFIED SECTION - Reverted widevane section back to more or less original state
    if ((data[10] != 0) && (this->traits_.supports_swing_mode(climate::CLIMATE_SWING_HORIZONTAL))) {    // wideVane is not always supported
        uint8_t wideVaneByte = data[10] & 0x0F;
        auto wideVane_opt = hp_wide_vane_from_wire(wideVaneByte);
        if (wideVane_opt) {
            receivedSettings.wideVane = *wideVane_opt;
        } else {
            ESP_LOGW("Decoder", "Unknown wideVane byte 0x%02X — keeping previous value", wideVaneByte);
            receivedSettings.wideVane = (this->currentSettings.wideVane != HPWideVaneMode::UNKNOWN)
                ? this->currentSettings.wideVane
                : HPWideVaneMode::CENTER;  // default to "|" (center) when no prior value exists
        }
        this->wideVaneAdj = (data[10] & 0xF0) == 0x80 ? true : false;
        ESP_LOGD("Decoder", "[wideVane: %s (adj:%d)]", hp_wide_vane_to_str(receivedSettings.wideVane), this->wideVaneAdj);
    } else {
        ESP_LOGD("Decoder", "widevane is not supported");
    }
    // --- END OF MODIFIED SECTION ---

    if (this->iSee_sensor_ != nullptr) {
        this->iSee_sensor_->publish_state(receivedSettings.iSee);
    }

    // --- TARGET HUMIDITY (byte 12 of 0x02 settings packet) ---
    // Some premium models (e.g. MSZ-LN series) store a target humidity
    // percentage in data[12]. This value changes when the mode is switched
    // via the IR remote (e.g. COOL→70%, DRY→50%, HEAT→40%).
    // Not all models populate this byte — it may read 0x00 on unsupported units.
    if (this->target_humidity_sensor_ != nullptr) {
        uint8_t raw_humidity = data[12];
        if (raw_humidity > 0 && raw_humidity <= 100) {
            float humidity_pct = static_cast<float>(raw_humidity);
            if (this->target_humidity_sensor_->get_raw_state() != humidity_pct) {
                ESP_LOGD("Decoder", "[Target Humidity: %.0f%%]", humidity_pct);
                this->target_humidity_sensor_->publish_state(humidity_pct);
            }
        } else if (raw_humidity != 0) {
            ESP_LOGD("Decoder", "[Target Humidity byte out of range: 0x%02X]", raw_humidity);
        }
    }

    // --- AIRFLOW CONTROL START
    if (this->airflow_control_select_ != nullptr) {
        if (data[10] == 0x80) {
            if (receivedSettings.iSee) {
                auto airflow_opt = hp_airflow_control_from_wire(data[14]);
                if (airflow_opt) {
                    receivedRunStates.airflow_control = *airflow_opt;
                } else {
                    ESP_LOGW("Decoder", "Unknown airflow_control byte 0x%02X — keeping previous value", data[14]);
                    receivedRunStates.airflow_control = this->currentRunStates.airflow_control;
                }
            } else {
                // For some reason data[10] is 0x80, but the i-See sensor is not active. 
                // Some units let us do this, but the real mode is unknown (might be powersave) and the i-See sensor does not get activated.
                //receivedRunStates.airflow_control = "N/A";
                ESP_LOGD("Decoder", "i-See sensor not present/active.");
                receivedRunStates.airflow_control = HPAirflowControl::EVEN;
            }
        } else {
            receivedRunStates.airflow_control = HPAirflowControl::EVEN;
        }
        if (receivedRunStates.airflow_control != this->currentRunStates.airflow_control) {
            this->currentRunStates.airflow_control = receivedRunStates.airflow_control;
            this->airflow_control_select_->publish_state(hp_airflow_control_to_str(receivedRunStates.airflow_control));
        }
    }

    // --- AIRFLOW CONTROL END

    this->heatpumpUpdate(receivedSettings);
}

void CN105Climate::getRoomTemperatureFromResponsePacket() {

    heatpumpStatus receivedStatus{};

    //ESP_LOGD("Decoder", "[0x03 room temperature]");
    //this->last_received_packet_sensor->publish_state("0x62-> 0x03: Data -> Room temperature");
    //                 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    // FC 62 01 30 10 03 00 00 0E 00 94 B0 B0 FE 42 00 01 0A 64 00 00 A9
    //                         RT    OT RT SP ?? ?? ?? RM RM RM
    // RT = room temperature (in old format and in new format)
    // OT = outside air temperature
    // SP = room setpoint temperature?
    // RM = indoor unit operating time in minutes

    if (data[5] > 1) {
        receivedStatus.outsideAirTemperature = (data[5] - 128) / 2.0f;
    } else {
        receivedStatus.outsideAirTemperature = NAN;
    }

    if (data[6] != 0x00) {
        int temp = data[6];
        temp -= 128;
        receivedStatus.roomTemperature = temp / 2.0f;
        ESP_LOGD(LOG_TEMP_SENSOR_TAG, "data[6]  --> [Room °C: %f]", receivedStatus.roomTemperature);
    } else {
        uint8_t room_temp_byte = data[3];
        if (room_temp_byte <= 31) {
            receivedStatus.roomTemperature = static_cast<float>(10 + room_temp_byte);
        } else {
            ESP_LOGW("Decoder", "Unknown room_temp byte 0x%02X — keeping previous value", data[3]);
            receivedStatus.roomTemperature = this->currentStatus.roomTemperature;
        }
        ESP_LOGD(LOG_TEMP_SENSOR_TAG, "data[3] map --> [Room °C : %f]", receivedStatus.roomTemperature);
    }

    // Update the remote temperature control sensor (Issue 290)
    if (this->remote_temp_sensor_ != nullptr) {
        bool is_remote = false;
        if (this->remote_temp_keepalive_active_ && this->remoteTemperature_ > 0) {
            float diff = abs(receivedStatus.roomTemperature - this->remoteTemperature_);
            if (diff <= this->remote_temp_margin_) {
                is_remote = true;
            }
        }
        this->remote_temp_sensor_->publish_state(is_remote);
    }

    receivedStatus.runtimeHours = float((data[11] << 16) | (data[12] << 8) | data[13]) / 60;

    ESP_LOGD("Decoder", "[Room °C: %f]", receivedStatus.roomTemperature);
    ESP_LOGD("Decoder", "[OAT  °C: %f]", receivedStatus.outsideAirTemperature);

    // no change with this packet to currentStatus for operating and compressorFrequency
    receivedStatus.operating = currentStatus.operating;
    receivedStatus.compressorFrequency = currentStatus.compressorFrequency;
    receivedStatus.inputPower = currentStatus.inputPower;
    receivedStatus.kWh = currentStatus.kWh;
    this->statusChanged(receivedStatus);
}

void CN105Climate::getOperatingAndCompressorFreqFromResponsePacket() {
    //FC 62 01 30 10 06 00 00 1A 01 00 00 00 00 00 00 00 00 00 00 00 3C
    //MSZ-RW25VGHZ-SC1 / MUZ-RW25VGHZ-SC1
    //FC 62 01 30 10 06 00 00 00 01 00 08 05 50 00 00 42 00 00 00 00 B7
    //                           OP IP IP EU EU       ??
    // OP = operating status (1 = compressor running, 0 = standby)
    // IP = Current input power in Watts (16-bit decimal)
    // EU = energy usage
    //      (used energy in kWh = value/10)
    //      TODO: Currently the maximum size of the counter is not known and
    //            if the counter extends to other bytes.
    // ?? = unknown bytes that appear to have a fixed/constant value
    heatpumpStatus receivedStatus{};
    ESP_LOGD("Decoder", "[0x06 is status]");
    //this->last_received_packet_sensor->publish_state("0x62-> 0x06: Data -> Heatpump Status");

    // reset counter (because a reply indicates it is connected)
    this->nonResponseCounter = 0;
    receivedStatus.operating = data[4];
    // Some models (e.g. PAA/PUZ combo) seem to have some noise on the compressor frequency sensor, even when not in operation.
    // To avoid reporting random values, set the compressor frequency to 0 when the heatpump is not operating.
    receivedStatus.compressorFrequency = (data[4]) ? data[3] : 0;
    receivedStatus.inputPower = convert_input_power_to_W(float((data[5] << 8) | data[6]));
    receivedStatus.kWh = convert_energy_usage_to_kWh(float((data[7] << 8) | data[8]));

    // no change with this packet to roomTemperature
    receivedStatus.roomTemperature = currentStatus.roomTemperature;
    receivedStatus.outsideAirTemperature = currentStatus.outsideAirTemperature;
    receivedStatus.runtimeHours = currentStatus.runtimeHours;
    this->statusChanged(receivedStatus);
}

void CN105Climate::getHVACOptionsFromResponsePacket() {
    //MSZ-LN25VG2W
    //FC 62 01 30 10 42 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 18
    //                  AP NM CL
    // AP = air purifier (1 = on, 0 = off)
    // NM = night mode (1 = on, 0 = off)
    // CL = circulator (1 = on, 0 = off) ! MIGHT BE SAME BYTE AS ECONOCOOL - NEEDS TESTING !
    heatpumpRunStates receivedRunStates{};
    ESP_LOGD("Decoder", "[0x42 is HVAC options]");

    if (this->air_purifier_switch_ != nullptr) {
        receivedRunStates.air_purifier = data[1];
        ESP_LOGD("Decoder", "[Air purifier : %s]", receivedRunStates.air_purifier ? "ON" : "OFF");
        if (receivedRunStates.air_purifier != this->currentRunStates.air_purifier || receivedRunStates.air_purifier != this->air_purifier_switch_->state) {
            this->currentRunStates.air_purifier = receivedRunStates.air_purifier;
            this->air_purifier_switch_->publish_state(receivedRunStates.air_purifier);
        }
    }
    if (this->night_mode_switch_ != nullptr) {
        receivedRunStates.night_mode = data[2];
        ESP_LOGD("Decoder", "[Night mode : %s]", receivedRunStates.night_mode ? "ON" : "OFF");
        if (receivedRunStates.night_mode != this->currentRunStates.night_mode || receivedRunStates.night_mode != this->night_mode_switch_->state) {
            this->currentRunStates.night_mode = receivedRunStates.night_mode;
            this->night_mode_switch_->publish_state(receivedRunStates.night_mode);
        }
    }
    if (this->circulator_switch_ != nullptr) {
        receivedRunStates.circulator = data[3];
        ESP_LOGD("Decoder", "[Circulator : %s]", receivedRunStates.circulator ? "ON" : "OFF");
        if (receivedRunStates.circulator != this->currentRunStates.circulator || receivedRunStates.circulator != this->circulator_switch_->state) {
            this->currentRunStates.circulator = receivedRunStates.circulator;
            this->circulator_switch_->publish_state(receivedRunStates.circulator);
        }
    }
}

void CN105Climate::terminateCycle() {
    if (this->shouldSendExternalTemperature_) {
        // We will receive ACK packet for this.
        // Sending WantedSettings must be delayed in this case (lastSend timestamp updated).        
        ESP_LOGD(LOG_REMOTE_TEMP, "Sending remote temperature...");
        this->sendRemoteTemperature();
    }

    this->loopCycle.cycleEnded();

    if (this->hp_uptime_connection_sensor_ != nullptr) {
        // if the uptime connection sensor is configured
        // we trigger  manual update at the end of a cycle.
        this->hp_uptime_connection_sensor_->update();
    }

    this->nbCompleteCycles_++;
}
void CN105Climate::getErrorInfoFromResponsePacket() {
    ESP_LOGD("Decoder", "0x04 error info");
    if (this->error_code_sensor_ != nullptr) {
        uint8_t error_raw = this->data[4];
        uint8_t error_sub = this->data[5];
        // Bit 7 (0x80) is a protocol status flag ("error reporting available"),
        // not an actual error code. Use lower 7 bits for real error detection.
        uint8_t error_code = error_raw & 0x7F;
        if (error_code == 0x00 && error_sub == 0x00) {
            this->error_code_sensor_->publish_state("No Error");
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "Error 0x%02X sub 0x%02X", error_code, error_sub);
            this->error_code_sensor_->publish_state(buf);
        }
    }
}

void CN105Climate::getDataFromResponsePacket() {

    // D'abord, laissons l'orchestrateur traiter les codes connus
    const uint8_t code = this->data[0];
    if (this->scheduler_.process_response(code)) {
        return;
    }
    // Sinon, switch pour les cas non gÃƒÂ©rÃƒÂ©s par l'orchestrateur
    switch (code) {

    case 0x04:
        // Handled by orchestrator (r_error_info onResponse → getErrorInfoFromResponsePacket)
        // Reaching here means the scheduler did not intercept this response — unexpected
        ESP_LOGW("Decoder", "[0x04] reached switch fallback — should have been handled by orchestrator");
        break; // orchestrator

    case 0x05:
        /* timer packet */
        ESP_LOGW("Decoder", "[0x05 is Timer : not implemented]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x05: Data -> Timer Packet");
        break;

    case 0x06:
        break; // orchestrator
    case 0x09:
        break; // orchestrator

    case 0x10:
        ESP_LOGD("Decoder", "[0x10 is Unknown : not implemented]");
        //this->getAutoModeStateFromResponsePacket();
        break;

    case 0x20: // fallthrough
    case 0x22:
        break; // orchestrator

    case 0x42:
        break; // orchestrator

    default:
        ESP_LOGW("Decoder", "packet type [%02X] <-- unknown and unexpected", data[0]);
        //this->last_received_packet_sensor->publish_state("0x62-> ?? : Data -> Unknown");
        break;
    }

}

void CN105Climate::updateSuccess() {
    ESP_LOGD(LOG_ACK, "Last heatpump data update successful!");
    // nothing can be done here because we have no mean to know wether it is an external temp ack
    // or a wantedSettings update ack
}

void CN105Climate::processCommand() {
    switch (this->parser_.command()) {
    case 0x61:  /* last update was successful */
        this->hpPacketDebug(this->parser_.raw(), this->parser_.frame_size(), LOG_ACK);
        this->updateSuccess();
        break;

    case 0x62:  /* packet contains data (room °C, settings, timer, status, or functions...)*/
        this->getDataFromResponsePacket();
        break;
    case 0x7a:  // Connection success (User / standard)
    case 0x7b:  // Connection success (Installer / extended)
        // Log en INFO sur le tag dÃƒÂ©diÃƒÂ©, dÃƒÂ©tails en DEBUG via hpPacketDebug
        ESP_LOGI(LOG_CONN_TAG, "--> Heatpump did reply: connection success (%s, 0x%02X)! <--",
            (this->parser_.command() == 0x7b) ? "Installer" : "User",
            this->parser_.command());
        this->hpPacketDebug(this->parser_.raw(), this->parser_.frame_size(), LOG_CONN_TAG);
        // isHeatpumpConnected_ replaced by FSM transition in setHeatpumpConnected()
        this->setHeatpumpConnected(true);
        // let's say that the last complete cycle was over now
        this->loopCycle.lastCompleteCycleMs = CUSTOM_MILLIS;
        this->currentSettings.resetSettings();      // each time we connect, we need to reset current setting to force a complete sync with ha component state and receievdSettings
        this->currentRunStates.resetSettings();
        break;
    default:
        break;
    }
}


void CN105Climate::statusChanged(heatpumpStatus status) {

    if (status != currentStatus) {
        this->debugStatus("received", status);
        this->debugStatus("current", currentStatus);


        this->currentStatus.operating = status.operating;
        this->currentStatus.compressorFrequency = status.compressorFrequency;
        this->currentStatus.inputPower = status.inputPower;
        this->currentStatus.kWh = status.kWh;
        this->currentStatus.runtimeHours = status.runtimeHours;
        this->currentStatus.roomTemperature = status.roomTemperature;
        this->currentStatus.outsideAirTemperature = status.outsideAirTemperature;
        this->setCurrentTemperature(this->currentStatus.roomTemperature);

        this->updateAction();       // update action info on HA climate component
        this->publish_state();

        if (this->compressor_frequency_sensor_ != nullptr) {
            this->compressor_frequency_sensor_->publish_state(currentStatus.compressorFrequency);
        }

        if (this->input_power_sensor_ != nullptr) {
            this->input_power_sensor_->publish_state(currentStatus.inputPower);
        }

        if (this->kwh_sensor_ != nullptr) {
            this->kwh_sensor_->publish_state(currentStatus.kWh);
        }

        if (this->runtime_hours_sensor_ != nullptr) {
            this->runtime_hours_sensor_->publish_state(currentStatus.runtimeHours);
        }

        if (this->outside_air_temperature_sensor_ != nullptr) {
            this->outside_air_temperature_sensor_->publish_state(this->fahrenheitSupport_.normalizeHeatpumpTemperatureToUiTemperature(currentStatus.outsideAirTemperature));
        }
    } // else no change
}


void CN105Climate::publishStateToHA(heatpumpSettings& settings) {

    if ((this->wantedSettings.mode == HPMode::UNKNOWN) && (this->wantedSettings.power == HPPower::UNKNOWN)) {        // to prevent overwriting a user demand
        checkPowerAndModeSettings(settings);
    }

    this->updateAction();       // update action info on HA climate component

    if (this->wantedSettings.fan == HPFanMode::UNKNOWN) {  // to prevent overwriting a user demand
        checkFanSettings(settings);
    }

    if (this->wantedSettings.vane == HPVaneMode::UNKNOWN) { // to prevent overwriting a user demand
        checkVaneSettings(settings);
    }

    if (this->wantedSettings.wideVane == HPWideVaneMode::UNKNOWN) { // to prevent overwriting a user demand
        checkWideVaneSettings(settings);
    }

    // HA Temp
    // Ignorer temporairement une consigne entrante si une consigne utilisateur est en cours
    bool hasPendingUserTemp = (this->wantedSettings.temperature != -1.0f) && (this->wantedSettings.hasChanged) && (!this->wantedSettings.hasBeenSent);
    uint32_t graceWindowMs = this->get_update_interval() + DEFER_SCHEDULE_UPDATE_LOOP_DELAY;
    bool graceAfterSend = (this->wantedSettings.hasBeenSent) && ((CUSTOM_MILLIS - this->wantedSettings.lastChange) < graceWindowMs);
    if (!hasPendingUserTemp && !graceAfterSend) {
        if (this->wantedSettings.temperature == -1) { // to prevent overwriting a user demand
            this->updateTargetTemperaturesFromSettings(settings.temperature);
            this->currentSettings.temperature = settings.temperature;
        }
    } else {
        ESP_LOGD(LOG_SETTINGS_TAG, "Ignoring incoming setpoint due to pending user change or grace window");
    }

    this->currentSettings.iSee = settings.iSee;

    this->currentSettings.connected = true;

    this->mode = this->desired_mode_;
    this->target_temperature = this->desired_temp_;

    // publish to HA
    this->publish_state();

}



void CN105Climate::heatpumpUpdate(heatpumpSettings& settings) {
    // settings correponds to current settings
    ESP_LOGV(LOG_SETTINGS_TAG, "Settings received");
    // if received settings are different from current settings 
    if (settings != this->currentSettings) {
        ESP_LOGI(LOG_SETTINGS_TAG, "Settings changed, updating HA states");
        this->debugSettings("current", this->currentSettings);
        this->debugSettings("received", settings);
        this->debugSettings("wanted", this->wantedSettings);
        this->debugClimate("climate");
        this->publishStateToHA(settings);
    }

}

void CN105Climate::checkVaneSettings(heatpumpSettings& settings, bool updateCurrentSettings) {
    if (this->hasChanged(currentSettings.vane, settings.vane, "vane")) {    // widevane setting change ?
        ESP_LOGI(LOG_SETTINGS_TAG, "vane setting changed");

        //this->debugSettings("settings", settings);

        if (updateCurrentSettings) {
            //ESP_LOGD(LOG_SETTINGS_TAG, "updating currentSetting with new value");
            currentSettings.vane = settings.vane;
        }

        if (settings.vane == HPVaneMode::SWING) {
            if (currentSettings.wideVane == HPWideVaneMode::SWING) {
                this->swing_mode = climate::CLIMATE_SWING_BOTH;
            } else {
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
            }
        } else {
            if (currentSettings.wideVane == HPWideVaneMode::SWING) {
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
            } else {
                this->swing_mode = climate::CLIMATE_SWING_OFF;
            }
        }
        ESP_LOGD(LOG_SETTINGS_TAG, "Swing mode is: %i", this->swing_mode);
    }


    updateExtraSelectComponents(settings);
}

void CN105Climate::checkWideVaneSettings(heatpumpSettings& settings, bool updateCurrentSettings) {

    /* ******** HANDLE MITSUBISHI VANE CHANGES ********
     * VANE_MAP[7]        = {"AUTO", "1", "2", "3", "4", "5", "SWING"};
     * WIDEVANE_MAP[8]   = { "<<", "<",  "|",  ">",  ">>", "<>", "SWING", "AIRFLOW CONTROL" }
     */

    if (this->hasChanged(currentSettings.wideVane, settings.wideVane, "wideVane")) {    // widevane setting change ?
        ESP_LOGI(TAG, "widevane setting changed");
        this->debugSettings("settings", settings);

        // here I hope that the vane and widevane are always sent together
        if (updateCurrentSettings) {
            currentSettings.wideVane = settings.wideVane;
        }

        if (settings.wideVane == HPWideVaneMode::SWING) {
            if (currentSettings.vane == HPVaneMode::SWING) {
                this->swing_mode = climate::CLIMATE_SWING_BOTH;
            } else {
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
            }
        } else {
            if (currentSettings.vane == HPVaneMode::SWING) {
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
            } else {
                this->swing_mode = climate::CLIMATE_SWING_OFF;
            }
        }
        ESP_LOGD(TAG, "Swing mode is: %i", this->swing_mode);
    }

    /*if (this->hasChanged(this->van_orientation->state.c_str(), settings.vane, "select vane")) {
        ESP_LOGI(TAG, "vane setting (extra select component) changed");
        this->van_orientation->publish_state(currentSettings.vane);
    }*/

    updateExtraSelectComponents(settings);
}
void CN105Climate::updateExtraSelectComponents(heatpumpSettings& settings) {
    if (this->vertical_vane_select_ != nullptr) {
        if (this->hasChanged(this->vertical_vane_select_->current_option(), hp_vane_to_str(settings.vane), "select vane")) {
            ESP_LOGI(TAG, "vane setting (extra select component) changed");
            this->vertical_vane_select_->publish_state(hp_vane_to_str(settings.vane));
        }
    }
    if (this->horizontal_vane_select_ != nullptr) {
        if (this->hasChanged(this->horizontal_vane_select_->current_option(), hp_wide_vane_to_str(settings.wideVane), "select wideVane")) {
            ESP_LOGI(TAG, "widevane setting (extra select component) changed");
            this->horizontal_vane_select_->publish_state(hp_wide_vane_to_str(settings.wideVane));
        }
    }
}
void CN105Climate::checkFanSettings(heatpumpSettings& settings, bool updateCurrentSettings) {
    /*
         * ******* HANDLE FAN CHANGES ********
         *
         * const char* FAN_MAP[6]         = {"AUTO", "QUIET", "1", "2", "3", "4"};
         */
         // currentSettings.fan== NULL is true when it is the first time we get en answer from hp

    if (this->hasChanged(currentSettings.fan, settings.fan, "fan")) { // fan setting change ?
        ESP_LOGI(TAG, "fan setting changed");
        if (updateCurrentSettings) {
            currentSettings.fan = settings.fan;
        }

        if (settings.fan == HPFanMode::QUIET) {
            this->fan_mode = climate::CLIMATE_FAN_QUIET;
        } else if (settings.fan == HPFanMode::F1) {
            this->fan_mode = climate::CLIMATE_FAN_LOW;
        } else if (settings.fan == HPFanMode::F2) {
            this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        } else if (settings.fan == HPFanMode::F3) {
            this->fan_mode = climate::CLIMATE_FAN_MIDDLE;
        } else if (settings.fan == HPFanMode::F4) {
            this->fan_mode = climate::CLIMATE_FAN_HIGH;
        } else { //case "AUTO" or default:
            this->fan_mode = climate::CLIMATE_FAN_AUTO;
        }
        if (this->fan_mode.has_value()) {
            ESP_LOGD(TAG, "Fan mode is: %i", static_cast<int>(this->fan_mode.value()));
        } else {
            ESP_LOGD(TAG, "Fan mode is not set");
        }
    }
}


void CN105Climate::checkPowerAndModeSettings(heatpumpSettings& settings, bool updateCurrentSettings) {
    climate::ClimateMode physical_mode = climate::CLIMATE_MODE_OFF;
    if (settings.power == HPPower::ON) {
        if (settings.mode == HPMode::HEAT) {
            physical_mode = climate::CLIMATE_MODE_HEAT;
        } else if (settings.mode == HPMode::DRY) {
            physical_mode = climate::CLIMATE_MODE_DRY;
        } else if (settings.mode == HPMode::COOL) {
            physical_mode = climate::CLIMATE_MODE_COOL;
        } else if (settings.mode == HPMode::FAN) {
            physical_mode = climate::CLIMATE_MODE_FAN_ONLY;
        } else if (settings.mode == HPMode::AUTO) {
            physical_mode = climate::CLIMATE_MODE_AUTO;
        }
    }

    if (updateCurrentSettings) {
        currentSettings.power = settings.power;
        currentSettings.mode = settings.mode;
    }

    if (!this->first_real_state_received_) {
        this->first_real_state_received_ = true;
        ESP_LOGI(TAG, "First physical climate settings received: power=%s, mode=%s, temp=%.1f", 
                 hp_power_to_str(settings.power), hp_mode_to_str(settings.mode), settings.temperature);
        
        bool fan_stop_state = this->fan_stop_switch_ != nullptr ? this->fan_stop_switch_->state : false;
        bool preserve_restored_mode = fan_stop_state && 
                                      (this->desired_mode_ == climate::CLIMATE_MODE_HEAT || this->desired_mode_ == climate::CLIMATE_MODE_COOL) && 
                                      (physical_mode == climate::CLIMATE_MODE_OFF);
        if (preserve_restored_mode) {
            ESP_LOGI(TAG, "Preserving restored desired mode (%s) because physical unit is OFF under Fan Stop.",
                     climate::climate_mode_to_string(this->desired_mode_));
            this->mode = this->desired_mode_;
            this->target_temperature = this->desired_temp_;
            this->last_commanded_real_mode_ = climate::CLIMATE_MODE_OFF;
            this->last_commanded_real_temp_ = this->desired_temp_;
        } else {
            this->desired_mode_ = physical_mode;
            this->mode = physical_mode;
            if (!std::isnan(settings.temperature)) {
                this->desired_temp_ = settings.temperature;
                this->target_temperature = settings.temperature;
            }
            this->last_commanded_real_mode_ = physical_mode;
            this->last_commanded_real_temp_ = settings.temperature;
        }
        
        this->evaluate_fan_stop_and_ltp();
        return;
    }

    // Check if we are inside the 5-second lockout window
    if (CUSTOM_MILLIS - this->last_mode_command_time_ms_ < 5000) {
        // Ignore setting this->mode from physical update during lockout
        ESP_LOGD(TAG, "Inside command lockout window, ignoring physical power/mode update");
        this->mode = this->desired_mode_;
        this->target_temperature = this->desired_temp_;
        return;
    }

    // Outside lockout window - check for external overrides (e.g. IR Remote)
    if (physical_mode != this->last_commanded_real_mode_) {
        ESP_LOGI(TAG, "External HVAC mode change detected: from %s to %s. Disabling Fan Stop.",
                 climate::climate_mode_to_string(this->last_commanded_real_mode_),
                 climate::climate_mode_to_string(physical_mode));
                 
        if (this->fan_stop_switch_ != nullptr && this->fan_stop_switch_->state) {
            this->fan_stop_switch_->turn_off();
        }
        
        this->desired_mode_ = physical_mode;
        this->mode = physical_mode;
        if (!std::isnan(settings.temperature)) {
            this->desired_temp_ = settings.temperature;
            this->target_temperature = settings.temperature;
        }
        
        this->last_commanded_real_mode_ = physical_mode;
        this->last_commanded_real_temp_ = settings.temperature;
        
        this->publish_state();
    } else if (physical_mode != climate::CLIMATE_MODE_OFF && !std::isnan(settings.temperature) && fabsf(settings.temperature - this->last_commanded_real_temp_) >= 0.25f) {
        ESP_LOGI(TAG, "External target temperature change detected: from %.1f to %.1f.",
                 this->last_commanded_real_temp_, settings.temperature);
                 
        this->desired_temp_ = settings.temperature;
        this->target_temperature = settings.temperature;
        this->last_commanded_real_temp_ = settings.temperature;
        
        this->publish_state();
        this->evaluate_fan_stop_and_ltp();
    } else {
        // No external override. Force the HA component state to remain aligned with user desired settings.
        this->mode = this->desired_mode_;
        this->target_temperature = this->desired_temp_;
    }
}
