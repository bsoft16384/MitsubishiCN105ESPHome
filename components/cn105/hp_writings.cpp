#include "cn105.h"
#include <algorithm>  // for std::max

using namespace esphome;

uint8_t CN105Climate::check_sum(uint8_t bytes[], int len) {
    return cn105_protocol::checksum(bytes, len);
}


void CN105Climate::send_first_connection_packet() {
    if (this->is_uart_ready()) {
        this->lastReconnectTimeMs = CUSTOM_MILLIS;          // marker to prevent to many reconnections
        this->set_heatpump_connected(false);
        uint8_t packet[CONNECT_LEN];
        memcpy(packet, CONNECT, CONNECT_LEN);

        // Choix du mode de handshake: standard (0x5A) ou installateur (0x5B)
        packet[1] = this->installer_mode_effective_ ? 0x5B : 0x5A;
        // CONNECT has a pre-calculated checksum in the constant; if we modify the command byte, we must recalculate it.
        packet[CONNECT_LEN - 1] = check_sum(packet, CONNECT_LEN - 1);

        ESP_LOGI(LOG_CONN_TAG, "Sending connection packet in %s mode (0x%02X)...", this->installer_mode_effective_ ? "Installer" : "Standard", packet[1]);

        // Byte details as DEBUG on the connection tag
        this->hp_packet_debug(packet, CONNECT_LEN, LOG_CONN_TAG);

        this->write_packet(packet, CONNECT_LEN, false);      // checkIsActive=false because it's the first packet and we don't have any reply yet

        this->lastSend = CUSTOM_MILLIS;
        this->lastConnectRqTimeMs = CUSTOM_MILLIS;
        this->nbHeatpumpConnections_++;

        // we wait for a 10s timeout to check if the hp has replied to connection packet
        this->set_timeout("checkFirstConnection", 10000, [this]() {
            if (!this->is_heatpump_connected()) {
                ESP_LOGE(LOG_CONN_TAG, "--> Heatpump did not reply: NOT CONNECTED <--");
                // Automatic fallback: if installer mode is requested but the heatpump ignores 0x5B,
                // we retry once in standard mode (0x5A) to preserve connectivity.
                if (this->installer_mode_ && this->installer_mode_effective_ && !this->installer_mode_fallback_done_) {
                    this->installer_mode_effective_ = false;
                    this->installer_mode_fallback_done_ = true;
                    ESP_LOGW(LOG_CONN_TAG, "No reply to installer handshake (0x5B). Falling back to standard handshake (0x5A).");
                }
                ESP_LOGI(LOG_CONN_TAG, "Reinitializing UART and trying to connect again...");
                this->reconnect_uart();
            }});

    } else {
        ESP_LOGE(LOG_CONN_TAG, "UART doesn't seem to be connected...");
        this->setup_uart();
    }
}




// void CN105Climate::statusChanged() {
//     ESP_LOGD(TAG, "hpStatusChanged ->");
//     this->current_temperature = currentStatus.roomTemperature;

//     ESP_LOGD(TAG, "t°: %f", currentStatus.roomTemperature);
//     ESP_LOGD(TAG, "operating: %d", currentStatus.operating);
//     ESP_LOGD(TAG, "compressor freq: %f", currentStatus.compressorFrequency);

//     this->updateAction();
//     this->publish_state();
// }

void CN105Climate::prepare_info_packet(uint8_t* packet, int length) {
    ESP_LOGV(TAG, "preparing info packet...");

    memset(packet, 0, length * sizeof(uint8_t));

    for (int i = 0; i < INFOHEADER_LEN && i < length; i++) {
        packet[i] = INFOHEADER[i];
    }
}

void CN105Climate::prepare_set_packet(uint8_t* packet, int length) {
    ESP_LOGV(TAG, "preparing Set packet...");
    memset(packet, 0, length * sizeof(uint8_t));

    for (int i = 0; i < HEADER_LEN && i < length; i++) {
        packet[i] = HEADER[i];
    }
}

void CN105Climate::write_packet(uint8_t* packet, int length, bool checkIsActive) {

    if ((this->is_uart_ready()) &&
        (this->is_heatpump_connection_active() || (!checkIsActive))) {

        ESP_LOGD(TAG, "writing packet...");
        this->hp_packet_debug(packet, length, "WRITE");

        for (int i = 0; i < length; i++) {
            this->get_hw_serial_()->write_byte((uint8_t)packet[i]);
        }

        // Prevent sending wantedSettings too soon after writing for example the remote temperature update packet
        this->lastSend = CUSTOM_MILLIS;

    } else {
        ESP_LOGW(TAG, "could not write as asked, because UART is not connected");
        this->reconnect_uart();
        ESP_LOGW(TAG, "delaying packet writing because we need to reconnect first...");
        if (length > PACKET_LEN) {
            ESP_LOGE(TAG, "Packet length %d exceeds PACKET_LEN %d, dropping.", length, PACKET_LEN);
            return;
        }
        memcpy(this->pending_packet_, packet, static_cast<size_t>(length));
        this->pending_packet_len_ = length;
        this->pending_check_is_active_ = checkIsActive;
        this->has_pending_packet_ = true;
        this->set_timeout("write", 4000, [this]() { this->try_write_pending_packet(); });
    }
}

void CN105Climate::try_write_pending_packet() {
    if (!this->has_pending_packet_) return;
    if (!this->is_uart_ready()) {
        this->reconnect_uart();
        this->set_timeout("write", 2000, [this]() { this->try_write_pending_packet(); });
        return;
    }
    this->write_packet(this->pending_packet_, this->pending_packet_len_, this->pending_check_is_active_);
    this->has_pending_packet_ = false;
}

const char* CN105Climate::get_mode_setting() {
    HPMode m = (this->wantedSettings.mode != HPMode::UNKNOWN) ? this->wantedSettings.mode : this->currentSettings.mode;
    return hp_mode_to_str(m);
}
 
const char* CN105Climate::get_power_setting() {
    HPPower p = (this->wantedSettings.power != HPPower::UNKNOWN) ? this->wantedSettings.power : this->currentSettings.power;
    return hp_power_to_str(p);
}
 
const char* CN105Climate::get_vane_setting() {
    HPVaneMode v = (this->wantedSettings.vane != HPVaneMode::UNKNOWN) ? this->wantedSettings.vane : this->currentSettings.vane;
    return hp_vane_to_str(v);
}
 
const char* CN105Climate::get_wide_vane_setting() {
    HPWideVaneMode wv = (this->wantedSettings.wideVane != HPWideVaneMode::UNKNOWN) ? this->wantedSettings.wideVane : this->currentSettings.wideVane;
    if (this->wantedSettings.wideVane != HPWideVaneMode::UNKNOWN) {
        if (this->wantedSettings.wideVane == HPWideVaneMode::AIRFLOW_CONTROL && !this->currentSettings.iSee) {
            wv = this->currentSettings.wideVane;
        }
    }
    return hp_wide_vane_to_str(wv);
}
 
const char* CN105Climate::get_fan_speed_setting() {
    HPFanMode f = (this->wantedSettings.fan != HPFanMode::UNKNOWN) ? this->wantedSettings.fan : this->currentSettings.fan;
    return hp_fan_to_str(f);
}
 
float CN105Climate::get_temperature_setting() {
    if (this->wantedSettings.temperature.has_value()) {
        return *this->wantedSettings.temperature;
    } else {
        return this->currentSettings.temperature.value_or(this->target_temperature);
    }
}
const char* CN105Climate::get_airflow_control_setting() {
    HPAirflowControl ac = (this->wantedRunStates.airflow_control != HPAirflowControl::UNKNOWN) ? this->wantedRunStates.airflow_control : this->currentRunStates.airflow_control;
    return hp_airflow_control_to_str(ac);
}
bool CN105Climate::get_air_purifier_run_state() {
    if (this->wantedRunStates.air_purifier != this->currentRunStates.air_purifier) {
        return this->wantedRunStates.air_purifier;
    } else {
        return this->currentRunStates.air_purifier;
    }
}
bool CN105Climate::get_night_mode_run_state() {
    if (this->wantedRunStates.night_mode != this->currentRunStates.night_mode) {
        return this->wantedRunStates.night_mode;
    } else {
        return this->currentRunStates.night_mode;
    }
}
bool CN105Climate::get_circulator_run_state() {
    if (this->wantedRunStates.circulator != this->currentRunStates.circulator) {
        return this->wantedRunStates.circulator;
    } else {
        return this->currentRunStates.circulator;
    }
}


void CN105Climate::create_packet(uint8_t* packet) {
    prepare_set_packet(packet, PACKET_LEN);

    //ESP_LOGD(TAG, "checking differences bw asked settings and current ones...");
    ESP_LOGD(TAG, "building packet for writing...");

    if (this->wantedSettings.power != HPPower::UNKNOWN) {
        ESP_LOGD(TAG, "power -> %s", get_power_setting());
        auto val_opt = hp_power_to_wire(wantedSettings.power);
        if (val_opt) { packet[8] = *val_opt; packet[6] += CONTROL_PACKET_1[0]; } else { ESP_LOGW(TAG, "Ignoring invalid power setting while building packet"); }
    }
 
    if (this->wantedSettings.mode != HPMode::UNKNOWN) {
        ESP_LOGD(TAG, "heatpump mode -> %s", get_mode_setting());
        auto val_opt = hp_mode_to_wire(wantedSettings.mode);
        if (val_opt) { packet[9] = *val_opt; packet[6] += CONTROL_PACKET_1[1]; } else { ESP_LOGW(TAG, "Ignoring invalid mode setting while building packet"); }
    }
 
    if (wantedSettings.temperature.has_value()) {
        ESP_LOGD(TAG, "temperature -> %f", get_temperature_setting());
        packet[19] = cn105_protocol::encode_temperature_b(get_temperature_setting());
        packet[6] += CONTROL_PACKET_1[2];
    }
 
    if (this->wantedSettings.fan != HPFanMode::UNKNOWN) {
        ESP_LOGD(TAG, "heatpump fan -> %s", get_fan_speed_setting());
        auto val_opt = hp_fan_to_wire(wantedSettings.fan);
        if (val_opt) { packet[11] = *val_opt; packet[6] += CONTROL_PACKET_1[3]; } else { ESP_LOGW(TAG, "Ignoring invalid fan setting while building packet"); }
    }
 
    if (this->wantedSettings.vane != HPVaneMode::UNKNOWN) {
        ESP_LOGD(TAG, "heatpump vane -> %s", get_vane_setting());
        auto val_opt = hp_vane_to_wire(wantedSettings.vane);
        if (val_opt) { packet[12] = *val_opt; packet[6] += CONTROL_PACKET_1[4]; } else { ESP_LOGW(TAG, "Ignoring invalid vane setting while building packet"); }
    }
 
    if (this->wantedSettings.wideVane != HPWideVaneMode::UNKNOWN) {
        ESP_LOGD(TAG, "heatpump widevane -> %s", get_wide_vane_setting());
        auto val_opt = hp_wide_vane_to_wire(wantedSettings.wideVane);
        if (val_opt) {
            packet[18] = *val_opt | (this->wideVaneAdj ? 0x80 : 0x00);
            packet[7] += CONTROL_PACKET_2[0];


            switch (this->vane_type_) {
                case VaneType::SPLIT_HORIZONTAL:
                    // Experimental: Left Horizontal Vane support for dual vane units (Type A)
                    // Byte 16 is used in IR protocol for Left Vane (which corresponds to Horizontal/Wide Vane on these units)
                    // Copy the base WIDEVANE value (without adjustment bit) to Byte 16
                    packet[16] = *val_opt;
                    break;
                case VaneType::SPLIT_VERTICAL:
                    // Experimental: Split Vertical Vane support (Type B)
                    // TODO: Reverse engineering required for Byte 12 or other control bytes.
                    // For now, logging to help debugging.
                    ESP_LOGD(TAG, "Split Vertical Vane: WideVane set to %s. Packet[12] (Vertical) is %02X", get_wide_vane_setting(), packet[12]);
                    break;
                case VaneType::STANDARD:
                default:
                    // No special handling
                    break;
            }
        } else { ESP_LOGW(TAG, "Ignoring invalid wideVane setting while building packet"); }
    }


    // add the checksum
    uint8_t chkSum = check_sum(packet, 21);
    packet[21] = chkSum;
    //ESP_LOGD(TAG, "debug before write packet:");
    //this->hpPacketDebug(packet, 22, "WRITE");
}





void CN105Climate::publish_wanted_settings_state_to_ha() {

    if ((this->wantedSettings.mode != HPMode::UNKNOWN) || (this->wantedSettings.power != HPPower::UNKNOWN)) {
        check_power_and_mode_settings(this->wantedSettings, false);
        this->update_action();       // update action info on HA climate component
    }
 
    if (this->wantedSettings.fan != HPFanMode::UNKNOWN) {
        check_fan_settings(this->wantedSettings, false);
    }
 
 
    if ((this->wantedSettings.vane != HPVaneMode::UNKNOWN) || (this->wantedSettings.wideVane != HPWideVaneMode::UNKNOWN)) {
        if (this->wantedSettings.vane == HPVaneMode::UNKNOWN) { // to prevent a nullpointer error
            this->wantedSettings.vane = this->currentSettings.vane;
        }
        if (this->wantedSettings.wideVane == HPWideVaneMode::UNKNOWN) { // to prevent a nullpointer error
            this->wantedSettings.wideVane = this->currentSettings.wideVane;
        }

        check_vane_settings(this->wantedSettings, false);
    }

    // HA Temp — only update if this SET includes an explicit temperature change;
    // otherwise the stale currentSettings.temperature would overwrite the UI.
    if (this->wantedSettings.temperature.has_value()) {
        this->update_target_temperatures_from_settings(this->get_temperature_setting());
    }

    if ((this->wantedSettings.vane != HPVaneMode::UNKNOWN) || (this->wantedSettings.wideVane != HPWideVaneMode::UNKNOWN)) {
        this->update_extra_select_components(this->wantedSettings);
    }

    // publish to HA
    this->publish_state();

}

void CN105Climate::publish_wanted_run_states_state_to_ha() {
    if (this->wantedRunStates.airflow_control != HPAirflowControl::UNKNOWN) {
        if (this->airflow_control_select_ != nullptr) {
            if (this->has_changed(this->airflow_control_select_->current_option(), hp_airflow_control_to_str(this->wantedRunStates.airflow_control), "select airflow control")) {
                ESP_LOGI(TAG, "airflow control setting changed");
                this->airflow_control_select_->publish_state(hp_airflow_control_to_str(wantedRunStates.airflow_control));
            }
        }
    }
    if (this->wantedRunStates.air_purifier > -1) {
        if (this->air_purifier_switch_ != nullptr && this->air_purifier_switch_->state != this->wantedRunStates.air_purifier) {
            ESP_LOGI(TAG, "air purifier setting changed");
            this->air_purifier_switch_->publish_state(wantedRunStates.air_purifier);
        }
    }
    if (this->wantedRunStates.night_mode > -1) {
        if (this->night_mode_switch_ != nullptr && this->night_mode_switch_->state != this->wantedRunStates.night_mode) {
            ESP_LOGI(TAG, "night mode setting changed");
            this->night_mode_switch_->publish_state(wantedRunStates.night_mode);
        }
    }
    if (this->wantedRunStates.circulator > -1) {
        if (this->circulator_switch_ != nullptr && this->circulator_switch_->state != this->wantedRunStates.circulator) {
            ESP_LOGI(TAG, "circulator setting changed");
            this->circulator_switch_->publish_state(wantedRunStates.circulator);
        }
    }
}


void CN105Climate::send_wanted_settings_delegate() {
    this->wantedSettings.hasBeenSent = true;
    this->lastSend = CUSTOM_MILLIS;
    ESP_LOGI(TAG, "sending wantedSettings..");
    this->debug_settings("wantedSettings", wantedSettings);
    // and then we send the update packet
    uint8_t packet[PACKET_LEN] = {};
    this->create_packet(packet);
    this->write_packet(packet, PACKET_LEN);
    this->hp_packet_debug(packet, 22, "WRITE_SETTINGS");

    this->publish_wanted_settings_state_to_ha();

    // as soon as the packet is sent, we reset the settings
    this->wantedSettings.resetSettings();

    // as we've just sent a packet to the heatpump, we let it time for process
    // this might not be necessary but, we give it a try because of issue #32
    // https://github.com/echavet/MitsubishiCN105ESPHome/issues/32
    this->loopCycle.defer_cycle();
}

/**
 * builds and send all an update packet to the heatpump
 *
 *
*/
void CN105Climate::send_wanted_settings() {
    if (this->is_heatpump_connection_active() && this->is_uart_ready()) {
        if (CUSTOM_MILLIS - this->lastSend > 300) {        // we don't want to send too many packets
            this->send_wanted_settings_delegate();
        } else {
            ESP_LOGD(TAG, "will sendWantedSettings later because we've sent one too recently...");
        }
    } else {
        this->reconnect_if_connection_lost();
    }
}

void CN105Climate::build_and_send_request_packet(int packetType) {
    // Legacy path kept temporarily if some callsites still pass packetType indices.
    // Map legacy indices to real codes and delegate to buildAndSendInfoPacket.
    uint8_t code = 0x02; // default to settings
    switch (packetType) {
    case 0: code = 0x02; break; // RQST_PKT_SETTINGS
    case 1: code = 0x03; break; // RQST_PKT_ROOM_TEMP
    case 2: code = 0x04; break; // RQST_PKT_UNKNOWN
    case 3: code = 0x05; break; // RQST_PKT_TIMERS
    case 4: code = 0x06; break; // RQST_PKT_STATUS
    case 5: code = 0x09; break; // RQST_PKT_STANDBY
    case 6: code = 0x42; break; // RQST_PKT_HVAC_OPTIONS
    default: code = 0x02; break;
    }
    this->build_and_send_info_packet(code);
}

void CN105Climate::build_and_send_info_packet(uint8_t code) {
    uint8_t packet[PACKET_LEN] = {};
    create_info_packet(packet, code);
    this->write_packet(packet, PACKET_LEN);
}



void CN105Climate::build_and_send_requests_info_packets() {
    if (this->is_heatpump_connected()) {
        ESP_LOGV(LOG_UPD_INT_TAG, "triggering infopacket because of update interval tick");
        ESP_LOGV("CONTROL_WANTED_SETTINGS", "hasChanged is %s", wantedSettings.hasChanged ? "true" : "false");
        this->loopCycle.cycle_started();
        this->nbCycles_++;
        // Sends the first sendable request (the list is registered once at the constructor)
        this->scheduler_.send_next_after(0x00); // 0x00 -> start, pick first eligible
    } else {
        this->reconnect_if_connection_lost();
    }
}





void CN105Climate::create_info_packet(uint8_t* packet, uint8_t code) {
    ESP_LOGD(TAG, "creating Info packet");
    // add the header to the packet
    for (int i = 0; i < INFOHEADER_LEN; i++) {
        packet[i] = INFOHEADER[i];
    }

    // directly set requested info code (0x02, 0x03, 0x06, 0x09, 0x42, ...)
    packet[5] = code;

    // pad the packet out
    for (int i = 0; i < 15; i++) {
        packet[i + 6] = 0x00;
    }

    // add the checksum
    uint8_t chkSum = check_sum(packet, 21);
    packet[21] = chkSum;
}


void CN105Climate::send_remote_temperature_packet() {
    // Actual writer for the remote temperature (0x07) packet.
    // Rate limiting and 0.5°C change-detection live upstream in set_remote_temperature();
    // the keep-alive timer forces periodic resends. This function just builds and sends,
    // and is the single source of truth for the last-sent tracking fields.
    uint32_t now = CUSTOM_MILLIS;
    bool temp_changed = (this->remoteTemperature_ != this->last_remote_temp_sent_);

    uint8_t packet[PACKET_LEN] = {};

    prepare_set_packet(packet, PACKET_LEN);

    packet[5] = 0x07;
    if (this->remoteTemperature_ != 0.0f) {
        packet[6] = 0x01;
        float clamped_temp = std::max(8.0f, std::min(this->remoteTemperature_, 37.5f));
        cn105_protocol::encode_remote_temperature(clamped_temp, packet[7], packet[8]);
    } else {
        packet[8] = 0x80; //MHK1 send 80, even though it could be 00, since ControlByte is 00
    }
    // add the checksum
    uint8_t chkSum = check_sum(packet, 21);
    packet[21] = chkSum;

    ESP_LOGD(LOG_REMOTE_TEMP, "Sending remote temperature packet... -> %.1f%s",
        this->remoteTemperature_, temp_changed ? " (changed)" : " (keep-alive)");
    write_packet(packet, PACKET_LEN);

    // Cancel any outstanding deferred write since we just sent the latest value
    this->cancel_timeout("deferred_remote_temp_send");

    // Update send tracking (single source of truth for the upstream rate limiter)
    this->last_remote_temp_send_ms_ = now;
    this->last_remote_temp_sent_ = this->remoteTemperature_;
}

void CN105Climate::send_remote_temperature() {
    this->shouldSendExternalTemperature_ = false;

    // Send the packet
    this->send_remote_temperature_packet();
}

void CN105Climate::send_wanted_run_states() {
    uint8_t packet[PACKET_LEN] = {};

    prepare_set_packet(packet, PACKET_LEN);

    packet[5] = 0x08;
    if (this->wantedRunStates.airflow_control != HPAirflowControl::UNKNOWN) {
        ESP_LOGD(TAG, "airflow control -> %s", get_airflow_control_setting());
        auto val_opt = hp_airflow_control_to_wire(wantedRunStates.airflow_control);
        if (val_opt) {
            packet[11] = *val_opt;
            packet[6] += RUN_STATE_PACKET_1[4];
        }
    }
    if (this->wantedRunStates.air_purifier > -1) {
        if (get_air_purifier_run_state() != currentRunStates.air_purifier) {
            ESP_LOGI(TAG, "air purifier switch state -> %s", get_air_purifier_run_state() ? "ON" : "OFF");
            packet[17] = get_air_purifier_run_state() ? 0x01 : 0x00;
            packet[7] += RUN_STATE_PACKET_2[1];
        }
    }
    if (this->wantedRunStates.night_mode > -1) {
        if (get_night_mode_run_state() != currentRunStates.night_mode) {
            ESP_LOGI(TAG, "night mode switch state -> %s", this->get_night_mode_run_state() ? "ON" : "OFF");
            packet[18] = get_night_mode_run_state() ? 0x01 : 0x00;
            packet[7] += RUN_STATE_PACKET_2[2];
        }
    }
    if (this->wantedRunStates.circulator > -1) {
        if (get_circulator_run_state() != currentRunStates.circulator) {
            ESP_LOGI(TAG, "circulator switch state -> %s", get_circulator_run_state() ? "ON" : "OFF");
            packet[19] = get_circulator_run_state() ? 0x01 : 0x00;
            packet[7] += RUN_STATE_PACKET_2[3];
        }
    }

    // Add the checksum
    uint8_t chkSum = check_sum(packet, 21);
    packet[21] = chkSum;
    ESP_LOGD(LOG_SET_RUN_STATE, "Sending set run state package (0x08)");
    write_packet(packet, PACKET_LEN);

    this->publish_wanted_run_states_state_to_ha();

    this->wantedRunStates.resetSettings();
    this->loopCycle.defer_cycle();
}
