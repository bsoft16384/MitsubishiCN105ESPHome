
#include "cn105.h"

using namespace esphome;

const char *esphome::driver_state_to_str(DriverState s) {
  switch (s) {
    case DriverState::BOOT:
      return "BOOT";
    case DriverState::WAIT_WIFI:
      return "WAIT_WIFI";
    case DriverState::WAIT_GRACE:
      return "WAIT_GRACE";
    case DriverState::CONNECTING:
      return "CONNECTING";
    case DriverState::CONNECTED:
      return "CONNECTED";
    case DriverState::DISCONNECTED:
      return "DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

void CN105Climate::transition_to_(DriverState next) {
  if (state_ == next)
    return;
  ESP_LOGI("FSM", "State: %s -> %s", driver_state_to_str(state_), driver_state_to_str(next));
  state_ = next;
}

CN105Climate::CN105Climate(uart::UARTComponent *uart)
    : UARTDevice(uart),
      scheduler_(
          // send callback: send a packet via build_and_send_info_packet
          [this](uint8_t code) { this->build_and_send_info_packet(code); },
          // timeout_callback: uses set_timeout from component
          [this](const std::string &name, uint32_t timeout_ms, std::function<void()> callback) {
            this->set_timeout(name.c_str(), timeout_ms, std::move(callback));
          },
          // terminate_callback: completes the cycle
          [this]() { this->terminate_cycle(); },
          // context_callback: Returns 'this' for the 'can_send' and 'on_response' callbacks.
          [this]() -> CN105Climate * { return this; }) {
  // Enables feature flags via the modern API (avoids deprecated setters).
  this->traits_.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION | climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  // supports_two_point_target_temperature will be defined in setup() depending on the supported modes
  this->traits_.set_visual_min_temperature(ESPMHP_MIN_TEMPERATURE);
  this->traits_.set_visual_max_temperature(ESPMHP_MAX_TEMPERATURE);
  this->traits_.set_visual_temperature_step(ESPMHP_TEMPERATURE_STEP);

  // state_ is initialized to BOOT in the header
  this->wide_vane_adj_ = false;
  this->functions = HeatpumpFunctions();
  this->auto_update_ = false;
  this->first_run_ = true;
  this->external_update_ = false;
  this->last_send_ = 0;
  this->info_mode_ = 0;
  this->last_connect_rq_time_ms_ = 0;
  // current_status_ fields are now default-initialized via HeatpumpStatus struct defaults

  this->horizontal_vane_select_ = nullptr;
  this->vertical_vane_select_ = nullptr;
  this->airflow_control_select_ = nullptr;
  this->compressor_frequency_sensor_ = nullptr;
  this->target_humidity_sensor_ = nullptr;
  this->input_power_sensor_ = nullptr;
  this->kwh_sensor_ = nullptr;
  this->runtime_hours_sensor_ = nullptr;

  this->air_purifier_switch_ = nullptr;
  this->night_mode_switch_ = nullptr;
  this->circulator_switch_ = nullptr;

  this->power_request_without_responses_ = 0;  // power request is not supported by all heatpump #112

  this->remote_temp_timeout_ = 4294967295;  // uint32_t max
  this->loop_cycle_.init();
  this->wanted_settings_.reset_settings();
  this->wanted_run_states_.reset_settings();

  // Register info requests moved to setup() to ensure hardware_settings_ are populated
}

void CN105Climate::register_info_requests() {
  scheduler_.clear_requests();

  // 0x02 Settings
  InfoRequest r_settings("settings", "Settings", 0x02, 3, 0);
  r_settings.on_response = [this](CN105Climate &self) {
    (void) self;
    this->get_settings_from_response_packet();
  };
  scheduler_.register_request(r_settings);

  // 0x03 Room temperature
  InfoRequest r_room("room_temp", "Room temperature", 0x03, 3, 0);
  r_room.on_response = [this](CN105Climate &self) {
    (void) self;
    this->get_room_temperature_from_response_packet();
  };
  scheduler_.register_request(r_room);

  // 0x06 Status
  InfoRequest r_status("status", "Status", 0x06, 3, 0);
  r_status.on_response = [this](CN105Climate &self) {
    (void) self;
    this->get_operating_and_compressor_freq_from_response_packet();
  };
  scheduler_.register_request(r_status);

  // 0x09 Standby/Power
  InfoRequest r_power("standby", "Power/Standby", 0x09, 3, 500);
  r_power.on_response = [this](CN105Climate &self) {
    (void) self;
    this->get_power_from_response_packet();
  };
  scheduler_.register_request(r_power);

  // 0x42 HVAC options
  InfoRequest r_hvac_opts("hvac_options", "HVAC options", 0x42, 3, 500);
  r_hvac_opts.can_send = [this](const CN105Climate &self) {
    (void) self;
    return (this->air_purifier_switch_ != nullptr || this->night_mode_switch_ != nullptr ||
            this->circulator_switch_ != nullptr);
  };
  r_hvac_opts.on_response = [this](CN105Climate &self) {
    (void) self;
    this->get_hvac_options_from_response_packet();
  };
  scheduler_.register_request(r_hvac_opts);

  // Placeholders
  InfoRequest r_error_info("error_info", "Error Info", 0x04, 3, 0);
  r_error_info.on_response = [this](CN105Climate &self) {
    (void) self;
    this->get_error_info_from_response_packet();
  };
  scheduler_.register_request(r_error_info);

  InfoRequest r_timers("timers", "Timers", 0x05, 1, 0);
  r_timers.disabled = true;
  scheduler_.register_request(r_timers);

  // Call to the new dedicated method.
  this->register_hardware_settings_requests();
}

void CN105Climate::register_hardware_settings_requests() {
  uint32_t interval = 0;
  bool is_enabled = false;

  if (!this->hardware_settings_.empty()) {
    ESP_LOGI(LOG_FUNCTIONS_TAG, "Registering function settings requests (0x20/0x22) with interval %u ms",
             this->hardware_settings_interval_ms_);
    interval = this->hardware_settings_interval_ms_;
    is_enabled = true;
  } else {
    ESP_LOGI(LOG_FUNCTIONS_TAG, "Registering function settings requests (0x20/0x22), disabled");
  }

  // Helper Lambda: Checks for incompatibility and disables everything if necessary.
  auto check_and_disable = [](CN105Climate &self, uint8_t code) -> bool {
    if (self.data_[0] != code)
      return false;

    bool all_zeros = true;
    // On some units (e.g. SEZ), codes may be present with a value of zero as long as the session
    // is not in installer mode. The presence of the byte (code+value) just validate the support.
    for (int i = 1; i < self.parser_.data_length(); i++) {
      if (self.data_[i] != 0) {
        all_zeros = false;
        break;
      }
    }

    if (all_zeros) {
      ESP_LOGW(LOG_FUNCTIONS_TAG, "Response 0x%02X contains only zeros. Feature not supported by unit. Disabling.",
               code);

      // 1. Do activate the request via the scheduler.
      self.scheduler_.disable_request(code);

      // 2. Mark graphics components as failed (unavailable).
      ESP_LOGD(LOG_FUNCTIONS_TAG, "Marking Hardware Setting Selects as failed.");
      for (auto *setting : self.hardware_settings_) {
        setting->set_enabled(false);
      }

      return false;
    }

    // If no hardware settings are defined in YAML this was a manual request
    // that is expected to run once, disable future requests.
    if (self.hardware_settings_.empty()) {
      self.scheduler_.disable_request(code);
    }

    return true;
  };

  // --- Part 1 (0x20) ---
  InfoRequest r_funcs1("functions1", "Functions Part 1", 0x20, 3, 0, interval, LOG_FUNCTIONS_TAG);
  r_funcs1.on_response = [this, check_and_disable](CN105Climate &self) {
    // Log the raw packet and decoded pairs even if the unit returns all zeros
    self.hp_packet_debug(self.data_, self.parser_.data_length(), "RX 0x20");
    self.hp_functions_debug(self.data_, self.parser_.data_length());
    if (check_and_disable(self, 0x20)) {
      if (self.parser_.data_length() >= 16) {
        self.functions.set_data1(&self.data_[1]);
        ESP_LOGD(LOG_FUNCTIONS_TAG, "Got functions packet 1 (via InfoRequest)");
      } else {
        ESP_LOGW(LOG_FUNCTIONS_TAG, "Functions packet 1 data length too short: %d", self.parser_.data_length());
      }
    }
  };
  scheduler_.register_request(r_funcs1);
  if (!is_enabled) {
    scheduler_.disable_request(0x20);
  }

  // --- Part 2 (0x22) ---
  InfoRequest r_funcs2("functions2", "Functions Part 2", 0x22, 3, 0, interval, LOG_FUNCTIONS_TAG);
  r_funcs2.on_response = [this, check_and_disable](CN105Climate &self) {
    // Log the raw packet and decoded pairs even if the unit returns all zeros
    self.hp_packet_debug(self.data_, self.parser_.data_length(), "RX 0x22");
    self.hp_functions_debug(self.data_, self.parser_.data_length());
    if (check_and_disable(self, 0x22)) {
      if (self.parser_.data_length() >= 16) {
        self.functions.set_data2(&self.data_[1]);
        ESP_LOGD(LOG_FUNCTIONS_TAG, "Got functions packet 2 (via InfoRequest)");
        self.functions_arrived();
      } else {
        ESP_LOGW(LOG_FUNCTIONS_TAG, "Functions packet 2 data length too short: %d", self.parser_.data_length());
      }
    }
  };
  scheduler_.register_request(r_funcs2);
  if (!is_enabled) {
    scheduler_.disable_request(0x22);
  }
}

// The sendInfoRequest, markResponseSeenFor, sendNextAfter, and processInfoResponse methods
// have been placed in RequestScheduler to comply with the Single Responsibility Principle (SRP).

void CN105Climate::ping_external_temperature() {
  this->set_timeout(SHEDULER_REMOTE_TEMP_TIMEOUT, this->remote_temp_timeout_, [this]() {
    ESP_LOGW(LOG_REMOTE_TEMP, "Remote temperature timeout occured, fall back to internal temperature!");
    this->clear_remote_temperature();
  });
}

void CN105Climate::set_remote_temp_timeout(uint32_t timeout) {
  this->remote_temp_timeout_ = timeout;
  if (timeout == 4294967295) {
    ESP_LOGI(LOG_REMOTE_TEMP, "set_remote_temp_timeout is set to never.");
  } else {
    // ESP_LOGI(LOG_ACTION_EVT_TAG, "set_remote_temp_timeout is set to %lu", timeout);
    log_info_uint32(LOG_REMOTE_TEMP, "set_remote_temp_timeout is set to ", timeout);

    this->ping_external_temperature();
  }
}

void CN105Climate::set_remote_temp_keepalive_interval(uint32_t interval_ms) {
  // Keep-alive cannot be disabled: it is the safety net that stops the unit from
  // reverting to its internal sensor when the remote temperature is stable (#474).
  // 0 (or any value under 20s) is clamped up to the 20s minimum.
  if (interval_ms < 20000) {
    ESP_LOGW(LOG_REMOTE_TEMP,
             "remote_temperature_keepalive_interval cannot be disabled and must be at least 20s. Clamping to 20s.");
    interval_ms = 20000;
  }
  this->remote_temp_keepalive_interval_ms_ = interval_ms;
  log_info_uint32(LOG_REMOTE_TEMP, "Remote temperature keep-alive interval set to ", interval_ms);
}

void CN105Climate::set_remote_temperature_margin(float margin) {
  this->remote_temp_margin_ = margin;
  ESP_LOGI(LOG_REMOTE_TEMP, "Remote temperature margin set to %.1f", margin);
}

void CN105Climate::start_remote_temp_keep_alive() {
  // Don't start if already active (keep-alive can no longer be disabled; the
  // interval is always >= 20s, see set_remote_temp_keepalive_interval).
  if (this->remote_temp_keepalive_active_) {
    ESP_LOGV(LOG_REMOTE_TEMP, "Keep-alive already active.");
    return;
  }

  this->remote_temp_keepalive_active_ = true;
  log_info_uint32(LOG_REMOTE_TEMP, "Starting remote temperature keep-alive with interval ",
                  this->remote_temp_keepalive_interval_ms_);

  this->set_interval(SCHEDULER_REMOTE_TEMP_KEEPALIVE, this->remote_temp_keepalive_interval_ms_, [this]() {
    if (this->remote_temperature_.has_value() && this->is_heatpump_connected()) {
      ESP_LOGD(LOG_REMOTE_TEMP, "Keep-alive: re-sending remote temperature %.1f", *this->remote_temperature_);
      // Send the temperature packet without resetting the watchdog timeout
      // (watchdog is only reset when HA sends a new value via set_remote_temperature)
      this->should_send_external_temperature_ = true;
    } else {
      if (!this->is_heatpump_connected()) {
        ESP_LOGW(LOG_REMOTE_TEMP, "Keep-alive skipped: Heatpump not connected!");
      } else {
        ESP_LOGD(LOG_REMOTE_TEMP, "Keep-alive skipped: no remote temperature set");
      }
    }
  });
}

void CN105Climate::stop_remote_temp_keep_alive() {
  if (!this->remote_temp_keepalive_active_) {
    return;
  }
  this->remote_temp_keepalive_active_ = false;
  this->cancel_interval(SCHEDULER_REMOTE_TEMP_KEEPALIVE);
  ESP_LOGI(LOG_REMOTE_TEMP, "Stopped remote temperature keep-alive.");
}

void CN105Climate::set_debounce_delay(uint32_t delay) {
  this->debounce_delay_ = delay;
  // ESP_LOGI(LOG_ACTION_EVT_TAG, "set_debounce_delay is set to %lu", delay);
  log_info_uint32(LOG_ACTION_EVT_TAG, "set_debounce_delay is set to ", delay);
}

float CN105Climate::get_compressor_frequency() { return current_status_.compressor_frequency; }
float CN105Climate::get_input_power() { return current_status_.input_power; }
float CN105Climate::get_kwh() { return current_status_.kwh; }
float CN105Climate::get_runtime_hours() { return current_status_.runtime_hours; }
bool CN105Climate::is_operating() { return current_status_.operating; }
bool CN105Climate::is_air_purifier() { return current_run_states_.air_purifier > 0; }
bool CN105Climate::is_night_mode() { return current_run_states_.night_mode > 0; }
bool CN105Climate::is_circulator() { return current_run_states_.circulator > 0; }

// SERIAL_8E1
void CN105Climate::setup_uart() {
  log_info_uint32(TAG, "setup_uart() with baudrate ", this->parent_->get_baud_rate());
  ESP_LOGI(LOG_CONN_TAG, "setup_uart(): baud=%d (UART port=%d)", this->parent_->get_baud_rate(), this->uart_port_);
  this->set_heatpump_connected(false);
  // isUARTConnected_ replaced by state_ (set to CONNECTING after successful config below)

  // just for debugging purpose, a way to use a button i, yaml to trigger a reconnect
  this->uart_setup_switch = true;

  if (this->parent_->get_data_bits() == 8 && this->parent_->get_parity() == uart::UART_CONFIG_PARITY_EVEN &&
      this->parent_->get_stop_bits() == 1) {
    ESP_LOGI(LOG_CONN_TAG, "UART configured as SERIAL_8E1");
    this->transition_to_(DriverState::CONNECTING);
    this->parser_.reset();
  } else {
    ESP_LOGW(LOG_CONN_TAG, "UART is not configured as SERIAL_8E1");
  }
}

void CN105Climate::set_heatpump_connected(bool state) {
  if (state) {
    this->transition_to_(DriverState::CONNECTED);
  } else if (state_ == DriverState::CONNECTED) {
    this->transition_to_(DriverState::DISCONNECTED);
  }
  if (this->hp_uptime_connection_sensor_ != nullptr) {
    if (state) {
      this->hp_uptime_connection_sensor_->start();
      ESP_LOGD(TAG, "starting hp_uptime_connection_sensor_ uptime chrono");
    } else {
      this->hp_uptime_connection_sensor_->stop();
      ESP_LOGD(TAG, "stopping hp_uptime_connection_sensor_ uptime chrono");
    }
  }
}
void CN105Climate::disconnect_uart() {
  ESP_LOGD(TAG, "disconnect_uart()");
  this->uart_setup_switch = false;
  this->set_heatpump_connected(false);
  // Legacy booleans removed — state managed by FSM (set_heatpump_connected / transition_to_)
  this->first_run_ = true;
  this->first_real_state_received_ = false;
  this->publish_state();
}

void CN105Climate::reconnect_uart() {
  ESP_LOGD(TAG, "reconnectUART()");
  this->last_reconnect_time_ms_ = CUSTOM_MILLIS;
  this->disconnect_uart();
  this->setup_uart();
  this->send_first_connection_packet();
}

void CN105Climate::reconnect_if_connection_lost() {
  uint32_t reconnect_time_ms = CUSTOM_MILLIS - this->last_reconnect_time_ms_;

  if (reconnect_time_ms < this->update_interval_) {
    return;
  }

  if (!this->is_heatpump_connection_active()) {
    uint32_t connect_time_ms = CUSTOM_MILLIS - this->last_connect_rq_time_ms_;
    if (connect_time_ms > this->update_interval_) {
      uint32_t lr_time_ms = CUSTOM_MILLIS - this->last_response_ms_;
      ESP_LOGW(TAG, "Heatpump has not replied for %lu s", (unsigned long) (lr_time_ms / 1000));
      ESP_LOGI(TAG, "We think Heatpump is not connected anymore..");
      this->reconnect_uart();
    }
  }
}

bool CN105Climate::is_heatpump_connection_active() {
  uint32_t lr_time_ms = CUSTOM_MILLIS - this->last_response_ms_;

  // if (lr_time_ms > MAX_DELAY_RESPONSE_FACTOR * this->update_interval_) {
  //     ESP_LOGV(TAG, "Heatpump has not replied for %ld s", lr_time_ms / 1000);
  //     ESP_LOGV(TAG, "We think Heatpump is not connected anymore..");
  //     this->disconnect_uart();
  // }

  return (lr_time_ms < MAX_DELAY_RESPONSE_FACTOR * this->update_interval_);
}
