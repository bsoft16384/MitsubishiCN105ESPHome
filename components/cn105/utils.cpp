#include "cn105.h"
#include "Globals.h"
#include <math.h>
#include <memory>

using namespace esphome;

void esphome::log_info_uint32(const char* tag, const char* msg, uint32_t value, const char* suffix) {
#if __GNUC__ >= 11
    ESP_LOGI(tag, "%s %lu %s", msg, (unsigned long)value, suffix);
#else
    ESP_LOGI(tag, "%s %u %s", msg, (unsigned int)value, suffix);
#endif
}
void esphome::log_debug_uint32(const char* tag, const char* msg, uint32_t value, const char* suffix) {
#if __GNUC__ >= 11
    ESP_LOGD(tag, "%s %lu %s", msg, (unsigned long)value, suffix);
#else
    ESP_LOGD(tag, "%s %u %s", msg, (unsigned int)value, suffix);
#endif
}

bool CN105Climate::has_changed(const char* before, const char* now, const char* field, bool checkNotNull) {
    if (now == NULL) {
        if (checkNotNull) {
            ESP_LOGE(TAG, "CAUTION: expected value in hasChanged() function for %s, got NULL", field);
        } else {
            ESP_LOGD(TAG, "No value in hasChanged() function for %s", field);
        }
        return false;
    }
    return ((before == NULL) || (strcmp(before, now) != 0));
}


const char* CN105Climate::get_if_not_null(const char* what, const char* defaultValue) {
    if (what == NULL) {
        return defaultValue;
    }
    return what;
}

/**
 * This function calculates the temperature setting based on the mode and the temperature points.
 * It returns the temperature setting.
 */
float CN105Climate::calculate_temperature_setting(float setting) {
    setting = std::round(2.0f * setting) / 2.0f;  // Round to the nearest half-degree.
    return setting < 10 ? 10 : (setting > 31 ? 31 : setting);
}

/**
 * Convert raw input power value to Watts.
 *
 * By default (power_unit_is_btu_ = false) the CN105 protocol already sends
 * the value in native Watts, so no conversion is needed (identity).
 *
 * Set `power_unit_is_btu: true` in YAML for units (e.g. some MSZ-LN models)
 * whose firmware encodes the value in BTU/s instead. In that case the
 * conversion factor is: 1 W = 1 J/s, 1 BTU = 1055.056 J
 *   => raw [BTU/s] * (3600 / 1055.056) = Watts
 */
float CN105Climate::convert_input_power_to_w(float raw_input_power) {
    if (power_unit_is_btu_) {
        static constexpr float conv_factor = 3600.0f / 1055.05558262f;
        return raw_input_power * conv_factor;
    }
    return raw_input_power;  // already in Watts
}

/**
 * Convert the raw energy usage value to kWh.
 *
 * By default (power_unit_is_btu_ = false) the protocol encodes energy as
 * kWh * 10, so we simply divide by 10.
 *
 * Set `power_unit_is_btu: true` for units that encode in kBTU instead:
 *   => raw [kBTU] * (1055.056 / 3 600 000) * 1000 = kWh
 */
float CN105Climate::convert_energy_usage_to_kwh(float raw_energy_usage) {
    if (power_unit_is_btu_) {
        static constexpr float conv_factor = 1055.05585262f / 3600000.0f;
        return 1000.0f * raw_energy_usage * conv_factor;
    }
    return raw_energy_usage / 10.0f;  // already in kWh/10
}

/**
 * This function updates the target temperatures from the settings.
 * It also calculates the temperature setting based on the mode and the temperature points.
 * It returns the temperature setting.
 */

void CN105Climate::update_target_temperatures_from_settings(float temperature) {
    ESP_LOGD(LOG_SETTINGS_TAG, "SINGLE SETPOINT %.1f", temperature);
    this->set_target_temperature(temperature);
}

void CN105Climate::debug_settings(const char* settingName, wantedHeatpumpSettings& settings) {
    ESP_LOGD(LOG_ACTION_EVT_TAG, "[%s]-> [power: %s, target °C: %.1f, mode: %s, fan: %s, vane: %s, wvane: %s, hasChanged ? -> %s, hasBeenSent ? -> %s]",
        get_if_not_null(settingName, "unnamed"),
        hp_power_to_str(settings.power),
        settings.temperature.value_or(NAN),
        hp_mode_to_str(settings.mode),
        hp_fan_to_str(settings.fan),
        hp_vane_to_str(settings.vane),
        hp_wide_vane_to_str(settings.wideVane),
        settings.hasChanged ? "YES" : " NO",
        settings.hasBeenSent ? "YES" : " NO"
    );
}

float CN105Climate::get_target_temperature_in_current_mode() {
    return this->get_target_temperature();
}

float CN105Climate::get_target_temperature() {
    return this->target_temperature;
}

float CN105Climate::get_current_temperature() {
    return this->current_temperature;
}

void CN105Climate::set_target_temperature(float temperature) {
    this->target_temperature = temperature;
}



void CN105Climate::set_current_temperature(float temperature) {
    this->current_temperature = temperature;
}



void CN105Climate::debug_climate(const char* settingName) {
    ESP_LOGD(LOG_SETTINGS_TAG, "[%s]-> [mode: %s, target °C: %.1f, fan: %s, swing: %s]",
        settingName,
        LOG_STR_ARG(climate_mode_to_string(this->mode)), // Utilisation de LOG_STR_ARG
        this->get_target_temperature_in_current_mode(),
        this->fan_mode.has_value() ? LOG_STR_ARG(climate_fan_mode_to_string(this->fan_mode.value())) : "-",
        LOG_STR_ARG(climate_swing_mode_to_string(this->swing_mode)));
}







void CN105Climate::debug_settings(const char* settingName, heatpumpSettings& settings) {
    ESP_LOGD(LOG_SETTINGS_TAG, "[%s]-> [power: %s, target °C: %.1f, mode: %s, fan: %s, vane: %s, wvane: %s]",
        get_if_not_null(settingName, "unnamed"),
        hp_power_to_str(settings.power),
        settings.temperature.value_or(NAN),
        hp_mode_to_str(settings.mode),
        hp_fan_to_str(settings.fan),
        hp_vane_to_str(settings.vane),
        hp_wide_vane_to_str(settings.wideVane)
    );
}


void CN105Climate::debug_status(const char* statusName, heatpumpStatus status) {
    // Déclarez un buffer (tableau de char) pour la conversion float -> string
    char outside_temp_buffer[16];

    ESP_LOGI(LOG_STATUS_TAG, "[%s]-> [room C°: %.1f, outside C°: %s, operating: %s, compressor freq: %.1f Hz]",
        statusName,
        status.roomTemperature,
        // Utilisation de snprintf dans l'expression ternaire
        isnan(status.outsideAirTemperature)
        ? "N/A"
        : (snprintf(outside_temp_buffer, sizeof(outside_temp_buffer), "%.1f", status.outsideAirTemperature) > 0 ? outside_temp_buffer : "ERR"),
        status.operating ? "YES" : "NO ",
        status.compressorFrequency);
}


void CN105Climate::debug_settings_and_status(const char* settingName, heatpumpSettings settings, heatpumpStatus status) {
    this->debug_settings(settingName, settings);
    this->debug_status(settingName, status);
}



void CN105Climate::hp_packet_debug(const uint8_t* packet, unsigned int length, const char* packetDirection, const char* log_prefix) {
    if (length < 5) {
        // Fallback for too short packets
        char output[20] = "";
        char* p = output;
        size_t rem = sizeof(output);
        for (unsigned int i = 0; i < length; i++) {
            int written = snprintf(p, rem, "%02X ", packet[i]);
            if (written > 0 && (size_t)written < rem) {
                p += written;
                rem -= written;
            }
        }
        ESP_LOGD(packetDirection, "SHORT: %s", output);
        return;
    }

    // Determine packet type label
    const char* label = "UNKNOWN";
    if (packet[0] == 0xFC) {
        switch (packet[1]) {
        case 0x5A: label = "CONNECT"; break;
        case 0x5B: label = "CONN_INST"; break; // Installer mode
        case 0x41: label = "SET"; break;       // Command sent to HP
        case 0x42: label = "ACK/INFO"; break;  // Response/Info from HP
        case 0x61: label = "GET"; break;       // Request data from HP
        case 0x62: label = "RESPONSE"; break;  // Data response from HP
        }
    }

    // Determine specific command/data type (Semantic decoding)
    // Byte 5 (index 5) is usually the subcommand
    const char* subLabel = "";
    if (length > 5) {
        switch (packet[5]) {
        case 0x01: subLabel = ":Start"; break; // Or "Connect" ?
        case 0x02: subLabel = ":Settings"; break;
        case 0x03: subLabel = ":RoomTemp"; break;
        case 0x04: subLabel = ":Status"; break; // Or "Unknown"? 0x04 is RQST_PKT_STATUS in cn105_types.h
        case 0x05: subLabel = ":Standby"; break; // RQST_PKT_STANDBY
        case 0x06: subLabel = ":Status"; break;  // RQST_PKT_HVAC_OPTIONS? Wait, need to check types map.
                                                 // In cn105_types: 0x06 is RQST_PKT_HVAC_OPTIONS? 
                                                 // Actually 0x06 in RCVD_PKT is TIMER? 
                                                 // Let's stick to common ones seen in logs:
                                                 // 02=Settings, 03=RoomTemp, 06=Status/Timers?, 09=Power?
                                                 // 0x09 is RCVD_PKT_STATUS in some contexts or Power?
                                                 // Looking at logs:
                                                 // FC 62 ... 09 ... -> Power/Standby?
                                                 // FC 62 ... 06 ... -> Status?
        case 0x09: subLabel = ":Power"; break;
        case 0x10: subLabel = ":Hello"; break; // Connect response 
        case 0x20: subLabel = ":Func1"; break; // Functions part 1
        case 0x22: subLabel = ":Func2"; break; // Functions part 2
        }
    }
    
    // For 0x06 specifically, in many logs it's Status or Timers. 
    // In types.h: RCVD_PKT_STATUS = 5, RCVD_PKT_TIMER = 6.
    // Let's use generic names if unsure, but user wants semantic.
    if (packet[5] == 0x06) subLabel = ":Status"; 
    
    char fullLabel[20];
    snprintf(fullLabel, sizeof(fullLabel), "%s%s", label, subLabel);

    // Format strings
    char headerStr[18] = "";
    char dataStr[380] = "";
    char csStr[4] = "";

    // HEADER: First 5 bytes
    char* p = headerStr;
    size_t header_rem = sizeof(headerStr);
    for (unsigned int i = 0; i < 5 && i < length; i++) {
        int written = snprintf(p, header_rem, "%02X ", packet[i]);
        if (written > 0 && (size_t)written < header_rem) {
            p += written;
            header_rem -= written;
        }
    }

    // DATA: Bytes 5 to Length-2 (payload)
    p = dataStr;
    size_t data_rem = sizeof(dataStr);
    if (length > 6) {
        for (unsigned int i = 5; i < length - 1; i++) {
            int written = snprintf(p, data_rem, "%02X ", packet[i]);
            if (written > 0 && (size_t)written < data_rem) {
                p += written;
                data_rem -= written;
            }
        }
    }

    // CHECKSUM: Last byte
    snprintf(csStr, sizeof(csStr), "%02X", packet[length - 1]);

// Output format: [LABEL:SubLabel ] HEADER -> [ PAYLOAD ] CS
    ESP_LOGD(packetDirection, "%s|%s|->[%s](%s) <%s>", 
        log_prefix, headerStr, dataStr, csStr, fullLabel);
}

void CN105Climate::hp_functions_debug(uint8_t* packet, unsigned int length) {
    if (length < 2) return; // No data to decode

    char output[128] = "";
    char* p = output;
    size_t rem = sizeof(output);

    char buffer[16];

    // Start at i=1 to skip the command byte (0x20 or 0x22)
    for (unsigned int i = 1; i < length; i++) {
        uint8_t byte = packet[i];

        // Mitsubishi function code encoding:
        // - The upper 6 bits represent the function code offset from 100
        // - The lower 2 bits represent the function setting value (1-3)
        int code = (byte >> 2) + 100;
        int value = byte & 3;

        // Formatting "Code:Value" (e.g. " 102:3")
        int written = snprintf(buffer, sizeof(buffer), " %d:%d", code, value);
        if (written > 0 && (size_t)written < sizeof(buffer)) {
            int copy_len = snprintf(p, rem, "%s", buffer);
            if (copy_len > 0 && (size_t)copy_len < rem) {
                p += copy_len;
                rem -= copy_len;
            }
        }
    }

    // Display with LOG_FUNCTIONS_TAG (defined in cn105_types.h)
    // E.g.: [FUNCTIONS] Decoded 20: 101:1 102:3 103:2 ...
    ESP_LOGD(LOG_FUNCTIONS_TAG, "Decoded %02X:%s", packet[0], output);
}

int CN105Climate::lookup_byte_map_index(const int valuesMap[], int len, int lookupValue, const char* debugInfo) {
    int idx = cn105_protocol::lookup_index(valuesMap, len, lookupValue);
    if (idx < 0) {
        ESP_LOGW("lookup", "%s caution value %d not found, returning -1", debugInfo, lookupValue);
    }
    return idx;
}
int CN105Climate::lookup_byte_map_index(const char* valuesMap[], int len, const char* lookupValue, const char* debugInfo) {
    int idx = cn105_protocol::lookup_index(valuesMap, len, lookupValue);
    if (idx < 0) {
        ESP_LOGW("lookup", "%s caution value %s not found, returning -1", debugInfo, lookupValue);
    }
    return idx;
}
const char* CN105Climate::lookup_byte_map_value(const char* valuesMap[], const uint8_t byteMap[], int len, uint8_t byteValue, const char* debugInfo, const char* defaultValue) {
    // Check if value exists in the map first
    for (int i = 0; i < len; i++) {
        if (byteMap[i] == byteValue) {
            return valuesMap[i];
        }
    }
    if (defaultValue != nullptr) {
        return defaultValue;
    }
    ESP_LOGW("lookup", "%s caution: value %d not found, returning value at index 0", debugInfo, byteValue);
    return valuesMap[0];
}
int CN105Climate::lookup_byte_map_value(const int valuesMap[], const uint8_t byteMap[], int len, uint8_t byteValue, const char* debugInfo) {
    int result = cn105_protocol::lookup_value(valuesMap, byteMap, len, byteValue);
    // Check if the lookup actually found a match vs returned fallback
    bool found = false;
    for (int i = 0; i < len; i++) {
        if (byteMap[i] == byteValue) { found = true; break; }
    }
    if (!found) {
        ESP_LOGW("lookup", "%s caution: value %d not found, returning value at index 0", debugInfo, byteValue);
    }
    return result;
}


