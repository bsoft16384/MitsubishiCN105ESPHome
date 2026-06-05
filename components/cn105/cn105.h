#pragma once
#include "Globals.h"
#include "cn105_protocol.h"
#include "frame_parser.h"
#include "esphome/components/uart/uart.h"
#include "heatpumpFunctions.h"
#include "van_orientation_select.h"
#include "uptime_connection_sensor.h"
#include "functions_number.h"
#include "functions_button.h"
#include "hvac_option_switch.h"
#include "hardware_setting_select.h"
#include "info_request.h"
#include "request_scheduler.h"
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/button/button.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/switch/switch.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include "cycle_management.h"
#include <vector>
#include <map>

namespace esphome {

    // Connection lifecycle FSM — replaces 6 scattered booleans
    enum class DriverState : uint8_t {
        BOOT,           // setup() done, loop() not yet called
        WAIT_WIFI,      // WiFi required but not yet connected
        WAIT_GRACE,     // WiFi OK, OTA grace delay in progress
        CONNECTING,     // UART configured, CONNECT sent, awaiting 0x7A/0x7B
        CONNECTED,      // Handshake succeeded, ready to poll
        DISCONNECTED,   // Response timeout, reconnection needed
    };

    const char* driver_state_to_str(DriverState s);

    void log_info_uint32(const char* tag, const char* msg, uint32_t value, const char* suffix = "");
    void log_debug_uint32(const char* tag, const char* msg, uint32_t value, const char* suffix = "");

    class CN105Climate : public climate::Climate, public Component, public esphome::uart::UARTDevice {


    public:

        CN105Climate(uart::UARTComponent* hw_serial);

        enum class VaneType {
            STANDARD = 0,
            SPLIT_HORIZONTAL = 1,
            SPLIT_VERTICAL = 2
        };

        void set_vertical_vane_select(VaneOrientationSelect* vertical_vane_select);
        void set_horizontal_vane_select(VaneOrientationSelect* horizontal_vane_select, const std::vector<std::string>& options = {});
        void set_airflow_control_select(VaneOrientationSelect* airflow_control_select);
        void set_compressor_frequency_sensor(esphome::sensor::Sensor* compressor_frequency_sensor);
        void set_target_humidity_sensor(esphome::sensor::Sensor* target_humidity_sensor);
        void set_input_power_sensor(esphome::sensor::Sensor* input_power_sensor);
        void set_kwh_sensor(esphome::sensor::Sensor* kwh_sensor);
        void set_runtime_hours_sensor(esphome::sensor::Sensor* runtime_hours_sensor);
        void set_outside_air_temperature_sensor(esphome::sensor::Sensor* outside_air_temperature_sensor);
        void set_isee_sensor(esphome::binary_sensor::BinarySensor* iSee_sensor);
        void set_stage_sensor(esphome::text_sensor::TextSensor* Stage_sensor);
        void set_use_stage_for_operating_status(bool value);
        void set_air_purifier_switch(HVACOptionSwitch* air_purifier_switch);
        void set_night_mode_switch(HVACOptionSwitch* night_mode_switch);
        void set_circulator_switch(HVACOptionSwitch* circulator_switch);

        void add_hardware_setting(HardwareSettingSelect* setting);
        void set_hardware_settings_interval(uint32_t interval_ms) { this->hardware_settings_interval_ms_ = interval_ms; }
        
        // Deprecated: kept for backward compatibility, will map to VaneType::SPLIT_HORIZONTAL
        void set_horizontal_vanes(int horizontal_vanes) { 
            if (horizontal_vanes > 1) {
                this->vane_type_ = VaneType::SPLIT_HORIZONTAL;
            }
        }
        void set_vane_type(VaneType type) { this->vane_type_ = type; }

        void set_functions_sensor(esphome::text_sensor::TextSensor* Functions_sensor);
        void set_functions_get_button(FunctionsButton* Button);
        void set_functions_set_button(FunctionsButton* Button);
        void set_functions_set_code(FunctionsNumber* Number);
        void set_functions_set_value(FunctionsNumber* Number);

        void set_sub_mode_sensor(esphome::text_sensor::TextSensor* Sub_mode_sensor);
        void set_auto_sub_mode_sensor(esphome::text_sensor::TextSensor* Auto_sub_mode_sensor);
        void set_error_code_sensor(esphome::text_sensor::TextSensor* error_code_sensor);
        void set_remote_temp_source(esphome::sensor::Sensor* source);
        void set_remote_temp_source_info_sensor(esphome::text_sensor::TextSensor* info_sensor);
        void set_hp_uptime_connection_sensor(cn105::HpUpTimeConnectionSensor* hp_up_connection_sensor);
        
        void set_remote_temperature_control_sensor(esphome::binary_sensor::BinarySensor* sensor);
        void set_remote_temperature_margin(float margin);
        void set_fan_stop_switch(esphome::switch_::Switch* fan_stop_switch) { this->fan_stop_switch_ = fan_stop_switch; }
        void set_low_temp_protection_switch(esphome::switch_::Switch* low_temp_protection_switch) { this->low_temp_protection_switch_ = low_temp_protection_switch; }
        void set_diagnostic_sensor(esphome::text_sensor::TextSensor* diagnostic_sensor) { this->diagnostic_sensor_ = diagnostic_sensor; }
        void set_hysteresis(float hysteresis) { this->hysteresis_ = hysteresis; }
        void set_low_temp_temp(float low_temp_temp) { this->low_temp_temp_ = low_temp_temp; }
        void set_low_temp_hysteresis(float low_temp_hysteresis) { this->low_temp_hysteresis_ = low_temp_hysteresis; }
        void evaluate_fan_stop_and_ltp();

        float convert_input_power_to_w(float raw_input_power);
        float convert_energy_usage_to_kwh(float raw_energy_usage);
        float get_compressor_frequency();
        float get_input_power();
        float get_kwh();
        float get_runtime_hours();
        bool is_operating();
        bool is_air_purifier();
        bool is_night_mode();
        bool is_circulator();

        // checks if the field has changed

        bool has_changed(const char* before, const char* now, const char* field, bool check_not_null = false);

        inline bool has_changed(esphome::StringRef before, const char* now, const char* field, bool check_not_null = false) {
            return has_changed(before.c_str(), now, field, check_not_null);
        }

        template <typename T>
        inline bool has_changed(T before, T now, const char* field, bool check_not_null = false) {
            return before != now;
        }


        float get_setup_priority() const override {
            return setup_priority::AFTER_WIFI;  // Configurez ce composant aprÃƒÂ¨s le WiFi
        }

        void generate_extra_components();

        void setup() override;
        void loop() override;

        void set_baud_rate(int baud_rate);
        void set_uart_port(int uart_port) { this->uart_port_ = uart_port; }
        void setup_uart();
        void disconnect_uart();
        void reconnect_uart();
        void build_and_send_requests_info_packets();
        void build_and_send_request_packet(int packet_type);
        void build_and_send_info_packet(uint8_t code);
        bool is_heatpump_connection_active();
        void reconnect_if_connection_lost();

        // FSM
        DriverState driver_state() const { return state_; }
        void transition_to_(DriverState next);
        // Compatibility accessors (replace former booleans)
        bool is_uart_ready() const { return state_ >= DriverState::CONNECTING; }
        bool is_heatpump_connected() const { return state_ == DriverState::CONNECTED; }

        void send_wanted_settings();
        void send_wanted_settings_delegate();
        // Use the temperature from an external sensor. Use
        // set_remote_temp(0) to switch back to the internal sensor.
        void set_remote_temperature(float);
        void send_remote_temperature();
        void send_remote_temperature_packet();  // Send packet only, without resetting watchdog
        void send_wanted_run_states();
        float get_deadband_adjusted_temperature(float remoteTemperature);

        void set_remote_temp_timeout(uint32_t timeout);

        // Configure the interval for remote temperature keep-alive (in milliseconds)
        // Set to 0 to disable keep-alive
        void set_remote_temp_keepalive_interval(uint32_t interval_ms);

        void set_debounce_delay(uint32_t delay);

        // this is the ping or heartbeat of the setRemotetemperature for timeout management
        void ping_external_temperature();

        // Start/stop the remote temperature keep-alive timer
        // Keep-alive periodically re-sends the remote temperature to prevent PAC fallback to internal sensor
        void start_remote_temp_keep_alive();
        void stop_remote_temp_keep_alive();

        uint32_t get_update_interval() const;
        void set_update_interval(uint32_t update_interval);

        climate::ClimateTraits traits() override;

        // Get a mutable reference to the traits that we support.
        climate::ClimateTraits& config_traits();

        void control(const esphome::climate::ClimateCall& call) override;
        void control_mode();
        void control_temperature();
        float calculate_temperature_setting(float setting);
        float get_target_temperature_in_current_mode();
        float get_target_temperature();
        float get_target_temperature_low();
        float get_target_temperature_high();
        float get_current_temperature();
        void set_target_temperature(float temperature);
        void set_target_temperature_low(float temperature);
        void set_target_temperature_high(float temperature);
        void set_current_temperature(float temperature);

        void control_fan();
        void control_swing();
        // Bootstrap connexion CN105 en loop() (ÃÂ©vite de perdre les tout premiers logs OTA)
        void maybe_start_connection_();

        // DÃÂ©lai de grÃÂ¢ce configurable avant d'envoyer CONNECT (pour laisser le flux OTA s'attacher)
        void set_connection_bootstrap_delay(uint32_t delay_ms) { this->conn_bootstrap_delay_ms_ = delay_ms; }

        // Mode installateur: utilise un handshake CONNECT ÃÂ©tendu (0x5B) au lieu du standard (0x5A)
        void set_installer_mode(bool mode) {
            // Mode demandÃÂ© via YAML
            this->installer_mode_ = mode;
            // Mode effectivement utilisÃÂ©: peut tomber en fallback vers standard si la PAC ignore 0x5B
            this->installer_mode_effective_ = mode;
            this->installer_mode_fallback_done_ = false;
        }

        // UnitÃÂ© de puissance brute envoyÃÂ©e par la PAC: false = Watts (dÃÂ©faut), true = BTU/s
        void set_power_unit_is_btu(bool v) { this->power_unit_is_btu_ = v; }

        // Configure the climate object with traits that we support.


        /// le bouton de setup de l'UART
        bool uart_setup_switch;

        // Legacy booleans replaced by DriverState FSM (see state_)
        // bool isUARTConnected_  → isUARTReady_()
        // bool isHeatpumpConnected_ → isHeatpumpConnected()
        bool shouldSendExternalTemperature_ = false;
        float remoteTemperature_ = 0;

        unsigned long nbCompleteCycles_ = 0;
        unsigned long nbCycles_ = 0;
        unsigned int nbHeatpumpConnections_ = 0;


        void send_first_connection_packet();
        void terminate_cycle();


        void functions_arrived();
        bool set_functions(HeatpumpFunctions const& functions);
        bool isGetFunctions_ = false;
        bool isSetFunctions_ = false;

        // helpers
        const char* get_if_not_null(const char* what, const char* defaultValue);




    protected:
        // HeatPump object using the underlying Arduino library.
        // same as PolingComponent
        uint32_t update_interval_;

        climate::ClimateTraits traits_;

        //Accessor method for the HardwareSerial pointer
        uart::UARTComponent* get_hw_serial_() {
            return this->parent_;
        }

        bool process_input(void);
        void process_data_packet();
        void get_error_info_from_response_packet();
    void get_data_from_response_packet();
        void get_auto_mode_state_from_response_packet(); //NET added
        void get_power_from_response_packet(); //NET added
        void get_settings_from_response_packet();
        void get_room_temperature_from_response_packet();
        void get_operating_and_compressor_freq_from_response_packet();
        void get_hvac_options_from_response_packet();

        void update_success();
        void process_command();

        uint8_t check_sum(uint8_t bytes[], int len);

        const char* get_mode_setting();
        const char* get_power_setting();
        const char* get_vane_setting();
        const char* get_wide_vane_setting();
        const char* get_airflow_control_setting();
        const char* get_fan_speed_setting();
        float get_temperature_setting();
        bool get_air_purifier_run_state();
        bool get_night_mode_run_state();
        bool get_circulator_run_state();

        void set_mode_setting(const char* setting);
        void set_power_setting(const char* setting);
        void set_vane_setting(const char* setting);
        void set_wide_vane_setting(const char* setting);
        void set_airflow_control_setting(const char* setting);
        void set_fan_speed(const char* setting);

        void set_heatpump_connected(bool state);

        binary_sensor::BinarySensor* isee_sensor_ = nullptr;
        binary_sensor::BinarySensor* remote_temp_sensor_ = nullptr;
        float remote_temp_margin_ = 0.4f;
        text_sensor::TextSensor* stage_sensor_{ nullptr }; // to save ref if needed
        bool use_stage_for_operating_status_{ false };
        text_sensor::TextSensor* functions_sensor_ = nullptr;
        FunctionsButton* functions_get_button_ = nullptr;
        FunctionsButton* functions_set_button_ = nullptr;
        FunctionsNumber* functions_set_code_ = nullptr;
        FunctionsNumber* functions_set_value_ = nullptr;
        text_sensor::TextSensor* sub_mode_sensor_ = nullptr;
        text_sensor::TextSensor* auto_sub_mode_sensor_ = nullptr;
        text_sensor::TextSensor* error_code_sensor_{ nullptr };
        sensor::Sensor* remote_temp_source_{ nullptr };
        text_sensor::TextSensor* remote_temp_source_info_sensor_{ nullptr };
        HVACOptionSwitch* air_purifier_switch_ = nullptr;
        HVACOptionSwitch* night_mode_switch_ = nullptr;
        HVACOptionSwitch* circulator_switch_ = nullptr;
        std::vector<HardwareSettingSelect*> hardware_settings_;
        uint32_t hardware_settings_interval_ms_{ 86400000 };  // Default 24h

        // The value of the code and value for the functions set.
        int functions_code_;
        int functions_value_;

        VaneOrientationSelect* vertical_vane_select_ = nullptr;
        VaneOrientationSelect* horizontal_vane_select_ = nullptr;
        std::vector<std::string> horizontal_vane_options_strings_;
        VaneOrientationSelect* airflow_control_select_ = nullptr;
        sensor::Sensor* compressor_frequency_sensor_ = nullptr;
        sensor::Sensor* target_humidity_sensor_ = nullptr;
        sensor::Sensor* input_power_sensor_ = nullptr;
        sensor::Sensor* kwh_sensor_ = nullptr;
        sensor::Sensor* runtime_hours_sensor_ = nullptr;
        sensor::Sensor* outside_air_temperature_sensor_ = nullptr;
        cn105::HpUpTimeConnectionSensor* hp_uptime_connection_sensor_ = nullptr;

    private:
        int uart_port_ = -1;
        const char* lookup_byte_map_value(const char* valuesMap[], const uint8_t byteMap[], int len, uint8_t byteValue, const char* debugInfo = "", const char* defaultValue = nullptr);
        int lookup_byte_map_value(const int valuesMap[], const uint8_t byteMap[], int len, uint8_t byteValue, const char* debugInfo = "");
        int lookup_byte_map_index(const char* valuesMap[], int len, const char* lookupValue, const char* debugInfo = "");
        int lookup_byte_map_index(const int valuesMap[], int len, int lookupValue, const char* debugInfo = "");
        template <typename T>
        int lookup_byte_map_index(const T valuesMap[], int len, T lookupValue, const char* debugInfo = "") {
            int idx = cn105_protocol::lookup_index(valuesMap, len, lookupValue);
            if (idx < 0) {
                ESP_LOGW("lookup", "%s caution: value not found, returning -1", debugInfo);
            }
            return idx;
        }

        void write_packet(uint8_t* packet, int length, bool checkIsActive = true);
        void prepare_info_packet(uint8_t* packet, int length);
        void prepare_set_packet(uint8_t* packet, int length);

        void publish_state_to_ha(heatpumpSettings& settings);
        void publish_wanted_settings_state_to_ha();
        void publish_wanted_run_states_state_to_ha();

        void heatpump_update(heatpumpSettings& settings);

        void status_changed(heatpumpStatus status);

        void check_pending_wanted_settings();
        void check_pending_wanted_run_states();
        void check_power_and_mode_settings(heatpumpSettings& settings, bool updateCurrentSettings = true);
        void check_fan_settings(heatpumpSettings& settings, bool updateCurrentSettings = true);
        void check_vane_settings(heatpumpSettings& settings, bool updateCurrentSettings = true);
        void check_wide_vane_settings(heatpumpSettings& settings, bool updateCurrentSettings = true);
        void update_extra_select_components(heatpumpSettings& settings);
        void update_target_temperatures_from_settings(float temperature);

        void update_action();
        void set_action_if_operating_to(climate::ClimateAction action);
        void hp_packet_debug(const uint8_t* packet, unsigned int length, const char* packetDirection, const char* log_prefix = "");
        void hp_functions_debug(uint8_t* packet, unsigned int length);

        void debug_settings(const char* settingName, heatpumpSettings& settings);
        void debug_settings(const char* settingName, wantedHeatpumpSettings& settings);
        void debug_status(const char* statusName, heatpumpStatus status);
        void debug_settings_and_status(const char* settingName, heatpumpSettings settings, heatpumpStatus status);
        void debug_climate(const char* settingName);




        void control_delegate(const esphome::climate::ClimateCall& call);
        // Refactor helpers for control_delegate
        bool process_mode_change(const esphome::climate::ClimateCall& call);
        bool process_temperature_change(const esphome::climate::ClimateCall& call);
        bool process_fan_change(const esphome::climate::ClimateCall& call);
        bool process_swing_change(const esphome::climate::ClimateCall& call);
        void finalize_control_if_updated(bool updated);


        void create_packet(uint8_t* packet);
        void create_info_packet(uint8_t* packet, uint8_t code);
        heatpumpSettings currentSettings{};
        wantedHeatpumpSettings wantedSettings{};
        heatpumpRunStates currentRunStates{};
        wantedHeatpumpRunStates wantedRunStates{};
        cycleManagement loopCycle{};

        // Orchestrateur des requÃƒÂªtes INFO
        RequestScheduler scheduler_;
        void register_info_requests();
        void register_hardware_settings_requests();
        unsigned long lastResponseMs = 0;


        uint32_t remote_temp_timeout_;
        uint32_t remote_temp_keepalive_interval_ms_ = DEFAULT_REMOTE_TEMP_KEEPALIVE_INTERVAL_MS;
        bool remote_temp_keepalive_active_ = false;
        uint32_t last_remote_temp_send_ms_ = 0;      // Timestamp of last remote temp packet sent
        float last_remote_temp_sent_ = 0;            // Last remote temp value actually sent (for change detection)
        uint8_t remote_temp_debounce_skip_count_ = 0;   // Counter for consecutive debounce skips
        bool remote_temp_heartbeat_warning_shown_ = false;  // Avoid spamming the warning
        uint32_t debounce_delay_;

        int baud_ = 0;



        unsigned long lastSend;
        unsigned long lastConnectRqTimeMs;
        unsigned long lastReconnectTimeMs;

        cn105_protocol::FrameParser parser_;     // UART frame assembler (Phase 3A)
        uint8_t* data;
        uint8_t get_payload_byte(int index, uint8_t default_val = 0) const;

        // All fields are default-initialized via heatpumpStatus struct defaults (NAN, false, etc.)
        heatpumpStatus currentStatus{};
        HeatpumpFunctions functions;

        bool wideVaneAdj;
        bool autoUpdate;
        bool firstRun;
        int infoMode;
        bool externalUpdate;

        // counter for status request for checking heatpump is still connected
        // is the counter > MAX_NON_RESPONSE_REQ then we conclude uart is not connected anymore
        int nonResponseCounter = 0;

        int powerRequestWithoutResponses = 0;

        bool isReading = false;
        bool isWriting = false;

        // foundStart, bytesRead, dataLength, command → moved into parser_ (Phase 3A)



        // Gestion sÃƒÂ»re d'un paquet diffÃƒÂ©rÃƒÂ© ÃƒÂ  ÃƒÂ©crire pour ÃƒÂ©viter la capture d'un buffer de pile
        void try_write_pending_packet();
        uint8_t pending_packet_[PACKET_LEN] = {};
        int pending_packet_len_ = 0;
        bool pending_check_is_active_ = true;
        bool has_pending_packet_ = false;

        // Connection lifecycle FSM
        DriverState state_ = DriverState::BOOT;
        uint32_t boot_ms_ = 0;
        uint32_t conn_bootstrap_delay_ms_{ 10000 };  // par dÃÂ©faut 10s

        bool installer_mode_{ false };
        bool installer_mode_effective_{ false };
        bool installer_mode_fallback_done_{ false };
        bool power_unit_is_btu_{ false };  // true = la PAC envoie en BTU/s (nÃÂ©cessite conversion ÃÂ3.412)

        int horizontal_vanes_{ 1 }; // Kept for legacy logging if needed, or can be removed if unused.
        VaneType vane_type_{ VaneType::STANDARD };

        esphome::switch_::Switch* fan_stop_switch_{nullptr};
        esphome::switch_::Switch* low_temp_protection_switch_{nullptr};
        esphome::text_sensor::TextSensor* diagnostic_sensor_{nullptr};
        float hysteresis_{0.5f};
        float low_temp_temp_{8.0f};
        float low_temp_hysteresis_{4.0f};

        // State tracking
        climate::ClimateMode desired_mode_{climate::CLIMATE_MODE_OFF};
        float desired_temp_{22.0f};
        uint32_t last_mode_command_time_ms_{0};
        bool first_real_state_received_{false};
        bool ltp_active_{false};

        climate::ClimateMode last_commanded_real_mode_{climate::CLIMATE_MODE_OFF};
        float last_commanded_real_temp_{22.0f};
    };
}
