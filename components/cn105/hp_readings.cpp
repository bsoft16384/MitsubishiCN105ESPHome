#include "cn105.h"

#include <map>

using namespace esphome;


/**
 * processInput: reads available bytes from UART and feeds them to the FrameParser.
 * When a complete frame is detected, delegates to process_data_packet().
 */

// A frame truncated mid-transmission leaves the parser waiting for bytes that never
// come, and the next frame would then be appended to the stale one. At 2400 baud 8E1
// a byte takes ~4.6 ms and the longest frame ~100 ms, so a gap this long between bytes
// of the same frame means the unit stopped talking and the parser must resync.
static const uint32_t FRAME_RESYNC_GAP_MS = 500;

bool CN105Climate::process_input(void) {
  bool processed = false;
  while (this->get_hw_serial_()->available()) {
    processed = true;
    uint8_t input_data;
    if (this->get_hw_serial_()->read_byte(&input_data)) {
      ESP_LOGV("Decoder", "--> %02X", input_data);
      const uint32_t now = CUSTOM_MILLIS;
      if (this->parser_.in_progress() && (now - this->last_rx_byte_ms_) > FRAME_RESYNC_GAP_MS) {
        ESP_LOGW("Decoder", "Discarding a truncated frame after a %lu ms gap",
                 (unsigned long) (now - this->last_rx_byte_ms_));
        this->parser_.reset();
      }
      this->last_rx_byte_ms_ = now;
      this->parser_.feed(input_data);
      if (this->parser_.frame_complete()) {
        this->process_data_packet();
        this->parser_.reset();
      }
    }
  }
  return processed;
}

/**
 * process_data_packet: called when the FrameParser has assembled a complete frame.
 * Validates checksum, sets the data pointer, and dispatches to processCommand().
 */
void CN105Climate::process_data_packet() {
  ESP_LOGV(TAG, "processing data packet...");

  // Point data at the payload section of the parser buffer
  // Note: cast away const because downstream code uses non-const data pointer
  this->data_ = const_cast<uint8_t *>(this->parser_.data());

  this->hp_packet_debug(this->parser_.raw(), this->parser_.frame_size(), "READ");

  // During handshake, log every received frame for diagnostics
  if (!this->is_heatpump_connected()) {
    ESP_LOGD(LOG_CONN_TAG, "RX during handshake (cmd=0x%02X len=%d)", this->parser_.command(),
             this->parser_.data_length());
    this->hp_packet_debug(this->parser_.raw(), this->parser_.frame_size(), LOG_CONN_TAG);
  }

  if (this->parser_.checksum_valid()) {
    ESP_LOGD("chk_sum", "OK");
    // checkpoint of a heatpump response
    this->last_response_ms_ = CUSTOM_MILLIS;

    // processing the specific command
    process_command();
  } else {
    ESP_LOGW("chk_sum", "KO -> checksum mismatch (cmd=0x%02X len=%d)", this->parser_.command(),
             this->parser_.data_length());
    if (!this->is_heatpump_connected()) {
      ESP_LOGD(LOG_CONN_TAG, "Checksum KO during handshake");
      this->hp_packet_debug(this->parser_.raw(), this->parser_.frame_size(), LOG_CONN_TAG);
    }
  }
}

uint8_t CN105Climate::get_payload_byte(int index, uint8_t default_val) const {
  if (index >= this->parser_.data_length()) {
    return default_val;
  }
  return this->data_[index];
}

void CN105Climate::get_power_from_response_packet() {
  auto decoded = cn105_decoder::decode_sub_modes(this->data_, this->parser_.data_length());
  if (!decoded) {
    ESP_LOGW("Decoder", "Power/Standby packet too short (%d < %d)", this->parser_.data_length(),
             cn105_decoder::POWER_MIN_LEN);
    return;
  }
  ESP_LOGD("Decoder", "[0x09 is sub modes]");

  // An unrecognised byte is not evidence of a change, so keep what we already know.
  const HPStage stage =
      cn105_decoder::resolve_or_keep(decoded->stage, this->current_settings_.stage, HPStage::IDLE);
  const HPSubMode sub_mode =
      cn105_decoder::resolve_or_keep(decoded->sub_mode, this->current_settings_.sub_mode, HPSubMode::NORMAL);
  const HPAutoSubMode auto_sub_mode = cn105_decoder::resolve_or_keep(
      decoded->auto_sub_mode, this->current_settings_.auto_sub_mode, HPAutoSubMode::AUTO_OFF);

  if (!decoded->stage)
    ESP_LOGW("Decoder", "Unknown stage byte 0x%02X — keeping previous value", decoded->stage_byte);
  if (!decoded->sub_mode)
    ESP_LOGW("Decoder", "Unknown sub_mode byte 0x%02X — keeping previous value", decoded->sub_mode_byte);
  if (!decoded->auto_sub_mode)
    ESP_LOGW("Decoder", "Unknown auto_sub_mode byte 0x%02X — keeping previous value", decoded->auto_sub_mode_byte);

  ESP_LOGD("Decoder", "[Stage : %s]", hp_stage_to_str(stage));
  ESP_LOGD("Decoder", "[Sub Mode  : %s]", hp_sub_mode_to_str(sub_mode));
  ESP_LOGD("Decoder", "[Auto Mode Sub Mode  : %s]", hp_auto_sub_mode_to_str(auto_sub_mode));

  // Always record the decoded state, whether or not the matching diagnostic sensor is
  // configured: current_settings_ drives update_action() and is what resolve_or_keep()
  // reads back as "previous value". Only the publish is conditional on the sensor.
  if (stage != this->current_settings_.stage) {
    this->current_settings_.stage = stage;
    if (this->stage_sensor_ != nullptr) {
      this->stage_sensor_->publish_state(hp_stage_to_str(stage));
    }
    // If using stage as operating fallback, update action immediately when stage changes
    // and publish to Home Assistant
    if (this->use_stage_for_operating_status_) {
      this->update_action();
      this->publish_state();
    }
  }
  if (sub_mode != this->current_settings_.sub_mode) {
    this->current_settings_.sub_mode = sub_mode;
    if (this->sub_mode_sensor_ != nullptr) {
      this->sub_mode_sensor_->publish_state(hp_sub_mode_to_str(sub_mode));
    }
  }
  if (auto_sub_mode != this->current_settings_.auto_sub_mode) {
    this->current_settings_.auto_sub_mode = auto_sub_mode;
    if (this->auto_sub_mode_sensor_ != nullptr) {
      this->auto_sub_mode_sensor_->publish_state(hp_auto_sub_mode_to_str(auto_sub_mode));
    }
  }
}

void CN105Climate::get_settings_from_response_packet() {
  auto decoded = cn105_decoder::decode_settings(this->data_, this->parser_.data_length());
  if (!decoded) {
    ESP_LOGW("Decoder", "Settings packet too short (%d < %d)", this->parser_.data_length(),
             cn105_decoder::SETTINGS_MIN_LEN);
    return;
  }
  ESP_LOGD("Decoder", "[0x02 is settings]");

  HeatpumpSettings received_settings{};

  received_settings.power =
      cn105_decoder::resolve_or_keep(decoded->power, this->current_settings_.power, HPPower::OFF);
  if (!decoded->power)
    ESP_LOGW("Decoder", "Unknown power byte 0x%02X — keeping previous value", decoded->power_byte);

  received_settings.i_see = decoded->i_see;
  received_settings.mode = cn105_decoder::resolve_or_keep(decoded->mode, this->current_settings_.mode, HPMode::FAN);
  if (!decoded->mode) {
    ESP_LOGW("Decoder", "Unknown mode byte 0x%02X — keeping previous value", decoded->mode_byte);
  } else if (received_settings.mode == HPMode::AUTO) {
    // The unit's own AUTO mode has no Home Assistant equivalent this component
    // exposes, so report it as FAN_ONLY. Logged once: the condition persists for
    // as long as the unit stays in AUTO, i.e. every settings response.
    if (!this->auto_mode_mapping_logged_) {
      this->auto_mode_mapping_logged_ = true;
      ESP_LOGI("Decoder", "Unit is in its own AUTO mode — reporting it as FAN to Home Assistant");
    }
    received_settings.mode = HPMode::FAN;
  }

  ESP_LOGD("Decoder", "[Power : %s]", hp_power_to_str(received_settings.power));
  ESP_LOGD("Decoder", "[i_see  : %d]", received_settings.i_see);
  ESP_LOGD("Decoder", "[Mode  : %s]", hp_mode_to_str(received_settings.mode));

  if (decoded->legacy_temperature_encoding) {
    // Logged once: an affected unit reports data[11] == 0x00 in every settings response.
    if (!this->legacy_temp_encoding_logged_) {
      this->legacy_temp_encoding_logged_ = true;
      ESP_LOGW("Decoder", "Legacy temperature encoding detected! This unit does not support high-precision target "
                          "temperature (data[11] is 0x00).");
    }
    received_settings.temperature = this->current_settings_.temperature;
  } else {
    received_settings.temperature = decoded->temperature;
  }
  ESP_LOGD("Decoder", "[Temp °C: %f]", received_settings.temperature.value_or(NAN));

  received_settings.fan = cn105_decoder::resolve_or_keep(decoded->fan, this->current_settings_.fan, HPFanMode::AUTO);
  if (!decoded->fan)
    ESP_LOGW("Decoder", "Unknown fan byte 0x%02X — keeping previous value", decoded->fan_byte);
  ESP_LOGD("Decoder", "[Fan: %s]", hp_fan_to_str(received_settings.fan));

  received_settings.vane = cn105_decoder::resolve_or_keep(decoded->vane, this->current_settings_.vane, HPVaneMode::AUTO);
  if (!decoded->vane)
    ESP_LOGW("Decoder", "Unknown vane byte 0x%02X — keeping previous value", decoded->vane_byte);
  ESP_LOGD("Decoder", "[Vane: %s]", hp_vane_to_str(received_settings.vane));

  // wide_vane is only tracked when the unit reports one and this config exposes it.
  if (decoded->wide_vane_present && (this->traits_.supports_swing_mode(climate::CLIMATE_SWING_HORIZONTAL) ||
                                     this->horizontal_vane_select_ != nullptr)) {
    received_settings.wide_vane = cn105_decoder::resolve_or_keep(decoded->wide_vane, this->current_settings_.wide_vane,
                                                                 HPWideVaneMode::CENTER);
    if (!decoded->wide_vane)
      ESP_LOGW("Decoder", "Unknown wide_vane byte 0x%02X — keeping previous value", decoded->wide_vane_byte);
    this->wide_vane_adj_ = decoded->wide_vane_adjustment;
    ESP_LOGD("Decoder", "[wide_vane: %s (adj:%d)]", hp_wide_vane_to_str(received_settings.wide_vane),
             this->wide_vane_adj_);
  } else {
    ESP_LOGD("Decoder", "widevane is not supported");
  }

  if (this->isee_sensor_ != nullptr) {
    this->isee_sensor_->publish_state(received_settings.i_see);
  }

  // Target humidity (data[12]) is only populated by some premium models (e.g. MSZ-LN).
  if (this->target_humidity_sensor_ != nullptr) {
    if (decoded->target_humidity.has_value()) {
      const float humidity_pct = static_cast<float>(*decoded->target_humidity);
      if (this->target_humidity_sensor_->get_raw_state() != humidity_pct) {
        ESP_LOGD("Decoder", "[Target Humidity: %.0f%%]", humidity_pct);
        this->target_humidity_sensor_->publish_state(humidity_pct);
      }
    } else if (decoded->humidity_byte != 0) {
      ESP_LOGD("Decoder", "[Target Humidity byte out of range: 0x%02X]", decoded->humidity_byte);
    }
  }

  if (this->airflow_control_select_ != nullptr) {
    if (decoded->airflow_control_reported && !decoded->airflow_control.has_value()) {
      ESP_LOGW("Decoder", "Unknown airflow_control byte 0x%02X — keeping previous value", decoded->airflow_byte);
    }
    const HPAirflowControl airflow = decoded->airflow_control.value_or(this->current_run_states_.airflow_control);
    if (airflow != this->current_run_states_.airflow_control) {
      this->current_run_states_.airflow_control = airflow;
      this->publish_select_option_(this->airflow_control_select_, hp_airflow_control_to_str(airflow),
                                   "airflow control");
    }
  }

  this->heatpump_update(received_settings);
}

void CN105Climate::get_room_temperature_from_response_packet() {
  //                  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
  //  FC 62 01 30 10 03 00 00 0E 00 94 B0 B0 FE 42 00 01 0A 64 00 00 A9
  //                          RT    OT RT SP ?? ?? ?? RM RM RM
  //  RT = room temperature (in old format and in new format)
  //  OT = outside air temperature
  //  SP = room setpoint temperature?
  //  RM = indoor unit operating time in minutes
  auto decoded = cn105_decoder::decode_room_temperature(this->data_, this->parser_.data_length());
  if (!decoded) {
    ESP_LOGW("Decoder", "Room temperature packet too short (%d < %d)", this->parser_.data_length(),
             cn105_decoder::ROOM_TEMP_MIN_LEN);
    return;
  }

  HeatpumpStatus received_status{};
  received_status.outside_air_temperature = decoded->outside_air_temperature;

  if (decoded->room_temperature.has_value()) {
    received_status.room_temperature = *decoded->room_temperature;
    ESP_LOGD(LOG_TEMP_SENSOR_TAG, "%s --> [Room °C: %f]", decoded->room_temperature_is_legacy ? "data[3] map" : "data[6]",
             received_status.room_temperature);
  } else {
    ESP_LOGW("Decoder", "Unknown room_temp byte 0x%02X — keeping previous value", decoded->legacy_room_temp_byte);
    received_status.room_temperature = this->current_status_.room_temperature;
  }

  // Issue 290: the unit never says which sensor it is using, so infer it by comparing
  // the value it echoes back against the remote temperature we are feeding it.
  const bool using_remote = cn105_decoder::unit_adopted_remote_temperature(
      received_status.room_temperature, this->remote_temperature_, this->remote_temp_keepalive_active_,
      this->remote_temp_margin_);

  if (this->current_temp_source_sensor_ != nullptr) {
    const char *source = using_remote ? "Remote" : "Internal";
    if (this->current_temp_source_sensor_->state != source) {
      this->current_temp_source_sensor_->publish_state(source);
    }
  }

  // When the unit is using our remote temperature, report the full-precision value we
  // sent rather than the 0.5°C-quantized reading it echoes back.
  if (using_remote) {
    received_status.room_temperature = *this->remote_temperature_;
  }

  received_status.runtime_hours = decoded->runtime_hours.value_or(this->current_status_.runtime_hours);

  ESP_LOGD("Decoder", "[Room °C: %f]", received_status.room_temperature);
  ESP_LOGD("Decoder", "[OAT  °C: %f]", received_status.outside_air_temperature);

  // no change with this packet to current_status_ for operating and compressor_frequency
  received_status.operating = current_status_.operating;
  received_status.compressor_frequency = current_status_.compressor_frequency;
  received_status.input_power = current_status_.input_power;
  received_status.kwh = current_status_.kwh;
  this->status_changed(received_status);
}

void CN105Climate::get_operating_and_compressor_freq_from_response_packet() {
  // FC 62 01 30 10 06 00 00 1A 01 00 00 00 00 00 00 00 00 00 00 00 3C
  // MSZ-RW25VGHZ-SC1 / MUZ-RW25VGHZ-SC1
  // FC 62 01 30 10 06 00 00 00 01 00 08 05 50 00 00 42 00 00 00 00 B7
  //                            OP IP IP EU EU       ??
  //  OP = operating status (1 = compressor running, 0 = standby)
  //  IP = Current input power in Watts (16-bit decimal)
  //  EU = energy usage (used energy in kwh = value/10)
  //  ?? = unknown bytes that appear to have a fixed/constant value
  auto decoded = cn105_decoder::decode_status(this->data_, this->parser_.data_length());
  if (!decoded) {
    ESP_LOGW("Decoder", "Status packet too short (%d < %d)", this->parser_.data_length(),
             cn105_decoder::STATUS_MIN_LEN);
    return;
  }
  ESP_LOGD("Decoder", "[0x06 is status]");

  HeatpumpStatus received_status{};
  received_status.operating = decoded->operating;
  received_status.compressor_frequency = decoded->compressor_frequency;
  received_status.input_power = this->convert_input_power_to_w(static_cast<float>(decoded->raw_input_power));
  received_status.kwh = this->convert_energy_usage_to_kwh(static_cast<float>(decoded->raw_energy_usage));

  // no change with this packet to room_temperature
  received_status.room_temperature = current_status_.room_temperature;
  received_status.outside_air_temperature = current_status_.outside_air_temperature;
  received_status.runtime_hours = current_status_.runtime_hours;
  this->status_changed(received_status);
}

void CN105Climate::terminate_cycle() {
  if (this->should_send_external_temperature_) {
    // We will receive ACK packet for this.
    // Sending WantedSettings must be delayed in this case (last_send_ timestamp updated).
    ESP_LOGD(LOG_REMOTE_TEMP, "Sending remote temperature...");
    this->send_remote_temperature();
  }

  this->loop_cycle_.cycle_ended();

  if (this->hp_uptime_connection_sensor_ != nullptr) {
    // if the uptime connection sensor is configured
    // we trigger  manual update at the end of a cycle.
    this->hp_uptime_connection_sensor_->update();
  }

  this->nb_complete_cycles_++;
}
void CN105Climate::get_error_info_from_response_packet() {
  auto decoded = cn105_decoder::decode_error_info(this->data_, this->parser_.data_length());
  if (!decoded) {
    ESP_LOGW("Decoder", "Error info packet too short (%d < %d)", this->parser_.data_length(),
             cn105_decoder::ERROR_INFO_MIN_LEN);
    return;
  }
  ESP_LOGD("Decoder", "0x04 error info");
  if (this->error_code_sensor_ != nullptr) {
    if (decoded->no_error) {
      this->error_code_sensor_->publish_state("No Error");
    } else {
      char buf[32];
      snprintf(buf, sizeof(buf), "Error 0x%02X sub 0x%02X", decoded->code, decoded->sub_code);
      this->error_code_sensor_->publish_state(buf);
    }
  }
}

void CN105Climate::get_data_from_response_packet() {
  // First, let the orchestrator process the known codes
  const uint8_t code = get_payload_byte(0);
  if (this->scheduler_.process_response(code)) {
    return;
  }
  // Otherwise, switch for cases not handled by the orchestrator
  switch (code) {
    case 0x04:
      // Handled by orchestrator (r_error_info on_response → getErrorInfoFromResponsePacket)
      // Reaching here means the scheduler did not intercept this response — unexpected
      ESP_LOGW("Decoder", "[0x04] reached switch fallback — should have been handled by orchestrator");
      break;  // orchestrator

    case 0x05:
      /* timer packet */
      ESP_LOGW("Decoder", "[0x05 is Timer : not implemented]");
      // this->last_received_packet_sensor->publish_state("0x62-> 0x05: Data -> Timer Packet");
      break;

    case 0x06:
      break;  // orchestrator
    case 0x09:
      break;  // orchestrator

    case 0x10:
      ESP_LOGD("Decoder", "[0x10 is Unknown : not implemented]");
      // this->get_auto_mode_state_from_response_packet();
      break;

    case 0x20:  // fallthrough
    case 0x22:
      break;  // orchestrator

    default:
      ESP_LOGW("Decoder", "packet type [%02X] <-- unknown and unexpected", get_payload_byte(0));
      // this->last_received_packet_sensor->publish_state("0x62-> ?? : Data -> Unknown");
      break;
  }
}

void CN105Climate::process_command() {
  switch (this->parser_.command()) {
    case 0x61: /* last update was successful */
      // We have no way to tell whether this acknowledges an external temp packet
      // or a wanted_settings_ update, so there is nothing more to derive from it.
      this->hp_packet_debug(this->parser_.raw(), this->parser_.frame_size(), LOG_ACK);
      // If a function-set part 2 is waiting on the ACK of part 1, send it now
      this->send_pending_functions_packet2();
      break;

    case 0x62: /* packet contains data (room °C, settings, timer, status, or functions...)*/
      this->get_data_from_response_packet();
      break;
    case 0x7a:  // Connection success (User / standard)
    case 0x7b:  // Connection success (Installer / extended)
      // Log as INFO on the dedicated tag, details as DEBUG via hp_packet_debug
      ESP_LOGI(LOG_CONN_TAG, "--> Heatpump did reply: connection success (%s, 0x%02X)! <--",
               (this->parser_.command() == 0x7b) ? "Installer" : "User", this->parser_.command());
      this->hp_packet_debug(this->parser_.raw(), this->parser_.frame_size(), LOG_CONN_TAG);
      // isHeatpumpConnected_ replaced by FSM transition in set_heatpump_connected()
      this->set_heatpump_connected(true);
      // let's say that the last complete cycle was over now (also clears any cycle
      // left running by the connection that just dropped, and re-arms starvation detection)
      this->loop_cycle_.init();
      // current_settings_ is cleared below, so the next evaluation must be free to
      // command the unit rather than being held by the settle window of a command
      // issued to the previous connection.
      this->has_issued_command_ = false;
      this->current_settings_.reset_settings();  // each time we connect, we need to reset current setting to force a
                                              // complete sync with ha component state and receievdSettings
      this->current_run_states_.reset_settings();
      break;
    default:
      break;
  }
}

void CN105Climate::status_changed(const HeatpumpStatus &status) {
  if (status != current_status_) {
    this->debug_status("received", status);
    this->debug_status("current", current_status_);

    this->current_status_.operating = status.operating;
    this->current_status_.compressor_frequency = status.compressor_frequency;
    this->current_status_.input_power = status.input_power;
    this->current_status_.kwh = status.kwh;
    this->current_status_.runtime_hours = status.runtime_hours;
    this->current_status_.room_temperature = status.room_temperature;
    this->current_status_.outside_air_temperature = status.outside_air_temperature;
    this->set_current_temperature(this->current_status_.room_temperature);

    this->update_action();  // update action info on HA climate component
    this->publish_state();

    if (this->compressor_frequency_sensor_ != nullptr) {
      this->compressor_frequency_sensor_->publish_state(current_status_.compressor_frequency);
    }

    if (this->input_power_sensor_ != nullptr) {
      this->input_power_sensor_->publish_state(current_status_.input_power);
    }

    if (this->kwh_sensor_ != nullptr) {
      this->kwh_sensor_->publish_state(current_status_.kwh);
    }

    if (this->runtime_hours_sensor_ != nullptr) {
      this->runtime_hours_sensor_->publish_state(current_status_.runtime_hours);
    }

    if (this->outside_air_temperature_sensor_ != nullptr) {
      this->outside_air_temperature_sensor_->publish_state(current_status_.outside_air_temperature);
    }
  }  // else no change
}

void CN105Climate::publish_state_to_ha(HeatpumpSettings &settings) {
  if ((this->wanted_settings_.mode == HPMode::UNKNOWN) &&
      (this->wanted_settings_.power == HPPower::UNKNOWN)) {  // to prevent overwriting a user demand
    check_power_and_mode_settings(settings);
  }

  this->update_action();  // update action info on HA climate component

  if (this->wanted_settings_.fan == HPFanMode::UNKNOWN) {  // to prevent overwriting a user demand
    check_fan_settings(settings);
  }

  if (this->wanted_settings_.vane == HPVaneMode::UNKNOWN) {  // to prevent overwriting a user demand
    check_vane_settings(settings);
  }

  if (this->wanted_settings_.wide_vane == HPWideVaneMode::UNKNOWN) {  // to prevent overwriting a user demand
    check_wide_vane_settings(settings);
  }

  // HA Temp
  // Temporarily ignore an incoming setpoint if a user setpoint change is in progress
  bool has_pending_user_temp = (this->wanted_settings_.temperature.has_value()) && (this->wanted_settings_.has_changed) &&
                            (!this->wanted_settings_.has_been_sent);
  uint32_t grace_window_ms = this->get_update_interval() + DEFER_SCHEDULE_UPDATE_LOOP_DELAY;
  bool grace_after_send = (this->last_wanted_settings_send_ms_ != 0) &&
                          ((CUSTOM_MILLIS - this->last_wanted_settings_send_ms_) < grace_window_ms);
  if (!has_pending_user_temp && !grace_after_send) {
    if (!this->wanted_settings_.temperature.has_value()) {  // to prevent overwriting a user demand
      if (settings.temperature.has_value()) {
        this->update_target_temperatures_from_settings(*settings.temperature);
      }
      this->current_settings_.temperature = settings.temperature;
    }
  } else {
    ESP_LOGD(LOG_SETTINGS_TAG, "Ignoring incoming setpoint due to pending user change or grace window");
  }

  this->current_settings_.i_see = settings.i_see;

  this->current_settings_.connected = true;

  this->mode = this->desired_mode_;
  this->target_temperature = this->desired_temp_;

  this->update_extra_select_components(settings);

  // publish to HA
  this->publish_state();
}

void CN105Climate::heatpump_update(HeatpumpSettings &settings) {
  // settings correponds to current settings
  ESP_LOGV(LOG_SETTINGS_TAG, "Settings received");
  // if received settings are different from current settings
  if (settings != this->current_settings_) {
    ESP_LOGI(LOG_SETTINGS_TAG, "Settings changed, updating HA states");
    this->debug_settings("current", this->current_settings_);
    this->debug_settings("received", settings);
    this->debug_settings("wanted", this->wanted_settings_);
    this->debug_climate("climate");
    this->publish_state_to_ha(settings);
  }
}

void CN105Climate::check_vane_settings(HeatpumpSettings &settings, bool update_current_settings) {
  if (this->has_changed(current_settings_.vane, settings.vane, "vane")) {  // widevane setting change ?
    ESP_LOGI(LOG_SETTINGS_TAG, "vane setting changed");

    // this->debugSettings("settings", settings);

    if (update_current_settings) {
      // ESP_LOGD(LOG_SETTINGS_TAG, "updating currentSetting with new value");
      current_settings_.vane = settings.vane;
    }

    if (settings.vane == HPVaneMode::SWING) {
      if (current_settings_.wide_vane == HPWideVaneMode::SWING) {
        this->swing_mode = climate::CLIMATE_SWING_BOTH;
      } else {
        this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
      }
    } else {
      if (current_settings_.wide_vane == HPWideVaneMode::SWING) {
        this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
      } else {
        this->swing_mode = climate::CLIMATE_SWING_OFF;
      }
    }
    ESP_LOGD(LOG_SETTINGS_TAG, "Swing mode is: %i", this->swing_mode);
  }
}

void CN105Climate::check_wide_vane_settings(HeatpumpSettings &settings, bool update_current_settings) {
  /* ******** HANDLE MITSUBISHI VANE CHANGES ********
   * VANE_MAP[7]        = {"AUTO", "1", "2", "3", "4", "5", "SWING"};
   * WIDEVANE_MAP[8]   = { "<<", "<",  "|",  ">",  ">>", "<>", "SWING", "AIRFLOW CONTROL" }
   */

  if (this->has_changed(current_settings_.wide_vane, settings.wide_vane, "wide_vane")) {  // widevane setting change ?
    ESP_LOGI(TAG, "widevane setting changed");
    this->debug_settings("settings", settings);

    // here I hope that the vane and widevane are always sent together
    if (update_current_settings) {
      current_settings_.wide_vane = settings.wide_vane;
    }

    if (settings.wide_vane == HPWideVaneMode::SWING) {
      if (current_settings_.vane == HPVaneMode::SWING) {
        this->swing_mode = climate::CLIMATE_SWING_BOTH;
      } else {
        this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
      }
    } else {
      if (current_settings_.vane == HPVaneMode::SWING) {
        this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
      } else {
        this->swing_mode = climate::CLIMATE_SWING_OFF;
      }
    }
    ESP_LOGD(TAG, "Swing mode is: %i", this->swing_mode);
  }
}
void CN105Climate::update_extra_select_components(HeatpumpSettings &settings) {
  // An UNKNOWN vane value (undecoded or unrecognized byte) would publish the literal
  // string "UNKNOWN", which is not in the select's option list — skip it.
  if (this->vertical_vane_select_ != nullptr && settings.vane != HPVaneMode::UNKNOWN) {
    if (this->has_changed(this->vertical_vane_select_->current_option(), hp_vane_to_str(settings.vane),
                          "select vane")) {
      ESP_LOGI(TAG, "vane setting (extra select component) changed");
      this->publish_select_option_(this->vertical_vane_select_, hp_vane_to_str(settings.vane), "vane");
    }
  }
  if (this->horizontal_vane_select_ != nullptr && settings.wide_vane != HPWideVaneMode::UNKNOWN) {
    if (this->has_changed(this->horizontal_vane_select_->current_option(), hp_wide_vane_to_str(settings.wide_vane),
                          "select wide_vane")) {
      ESP_LOGI(TAG, "widevane setting (extra select component) changed");
      this->publish_select_option_(this->horizontal_vane_select_, hp_wide_vane_to_str(settings.wide_vane), "wide_vane");
    }
  }
}
void CN105Climate::check_fan_settings(HeatpumpSettings &settings, bool update_current_settings) {
  /*
   * ******* HANDLE FAN CHANGES ********
   *
   * const char* FAN_MAP[6]         = {"AUTO", "QUIET", "1", "2", "3", "4"};
   */
  // current_settings_.fan== NULL is true when it is the first time we get en answer from hp

  if (this->has_changed(current_settings_.fan, settings.fan, "fan")) {  // fan setting change ?
    ESP_LOGI(TAG, "fan setting changed");
    if (update_current_settings) {
      current_settings_.fan = settings.fan;
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
    } else {  // case "AUTO" or default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
    }
    if (this->fan_mode.has_value()) {
      ESP_LOGD(TAG, "Fan mode is: %i", static_cast<int>(this->fan_mode.value()));
    } else {
      ESP_LOGD(TAG, "Fan mode is not set");
    }
  }
}

void CN105Climate::check_power_and_mode_settings(HeatpumpSettings &settings, bool update_current_settings) {
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
    }
    // HPMode::AUTO never reaches here: the decoder maps it to HPMode::FAN.
  }

  if (update_current_settings) {
    current_settings_.power = settings.power;
    current_settings_.mode = settings.mode;
  }

  if (!this->first_real_state_received_) {
    this->first_real_state_received_ = true;
    ESP_LOGI(TAG, "First physical climate settings received: power=%s, mode=%s, temp=%.1f",
             hp_power_to_str(settings.power), hp_mode_to_str(settings.mode), settings.temperature.value_or(NAN));

    bool fan_stop_state = this->fan_stop_switch_ != nullptr ? this->fan_stop_switch_->state : false;
    bool preserve_restored_mode =
        fan_stop_state &&
        (this->desired_mode_ == climate::CLIMATE_MODE_HEAT || this->desired_mode_ == climate::CLIMATE_MODE_COOL) &&
        (physical_mode == climate::CLIMATE_MODE_OFF);
    if (preserve_restored_mode) {
      ESP_LOGI(TAG, "Preserving restored desired mode (%s) because physical unit is OFF under Fan Stop.",
               LOG_STR_ARG(climate::climate_mode_to_string(this->desired_mode_)));
      this->mode = this->desired_mode_;
      this->target_temperature = this->desired_temp_;
      this->last_commanded_real_mode_ = climate::CLIMATE_MODE_OFF;
      this->last_commanded_real_temp_ = this->desired_temp_;
    } else {
      this->desired_mode_ = physical_mode;
      this->mode = physical_mode;
      if (settings.temperature.has_value()) {
        this->desired_temp_ = *settings.temperature;
        this->target_temperature = *settings.temperature;
      }
      this->last_commanded_real_mode_ = physical_mode;
      this->last_commanded_real_temp_ = settings.temperature.value_or(NAN);
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
             LOG_STR_ARG(climate::climate_mode_to_string(this->last_commanded_real_mode_)),
             LOG_STR_ARG(climate::climate_mode_to_string(physical_mode)));

    if (this->fan_stop_switch_ != nullptr && this->fan_stop_switch_->state) {
      this->fan_stop_switch_->turn_off();
    }

    this->desired_mode_ = physical_mode;
    this->mode = physical_mode;
    if (settings.temperature.has_value()) {
      this->desired_temp_ = *settings.temperature;
      this->target_temperature = *settings.temperature;
    }

    this->last_commanded_real_mode_ = physical_mode;
    this->last_commanded_real_temp_ = settings.temperature.value_or(NAN);

    this->publish_state();
  } else if (physical_mode != climate::CLIMATE_MODE_OFF && settings.temperature.has_value() &&
             fabsf(*settings.temperature - this->last_commanded_real_temp_) >= 0.25f) {
    ESP_LOGI(TAG, "External target temperature change detected: from %.1f to %.1f.", this->last_commanded_real_temp_,
             *settings.temperature);

    this->desired_temp_ = *settings.temperature;
    this->target_temperature = *settings.temperature;
    this->last_commanded_real_temp_ = *settings.temperature;

    this->publish_state();
    this->evaluate_fan_stop_and_ltp();
  } else {
    // No external override. Force the HA component state to remain aligned with user desired settings.
    this->mode = this->desired_mode_;
    this->target_temperature = this->desired_temp_;
  }
}
