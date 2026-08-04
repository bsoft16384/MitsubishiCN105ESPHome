#include "cn105.h"
#include <algorithm>  // for std::max

using namespace esphome;

uint8_t CN105Climate::check_sum(uint8_t bytes[], int len) { return cn105_protocol::checksum(bytes, len); }

void CN105Climate::send_first_connection_packet() {
  if (this->is_uart_ready()) {
    this->last_reconnect_time_ms_ = CUSTOM_MILLIS;  // marker to prevent to many reconnections
    this->set_heatpump_connected(false);
    uint8_t packet[CONNECT_LEN];
    memcpy(packet, CONNECT, CONNECT_LEN);

    // Handshake mode selection: standard (0x5A) or installer (0x5B)
    packet[1] = this->installer_mode_effective_ ? 0x5B : 0x5A;
    // CONNECT has a pre-calculated checksum in the constant; if we modify the command byte, we must recalculate it.
    packet[CONNECT_LEN - 1] = check_sum(packet, CONNECT_LEN - 1);

    ESP_LOGI(LOG_CONN_TAG, "Sending connection packet in %s mode (0x%02X)...",
             this->installer_mode_effective_ ? "Installer" : "Standard", packet[1]);

    // Byte details as DEBUG on the connection tag
    this->hp_packet_debug(packet, CONNECT_LEN, LOG_CONN_TAG);

    this->write_packet(packet, CONNECT_LEN,
                       false);  // check_is_active=false because it's the first packet and we don't have any reply yet

    this->last_send_ = CUSTOM_MILLIS;
    this->last_connect_rq_time_ms_ = CUSTOM_MILLIS;
    this->nb_heatpump_connections_++;

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
      }
    });

  } else {
    ESP_LOGE(LOG_CONN_TAG, "UART doesn't seem to be connected...");
    this->setup_uart();
  }
}

void CN105Climate::prepare_info_packet(uint8_t *packet, int length) {
  ESP_LOGV(TAG, "preparing info packet...");

  memset(packet, 0, length * sizeof(uint8_t));

  for (int i = 0; i < INFOHEADER_LEN && i < length; i++) {
    packet[i] = INFOHEADER[i];
  }
}

void CN105Climate::prepare_set_packet(uint8_t *packet, int length) {
  ESP_LOGV(TAG, "preparing Set packet...");
  memset(packet, 0, length * sizeof(uint8_t));

  for (int i = 0; i < HEADER_LEN && i < length; i++) {
    packet[i] = HEADER[i];
  }
}

void CN105Climate::write_packet(uint8_t *packet, int length, bool check_is_active) {
  if ((this->is_uart_ready()) && (this->is_heatpump_connection_active() || (!check_is_active))) {
    ESP_LOGD(TAG, "writing packet...");
    this->hp_packet_debug(packet, length, "WRITE");

    for (int i = 0; i < length; i++) {
      this->get_hw_serial_()->write_byte((uint8_t) packet[i]);
    }

    // Prevent sending wanted_settings_ too soon after writing for example the remote temperature update packet
    this->last_send_ = CUSTOM_MILLIS;

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
    this->pending_check_is_active_ = check_is_active;
    this->has_pending_packet_ = true;
    this->set_timeout("write", 4000, [this]() { this->try_write_pending_packet(); });
  }
}

void CN105Climate::try_write_pending_packet() {
  if (!this->has_pending_packet_)
    return;
  if (!this->is_uart_ready()) {
    this->reconnect_uart();
    this->set_timeout("write", 2000, [this]() { this->try_write_pending_packet(); });
    return;
  }
  this->write_packet(this->pending_packet_, this->pending_packet_len_, this->pending_check_is_active_);
  this->has_pending_packet_ = false;
}

const char *CN105Climate::get_mode_setting() {
  HPMode m = (this->wanted_settings_.mode != HPMode::UNKNOWN) ? this->wanted_settings_.mode : this->current_settings_.mode;
  return hp_mode_to_str(m);
}

const char *CN105Climate::get_power_setting() {
  HPPower p =
      (this->wanted_settings_.power != HPPower::UNKNOWN) ? this->wanted_settings_.power : this->current_settings_.power;
  return hp_power_to_str(p);
}

const char *CN105Climate::get_vane_setting() {
  HPVaneMode v =
      (this->wanted_settings_.vane != HPVaneMode::UNKNOWN) ? this->wanted_settings_.vane : this->current_settings_.vane;
  return hp_vane_to_str(v);
}

const char *CN105Climate::get_wide_vane_setting() {
  HPWideVaneMode wv = (this->wanted_settings_.wide_vane != HPWideVaneMode::UNKNOWN) ? this->wanted_settings_.wide_vane
                                                                                 : this->current_settings_.wide_vane;
  if (this->wanted_settings_.wide_vane != HPWideVaneMode::UNKNOWN) {
    if (this->wanted_settings_.wide_vane == HPWideVaneMode::AIRFLOW_CONTROL && !this->current_settings_.i_see) {
      wv = this->current_settings_.wide_vane;
    }
  }
  return hp_wide_vane_to_str(wv);
}

const char *CN105Climate::get_fan_speed_setting() {
  HPFanMode f = (this->wanted_settings_.fan != HPFanMode::UNKNOWN) ? this->wanted_settings_.fan : this->current_settings_.fan;
  return hp_fan_to_str(f);
}

float CN105Climate::get_temperature_setting() {
  if (this->wanted_settings_.temperature.has_value()) {
    return *this->wanted_settings_.temperature;
  } else {
    return this->current_settings_.temperature.value_or(this->target_temperature);
  }
}
const char *CN105Climate::get_airflow_control_setting() {
  HPAirflowControl ac = (this->wanted_run_states_.airflow_control != HPAirflowControl::UNKNOWN)
                            ? this->wanted_run_states_.airflow_control
                            : this->current_run_states_.airflow_control;
  return hp_airflow_control_to_str(ac);
}

void CN105Climate::create_packet(uint8_t *packet) {
  prepare_set_packet(packet, PACKET_LEN);

  // ESP_LOGD(TAG, "checking differences bw asked settings and current ones...");
  ESP_LOGD(TAG, "building packet for writing...");

  if (this->wanted_settings_.power != HPPower::UNKNOWN) {
    ESP_LOGD(TAG, "power -> %s", get_power_setting());
    auto val_opt = hp_power_to_wire(wanted_settings_.power);
    if (val_opt) {
      packet[8] = *val_opt;
      packet[6] += CONTROL_PACKET_1[0];
    } else {
      ESP_LOGW(TAG, "Ignoring invalid power setting while building packet");
    }
  }

  if (this->wanted_settings_.mode != HPMode::UNKNOWN) {
    ESP_LOGD(TAG, "heatpump mode -> %s", get_mode_setting());
    auto val_opt = hp_mode_to_wire(wanted_settings_.mode);
    if (val_opt) {
      packet[9] = *val_opt;
      packet[6] += CONTROL_PACKET_1[1];
    } else {
      ESP_LOGW(TAG, "Ignoring invalid mode setting while building packet");
    }
  }

  if (wanted_settings_.temperature.has_value()) {
    ESP_LOGD(TAG, "temperature -> %f", get_temperature_setting());
    packet[19] = cn105_protocol::encode_temperature_b(get_temperature_setting());
    packet[6] += CONTROL_PACKET_1[2];
  }

  if (this->wanted_settings_.fan != HPFanMode::UNKNOWN) {
    ESP_LOGD(TAG, "heatpump fan -> %s", get_fan_speed_setting());
    auto val_opt = hp_fan_to_wire(wanted_settings_.fan);
    if (val_opt) {
      packet[11] = *val_opt;
      packet[6] += CONTROL_PACKET_1[3];
    } else {
      ESP_LOGW(TAG, "Ignoring invalid fan setting while building packet");
    }
  }

  if (this->wanted_settings_.vane != HPVaneMode::UNKNOWN) {
    ESP_LOGD(TAG, "heatpump vane -> %s", get_vane_setting());
    auto val_opt = hp_vane_to_wire(wanted_settings_.vane);
    if (val_opt) {
      packet[12] = *val_opt;
      packet[6] += CONTROL_PACKET_1[4];
    } else {
      ESP_LOGW(TAG, "Ignoring invalid vane setting while building packet");
    }
  }

  if (this->wanted_settings_.wide_vane != HPWideVaneMode::UNKNOWN) {
    ESP_LOGD(TAG, "heatpump widevane -> %s", get_wide_vane_setting());
    auto val_opt = hp_wide_vane_to_wire(wanted_settings_.wide_vane);
    if (val_opt) {
      packet[18] = *val_opt | (this->wide_vane_adj_ ? 0x80 : 0x00);
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
          ESP_LOGD(TAG, "Split Vertical Vane: WideVane set to %s. Packet[12] (Vertical) is %02X",
                   get_wide_vane_setting(), packet[12]);
          break;
        case VaneType::STANDARD:
        default:
          // No special handling
          break;
      }
    } else {
      ESP_LOGW(TAG, "Ignoring invalid wide_vane setting while building packet");
    }
  }

  // add the checksum
  uint8_t chk_sum = check_sum(packet, 21);
  packet[21] = chk_sum;
  // ESP_LOGD(TAG, "debug before write packet:");
  // this->hp_packet_debug(packet, 22, "WRITE");
}

void CN105Climate::publish_wanted_settings_state_to_ha() {
  if ((this->wanted_settings_.mode != HPMode::UNKNOWN) || (this->wanted_settings_.power != HPPower::UNKNOWN)) {
    check_power_and_mode_settings(this->wanted_settings_, false);
    this->update_action();  // update action info on HA climate component
  }

  if (this->wanted_settings_.fan != HPFanMode::UNKNOWN) {
    check_fan_settings(this->wanted_settings_, false);
  }

  if ((this->wanted_settings_.vane != HPVaneMode::UNKNOWN) ||
      (this->wanted_settings_.wide_vane != HPWideVaneMode::UNKNOWN)) {
    if (this->wanted_settings_.vane == HPVaneMode::UNKNOWN) {  // to prevent a nullpointer error
      this->wanted_settings_.vane = this->current_settings_.vane;
    }
    if (this->wanted_settings_.wide_vane == HPWideVaneMode::UNKNOWN) {  // to prevent a nullpointer error
      this->wanted_settings_.wide_vane = this->current_settings_.wide_vane;
    }

    check_vane_settings(this->wanted_settings_, false);
  }

  // HA Temp — only update if this SET includes an explicit temperature change;
  // otherwise the stale current_settings_.temperature would overwrite the UI.
  if (this->wanted_settings_.temperature.has_value()) {
    this->update_target_temperatures_from_settings(this->get_temperature_setting());
  }

  if ((this->wanted_settings_.vane != HPVaneMode::UNKNOWN) ||
      (this->wanted_settings_.wide_vane != HPWideVaneMode::UNKNOWN)) {
    this->update_extra_select_components(this->wanted_settings_);
  }

  // publish to HA
  this->publish_state();
}

void CN105Climate::publish_wanted_run_states_state_to_ha() {
  if (this->wanted_run_states_.airflow_control != HPAirflowControl::UNKNOWN) {
    if (this->airflow_control_select_ != nullptr) {
      if (this->has_changed(this->airflow_control_select_->current_option(),
                            hp_airflow_control_to_str(this->wanted_run_states_.airflow_control),
                            "select airflow control")) {
        ESP_LOGI(TAG, "airflow control setting changed");
        this->airflow_control_select_->publish_state(hp_airflow_control_to_str(wanted_run_states_.airflow_control));
      }
    }
  }
}

void CN105Climate::send_wanted_settings_delegate() {
  this->wanted_settings_.has_been_sent = true;
  this->last_send_ = CUSTOM_MILLIS;
  this->last_wanted_settings_send_ms_ = CUSTOM_MILLIS;
  ESP_LOGI(TAG, "sending wanted_settings_..");
  this->debug_settings("wanted_settings_", wanted_settings_);
  // and then we send the update packet
  uint8_t packet[PACKET_LEN] = {};
  this->create_packet(packet);
  this->write_packet(packet, PACKET_LEN);
  this->hp_packet_debug(packet, 22, "WRITE_SETTINGS");

  this->publish_wanted_settings_state_to_ha();

  // as soon as the packet is sent, we reset the settings
  this->wanted_settings_.reset_settings();

  // as we've just sent a packet to the heatpump, we let it time for process
  // this might not be necessary but, we give it a try because of issue #32
  // https://github.com/echavet/MitsubishiCN105ESPHome/issues/32
  this->loop_cycle_.defer_cycle();
}

/**
 * builds and send all an update packet to the heatpump
 *
 *
 */
void CN105Climate::send_wanted_settings() {
  if (this->is_heatpump_connection_active() && this->is_uart_ready()) {
    if (CUSTOM_MILLIS - this->last_send_ > 300) {  // we don't want to send too many packets
      this->send_wanted_settings_delegate();
    } else {
      ESP_LOGD(TAG, "will send_wanted_settings later because we've sent one too recently...");
    }
  } else {
    this->reconnect_if_connection_lost();
  }
}

void CN105Climate::build_and_send_info_packet(uint8_t code) {
  uint8_t packet[PACKET_LEN] = {};
  create_info_packet(packet, code);
  this->write_packet(packet, PACKET_LEN);
}

void CN105Climate::build_and_send_requests_info_packets() {
  if (this->is_heatpump_connected()) {
    ESP_LOGV(LOG_UPD_INT_TAG, "triggering infopacket because of update interval tick");
    ESP_LOGV("CONTROL_WANTED_SETTINGS", "has_changed is %s", wanted_settings_.has_changed ? "true" : "false");
    this->loop_cycle_.cycle_started();
    this->nb_cycles_++;
    // Sends the first sendable request (the list is registered once at the constructor)
    this->scheduler_.send_next_after(0x00);  // 0x00 -> start, pick first eligible
  } else {
    this->reconnect_if_connection_lost();
  }
}

void CN105Climate::create_info_packet(uint8_t *packet, uint8_t code) {
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
  uint8_t chk_sum = check_sum(packet, 21);
  packet[21] = chk_sum;
}

void CN105Climate::send_remote_temperature_packet() {
  // Actual writer for the remote temperature (0x07) packet.
  // Rate limiting and 0.5°C change-detection live upstream in set_remote_temperature();
  // the keep-alive timer forces periodic resends. This function just builds and sends,
  // and is the single source of truth for the last-sent tracking fields.
  uint32_t now = CUSTOM_MILLIS;
  bool temp_changed = (this->remote_temperature_ != this->last_remote_temp_sent_);

  uint8_t packet[PACKET_LEN] = {};

  prepare_set_packet(packet, PACKET_LEN);

  packet[5] = 0x07;
  if (this->remote_temperature_.has_value()) {
    packet[6] = 0x01;
    float clamped_temp = std::max(8.0f, std::min(*this->remote_temperature_, 37.5f));
    cn105_protocol::encode_remote_temperature(clamped_temp, packet[7], packet[8]);
  } else {
    packet[8] = 0x80;  // MHK1 send 80, even though it could be 00, since ControlByte is 00
  }
  // add the checksum
  uint8_t chk_sum = check_sum(packet, 21);
  packet[21] = chk_sum;

  ESP_LOGD(LOG_REMOTE_TEMP, "Sending remote temperature packet... -> %.1f%s",
           this->remote_temperature_.value_or(NAN), temp_changed ? " (changed)" : " (keep-alive)");
  write_packet(packet, PACKET_LEN);

  // Cancel any outstanding deferred write since we just sent the latest value
  this->cancel_timeout("deferred_remote_temp_send");

  // Update send tracking (single source of truth for the upstream rate limiter)
  this->last_remote_temp_send_ms_ = now;
  this->last_remote_temp_sent_ = this->remote_temperature_;
}

void CN105Climate::send_remote_temperature() {
  this->should_send_external_temperature_ = false;
  this->remote_temp_pending_since_ms_ = 0;

  // Send the packet
  this->send_remote_temperature_packet();
}

bool CN105Climate::send_pending_remote_temperature_() {
  // Safety net. The remote temperature is normally written by terminate_cycle(), so anything
  // that stops info cycles also stops the heatpump learning the room temperature — it silently
  // falls back to its own internal sensor and can overshoot the setpoint by degrees. If the
  // queued write has been waiting far longer than a cycle should take, write it directly.
  if (!this->is_heatpump_connection_active() || !this->is_uart_ready()) {
    // Can't write right now. Restart the stall clock so this branch does not claim
    // every loop iteration — the write stays queued and the loop is free to start a
    // cycle (which is what re-establishes communication in the first place).
    this->remote_temp_pending_since_ms_ = CUSTOM_MILLIS;
    ESP_LOGD(LOG_REMOTE_TEMP, "Remote temperature write stalled but the link is down; retrying later");
    return false;
  }

  ESP_LOGW(LOG_REMOTE_TEMP, "Remote temperature write stalled (no cycle completed); sending it directly");
  this->send_remote_temperature();
  return true;
}

void CN105Climate::send_wanted_run_states() {
  uint8_t packet[PACKET_LEN] = {};

  prepare_set_packet(packet, PACKET_LEN);

  packet[5] = 0x08;
  if (this->wanted_run_states_.airflow_control != HPAirflowControl::UNKNOWN) {
    ESP_LOGD(TAG, "airflow control -> %s", get_airflow_control_setting());
    auto val_opt = hp_airflow_control_to_wire(wanted_run_states_.airflow_control);
    if (val_opt) {
      packet[11] = *val_opt;
      packet[6] += RUN_STATE_PACKET_1[4];
    }
  }

  // Add the checksum
  uint8_t chk_sum = check_sum(packet, 21);
  packet[21] = chk_sum;
  ESP_LOGD(LOG_SET_RUN_STATE, "Sending set run state package (0x08)");
  write_packet(packet, PACKET_LEN);

  this->publish_wanted_run_states_state_to_ha();

  this->wanted_run_states_.reset_settings();
  this->loop_cycle_.defer_cycle();
}
