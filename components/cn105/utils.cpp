#include "cn105.h"
#include "globals.h"
#include <math.h>
#include <memory>

using namespace esphome;

void esphome::log_info_uint32(const char *tag, const char *msg, uint32_t value, const char *suffix) {
#if __GNUC__ >= 11
  ESP_LOGI(tag, "%s %lu %s", msg, (unsigned long) value, suffix);
#else
  ESP_LOGI(tag, "%s %u %s", msg, (unsigned int) value, suffix);
#endif
}
void esphome::log_debug_uint32(const char *tag, const char *msg, uint32_t value, const char *suffix) {
#if __GNUC__ >= 11
  ESP_LOGD(tag, "%s %lu %s", msg, (unsigned long) value, suffix);
#else
  ESP_LOGD(tag, "%s %u %s", msg, (unsigned int) value, suffix);
#endif
}

bool CN105Climate::has_changed(const char *before, const char *now, const char *field) {
  if (now == NULL) {
    ESP_LOGD(TAG, "No value in has_changed() function for %s", field);
    return false;
  }
  return ((before == NULL) || (strcmp(before, now) != 0));
}

const char *CN105Climate::get_if_not_null(const char *what, const char *default_value) {
  if (what == NULL) {
    return default_value;
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
 * Convert the raw energy usage value to kwh.
 *
 * By default (power_unit_is_btu_ = false) the protocol encodes energy as
 * kwh * 10, so we simply divide by 10.
 *
 * Set `power_unit_is_btu: true` for units that encode in kBTU instead:
 *   => raw [kBTU] * (1055.056 / 3 600 000) * 1000 = kwh
 */
float CN105Climate::convert_energy_usage_to_kwh(float raw_energy_usage) {
  if (power_unit_is_btu_) {
    static constexpr float conv_factor = 1055.05585262f / 3600000.0f;
    return 1000.0f * raw_energy_usage * conv_factor;
  }
  return raw_energy_usage / 10.0f;  // already in kwh/10
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

void CN105Climate::debug_settings(const char *setting_name, WantedHeatpumpSettings &settings) {
  ESP_LOGD(LOG_ACTION_EVT_TAG,
           "[%s]-> [power: %s, target °C: %.1f, mode: %s, fan: %s, vane: %s, wvane: %s, has_changed ? -> %s, "
           "has_been_sent ? -> %s]",
           get_if_not_null(setting_name, "unnamed"), hp_power_to_str(settings.power), settings.temperature.value_or(NAN),
           hp_mode_to_str(settings.mode), hp_fan_to_str(settings.fan), hp_vane_to_str(settings.vane),
           hp_wide_vane_to_str(settings.wide_vane), settings.has_changed ? "YES" : " NO",
           settings.has_been_sent ? "YES" : " NO");
}

float CN105Climate::get_target_temperature_in_current_mode() { return this->get_target_temperature(); }

float CN105Climate::get_target_temperature() { return this->target_temperature; }

float CN105Climate::get_current_temperature() { return this->current_temperature; }

void CN105Climate::set_target_temperature(float temperature) { this->target_temperature = temperature; }

void CN105Climate::set_current_temperature(float temperature) { this->current_temperature = temperature; }

void CN105Climate::debug_climate(const char *setting_name) {
  ESP_LOGD(LOG_SETTINGS_TAG, "[%s]-> [mode: %s, target °C: %.1f, fan: %s, swing: %s]", setting_name,
           LOG_STR_ARG(climate_mode_to_string(this->mode)),  // use LOG_STR_ARG
           this->get_target_temperature_in_current_mode(),
           this->fan_mode.has_value() ? LOG_STR_ARG(climate_fan_mode_to_string(this->fan_mode.value())) : "-",
           LOG_STR_ARG(climate_swing_mode_to_string(this->swing_mode)));
}

void CN105Climate::debug_settings(const char *setting_name, HeatpumpSettings &settings) {
  ESP_LOGD(LOG_SETTINGS_TAG, "[%s]-> [power: %s, target °C: %.1f, mode: %s, fan: %s, vane: %s, wvane: %s]",
           get_if_not_null(setting_name, "unnamed"), hp_power_to_str(settings.power), settings.temperature.value_or(NAN),
           hp_mode_to_str(settings.mode), hp_fan_to_str(settings.fan), hp_vane_to_str(settings.vane),
           hp_wide_vane_to_str(settings.wide_vane));
}

void CN105Climate::debug_status(const char *status_name, HeatpumpStatus status) {
  // Declare a buffer (char array) for the float to string conversion
  char outside_temp_buffer[16];

  ESP_LOGI(LOG_STATUS_TAG, "[%s]-> [room C°: %.1f, outside C°: %s, operating: %s, compressor freq: %.1f Hz]",
           status_name, status.room_temperature,
           // use snprintf inside the ternary expression
           isnan(status.outside_air_temperature)
               ? "N/A"
               : (snprintf(outside_temp_buffer, sizeof(outside_temp_buffer), "%.1f", status.outside_air_temperature) > 0
                      ? outside_temp_buffer
                      : "ERR"),
           status.operating ? "YES" : "NO ", status.compressor_frequency);
}

void CN105Climate::debug_settings_and_status(const char *setting_name, HeatpumpSettings settings,
                                             HeatpumpStatus status) {
  this->debug_settings(setting_name, settings);
  this->debug_status(setting_name, status);
}

namespace {
// Human-readable label for the command byte (offset 1 of a 0xFC-framed packet).
const char *packet_command_label(uint8_t command) {
  switch (command) {
    case 0x5A:
      return "CONNECT";
    case 0x5B:
      return "CONN_INST";  // installer mode
    case 0x41:
      return "SET";  // command sent to HP
    case 0x42:
      return "ACK/INFO";  // response/info from HP
    case 0x61:
      return "GET";  // request data from HP
    case 0x62:
      return "RESPONSE";  // data response from HP
    default:
      return "UNKNOWN";
  }
}

// Human-readable label for the sub-command byte (offset 5), or "" if unknown.
const char *packet_subcommand_label(uint8_t sub) {
  switch (sub) {
    case 0x01:
      return ":Start";
    case 0x02:
      return ":Settings";
    case 0x03:
      return ":RoomTemp";
    case 0x04:
      return ":Status";
    case 0x05:
      return ":Standby";
    case 0x06:
      return ":Status";
    case 0x09:
      return ":Power";
    case 0x10:
      return ":Hello";  // connect response
    case 0x20:
      return ":Func1";  // functions part 1
    case 0x22:
      return ":Func2";  // functions part 2
    default:
      return "";
  }
}

// Append "XX " for each byte in [begin, end) into buf, respecting its size.
void append_hex(char *buf, size_t buf_size, const uint8_t *begin, const uint8_t *end) {
  if (buf_size == 0)
    return;
  buf[0] = '\0';
  char *p = buf;
  size_t rem = buf_size;
  for (const uint8_t *b = begin; b < end; ++b) {
    int written = snprintf(p, rem, "%02X ", *b);
    if (written <= 0 || (size_t) written >= rem)
      break;
    p += written;
    rem -= written;
  }
}
}  // namespace

void CN105Climate::hp_packet_debug(const uint8_t *packet, unsigned int length, const char *packet_direction,
                                   const char *log_prefix) {
  if (length < 5) {
    // Fallback for too short packets
    char output[20] = "";
    append_hex(output, sizeof(output), packet, packet + length);
    ESP_LOGD(packet_direction, "SHORT: %s", output);
    return;
  }

  const char *label = (packet[0] == 0xFC) ? packet_command_label(packet[1]) : "UNKNOWN";
  // Byte 5 (index 5) is the sub-command; only present once we have more than the 5-byte header.
  const char *sub_label = (length > 5) ? packet_subcommand_label(packet[5]) : "";

  char full_label[20];
  snprintf(full_label, sizeof(full_label), "%s%s", label, sub_label);

  // HEADER: first 5 bytes
  char header_str[18] = "";
  append_hex(header_str, sizeof(header_str), packet, packet + 5);

  // DATA: payload (bytes 5 .. length-2, i.e. excluding header and checksum)
  char data_str[380] = "";
  if (length > 6) {
    append_hex(data_str, sizeof(data_str), packet + 5, packet + (length - 1));
  }

  // CHECKSUM: last byte
  char cs_str[4] = "";
  snprintf(cs_str, sizeof(cs_str), "%02X", packet[length - 1]);

  // Output format: prefix|HEADER|->[PAYLOAD](CS) <LABEL:SubLabel>
  ESP_LOGD(packet_direction, "%s|%s|->[%s](%s) <%s>", log_prefix, header_str, data_str, cs_str, full_label);
}

void CN105Climate::hp_functions_debug(uint8_t *packet, unsigned int length) {
  if (length < 2)
    return;  // No data to decode

  char output[128] = "";
  char *p = output;
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
    if (written > 0 && (size_t) written < sizeof(buffer)) {
      int copy_len = snprintf(p, rem, "%s", buffer);
      if (copy_len > 0 && (size_t) copy_len < rem) {
        p += copy_len;
        rem -= copy_len;
      }
    }
  }

  // Display with LOG_FUNCTIONS_TAG (defined in cn105_types.h)
  // E.g.: [FUNCTIONS] Decoded 20: 101:1 102:3 103:2 ...
  ESP_LOGD(LOG_FUNCTIONS_TAG, "Decoded %02X:%s", packet[0], output);
}

int CN105Climate::lookup_byte_map_index(const int values_map[], int len, int lookup_value, const char *debug_info) {
  int idx = cn105_protocol::lookup_index(values_map, len, lookup_value);
  if (idx < 0) {
    ESP_LOGW("lookup", "%s caution value %d not found, returning -1", debug_info, lookup_value);
  }
  return idx;
}
int CN105Climate::lookup_byte_map_index(const char *values_map[], int len, const char *lookup_value,
                                        const char *debug_info) {
  int idx = cn105_protocol::lookup_index(values_map, len, lookup_value);
  if (idx < 0) {
    ESP_LOGW("lookup", "%s caution value %s not found, returning -1", debug_info, lookup_value);
  }
  return idx;
}
const char *CN105Climate::lookup_byte_map_value(const char *values_map[], const uint8_t byte_map[], int len,
                                                uint8_t byte_value, const char *debug_info, const char *default_value) {
  // Check if value exists in the map first
  for (int i = 0; i < len; i++) {
    if (byte_map[i] == byte_value) {
      return values_map[i];
    }
  }
  if (default_value != nullptr) {
    return default_value;
  }
  ESP_LOGW("lookup", "%s caution: value %d not found, returning value at index 0", debug_info, byte_value);
  return values_map[0];
}
int CN105Climate::lookup_byte_map_value(const int values_map[], const uint8_t byte_map[], int len, uint8_t byte_value,
                                        const char *debug_info) {
  int result = cn105_protocol::lookup_value(values_map, byte_map, len, byte_value);
  // Check if the lookup actually found a match vs returned fallback
  bool found = false;
  for (int i = 0; i < len; i++) {
    if (byte_map[i] == byte_value) {
      found = true;
      break;
    }
  }
  if (!found) {
    ESP_LOGW("lookup", "%s caution: value %d not found, returning value at index 0", debug_info, byte_value);
  }
  return result;
}
