#include "cn105.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

using namespace esphome;

/**
 * This method is call by the esphome framework to initialize the component
 * We don't try to connect to the heater here because errors could not be logged fine because the
 * UART is used for communication with the heatpump
 * setupUART will handle the
*/
void CN105Climate::setup() {

    ESP_LOGD(TAG, "Component initialization: setup call");
    this->boot_ms_ = CUSTOM_MILLIS;
    this->current_temperature = NAN;
    this->target_temperature = NAN;
    this->target_temperature_low = NAN;
    this->target_temperature_high = NAN;
    this->fan_mode = climate::CLIMATE_FAN_OFF;
    this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->parser_.reset();
    this->lastResponseMs = CUSTOM_MILLIS;

    // initialize diagnostic stats
    this->nbCompleteCycles_ = 0;
    this->nbCycles_ = 0;
    this->nbHeatpumpConnections_ = 0;

    // Register info requests here to ensure all dependencies (like hardware_settings) are ready
    this->register_info_requests();


    //ESP_LOGI(TAG, "remote_temp_timeout is set to %lu", this->remote_temp_timeout_);
    log_info_uint32(TAG, "remote_temp_timeout is set to ", this->remote_temp_timeout_);
    //ESP_LOGI(TAG, "debounce_delay is set to %lu", this->debounce_delay_);
    log_info_uint32(TAG, "debounce_delay is set to ", this->debounce_delay_);

    // IMPORTANT: do not initiate the UART/CN105 connection in setup().
    // We start the sequence in loop() to avoid missing the first OTA logs.


    ESP_LOGI(TAG, "Horizontal vanes configured: %d", this->horizontal_vanes_);

    // Restore set points from ESPHome preferences
    auto restore = this->restore_state_();
    if (restore.has_value()) {
        restore->apply(this);
        this->desired_mode_ = this->mode;
        if (!std::isnan(this->target_temperature)) {
            this->desired_temp_ = this->target_temperature;
        } else {
            this->desired_temp_ = 22.0f;
        }
        ESP_LOGI(TAG, "Restored state: mode=%s, temp=%.1f", climate::climate_mode_to_string(this->desired_mode_), this->desired_temp_);
    } else {
        this->desired_mode_ = climate::CLIMATE_MODE_OFF;
        this->desired_temp_ = 22.0f;
        ESP_LOGI(TAG, "No restored state found. Defaults used.");
    }
}


/**
 * @brief Executes the main loop for the CN105Climate component.
 * This function is called repeatedly in the main program loop.
 */
void CN105Climate::loop() {
    // Bootstrap connection CN105 (UART + CONNECT) from loop()
    this->maybe_start_connection_();

    // As long as the connection is not successful, we do not launch ANY cycle/write (otherwise it short-circuits the delay).
    // We still continue to read/process the input in order to detect 0x7A/0x7B (connection success).
    const bool can_talk_to_hp = this->is_heatpump_connected();

    if (can_talk_to_hp) {
        uint32_t now = CUSTOM_MILLIS;
        static uint32_t last_evaluation_time = 0;
        static float last_room_temp = NAN;
        float current_room_temp = this->current_temperature;
        bool temp_changed = !std::isnan(current_room_temp) && (std::isnan(last_room_temp) || fabsf(current_room_temp - last_room_temp) >= 0.1f);
        if (temp_changed || (now - last_evaluation_time >= 5000)) {
            last_evaluation_time = now;
            last_room_temp = current_room_temp;
            this->evaluate_fan_stop_and_ltp();
        }
    }

    if (!this->process_input()) {                                            // if we don't get any input: no read op
        if (!can_talk_to_hp) {
            return;
        }
        if ((this->wantedSettings.hasChanged) && (!this->loopCycle.is_cycle_running())) {
            this->check_pending_wanted_settings();
        } else if ((this->wantedRunStates.hasChanged) && (!this->loopCycle.is_cycle_running())) {
            this->check_pending_wanted_run_states();
        } else if ((this->isSetFunctions_) && (!this->loopCycle.is_cycle_running())) {
            this->isSetFunctions_ = false;
            this->set_functions(this->functions);
            // Also request to get function settings from heat pump to update UI with latest values.
            this->isGetFunctions_ = true;
        } else {
            if (this->loopCycle.is_cycle_running()) {                         // if we are  running an update cycle
                this->loopCycle.check_timeout(this->update_interval_);
            } else { // we are not running a cycle
                if (this->loopCycle.has_update_interval_passed(this->get_update_interval())) {
                    if (this->isGetFunctions_) {
                        // Reactivate requests 0x20/0x22 and bypass interval timers.
                        // This must be done before starting a new cycle to prevent a race hazard of
                        // request 0x22 occurring before request 0x20.
                        this->scheduler_.enable_request(0x20);
                        this->scheduler_.timer_bypass(0x20);
                        this->scheduler_.enable_request(0x22);
                        this->scheduler_.timer_bypass(0x22);
                        this->isGetFunctions_ = false;
                    }
                    this->build_and_send_requests_info_packets();            // initiate an update cycle with this->cycleStarted();
                }
            }
        }
    }
}

void CN105Climate::maybe_start_connection_() {
    switch (state_) {
        case DriverState::BOOT: {
            // Arm a 120s global timeout (fires once, forces connection even without WiFi)
            this->set_timeout("cn105_bootstrap_timeout", 120000, [this]() {
                if (state_ >= DriverState::CONNECTING) return;
                ESP_LOGW(LOG_CONN_TAG, "Bootstrap connection: 120s timeout, starting CN105 anyway");
                this->setup_uart();
                this->send_first_connection_packet();
            });

#ifdef USE_WIFI
            if (wifi::global_wifi_component != nullptr && !wifi::global_wifi_component->is_connected()) {
                this->transition_to_(DriverState::WAIT_WIFI);
                ESP_LOGI(LOG_CONN_TAG, "Bootstrap connection: waiting for WiFi before UART/CONNECT initialization");
                return;
            }
#endif
            // WiFi ready (or no WiFi) — check grace delay
            this->transition_to_(DriverState::WAIT_GRACE);
            ESP_LOGI(LOG_CONN_TAG, "Bootstrap connection: grace delay %ums for OTA logs", this->conn_bootstrap_delay_ms_);
            return;
        }

        case DriverState::WAIT_WIFI: {
#ifdef USE_WIFI
            if (wifi::global_wifi_component != nullptr && !wifi::global_wifi_component->is_connected()) {
                return;  // still waiting
            }
#endif
            this->transition_to_(DriverState::WAIT_GRACE);
            ESP_LOGI(LOG_CONN_TAG, "Bootstrap connection: WiFi connected, grace delay %ums", this->conn_bootstrap_delay_ms_);
            return;
        }

        case DriverState::WAIT_GRACE: {
            const uint32_t elapsed = CUSTOM_MILLIS - this->boot_ms_;
            if (elapsed < this->conn_bootstrap_delay_ms_) {
                return;  // grace delay not elapsed yet
            }
            ESP_LOGI(LOG_CONN_TAG, "Bootstrap connection: initializing UART + sending CONNECT (loop)");
            this->setup_uart();
            this->send_first_connection_packet();
            // setup_uart() transitions to CONNECTING if UART config is valid
            return;
        }

        case DriverState::CONNECTING:
        case DriverState::CONNECTED:
        case DriverState::DISCONNECTED:
            // Nothing to do — connection already started or managed elsewhere
            return;
    }
}

uint32_t CN105Climate::get_update_interval() const { return this->update_interval_; }
void CN105Climate::set_update_interval(uint32_t update_interval) {
    //ESP_LOGD(TAG, "Setting update interval to %lu", update_interval);
    log_debug_uint32(TAG, "Setting update interval to ", update_interval);

    this->update_interval_ = update_interval;
    this->autoUpdate = (update_interval != 0);
}
