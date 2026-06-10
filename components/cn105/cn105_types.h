#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <optional>
#include <array>
#include "cn105_protocol.h"

#define MAX_FRAME_BYTES 128
#define MAX_DELAY_RESPONSE_FACTOR 10

inline constexpr const char *LOG_ACTION_EVT_TAG = "EVT_SETS";
inline constexpr const char *TAG = "CN105";
inline constexpr const char *LOG_REMOTE_TEMP = "REMOTE_TEMP";
inline constexpr const char *LOG_ACK = "ACK";
inline constexpr const char *LOG_SETTINGS_TAG = "SETTINGS";
inline constexpr const char *LOG_STATUS_TAG = "STATUS";
inline constexpr const char *LOG_CYCLE_TAG = "CYCLE";
inline constexpr const char *LOG_UPD_INT_TAG = "UPDT_ITVL";
inline constexpr const char *LOG_SET_RUN_STATE = "SET_RUN_STATE";
inline constexpr const char *LOG_OPERATING_STATUS_TAG = "OPERATING_STATUS";
inline constexpr const char *LOG_TEMP_SENSOR_TAG = "TEMP_SENSOR";
inline constexpr const char *LOG_DUAL_SP_TAG = "DUAL_SP";
inline constexpr const char *LOG_FUNCTIONS_TAG = "FUNCTIONS";
inline constexpr const char *LOG_HARDWARE_SELECT_TAG = "HardwareSelect";
inline constexpr const char *LOG_CONN_TAG = "CN105_CONN";

inline constexpr const char *SCHEDULER_REMOTE_TEMP_TIMEOUT = "->remote_temp_timeout";
inline constexpr const char *SCHEDULER_REMOTE_TEMP_KEEPALIVE = "->remote_temp_keepalive";

// Default interval for remote temperature keep-alive (20 seconds, as observed on Kumo)
static const uint32_t DEFAULT_REMOTE_TEMP_KEEPALIVE_INTERVAL_MS = 20000;

// Minimum spacing between remote temperature writes to the unit (rate limit).
// Faster changes are coalesced and deferred until this window elapses.
static const uint32_t REMOTE_TEMP_MIN_SEND_INTERVAL_MS = 10000;

static const int DEFER_SCHEDULE_UPDATE_LOOP_DELAY = 750;

static const int PACKET_LEN = 22;

static const int CONNECT_LEN = 8;
static const uint8_t CONNECT[CONNECT_LEN] = {0xfc, 0x5a, 0x01, 0x30, 0x02, 0xca, 0x01, 0xa8};
static const int HEADER_LEN = 8;
static const uint8_t HEADER[HEADER_LEN] = {0xfc, 0x41, 0x01, 0x30, 0x10, 0x01, 0x00, 0x00};

static const int INFOHEADER_LEN = 5;
static const uint8_t INFOHEADER[INFOHEADER_LEN] = {0xfc, 0x42, 0x01, 0x30, 0x10};

static const int RCVD_PKT_NONE = -1;
static const int RCVD_PKT_FAIL = 0;
static const int RCVD_PKT_CONNECT_SUCCESS = 1;
static const int RCVD_PKT_SETTINGS = 2;
static const int RCVD_PKT_ROOM_TEMP = 3;
static const int RCVD_PKT_UPDATE_SUCCESS = 4;
static const int RCVD_PKT_STATUS = 5;
static const int RCVD_PKT_TIMER = 6;
static const int RCVD_PKT_FUNCTIONS = 7;

static const int MAX_NON_RESPONSE_REQ = 5;

static const uint8_t CONTROL_PACKET_1[5] = {0x01, 0x02, 0x04, 0x08, 0x10};
static const uint8_t CONTROL_PACKET_2[1] = {0x01};
static const uint8_t RUN_STATE_PACKET_1[5] = {0x01, 0x04, 0x08, 0x10, 0x20};

static const uint8_t FUNCTIONS_SET_PART1 = 0x1F;
static const uint8_t FUNCTIONS_GET_PART1 = 0x20;
static const uint8_t FUNCTIONS_SET_PART2 = 0x21;
static const uint8_t FUNCTIONS_GET_PART2 = 0x22;

const uint8_t ESPMHP_MIN_TEMPERATURE = 16;
const uint8_t ESPMHP_MAX_TEMPERATURE = 31;
const float ESPMHP_TEMPERATURE_STEP = 0.5;

enum class HPPower : uint8_t { OFF = 0, ON = 1, UNKNOWN = 2 };

enum class HPMode : uint8_t { HEAT = 0, DRY = 1, COOL = 2, FAN = 3, AUTO = 4, UNKNOWN = 5 };

enum class HPFanMode : uint8_t { AUTO = 0, QUIET = 1, F1 = 2, F2 = 3, F3 = 4, F4 = 5, UNKNOWN = 6 };

enum class HPVaneMode : uint8_t { AUTO = 0, V1 = 1, V2 = 2, V3 = 3, V4 = 4, V5 = 5, SWING = 6, UNKNOWN = 7 };

enum class HPWideVaneMode : uint8_t {
  LEFT_LEFT = 0,
  LEFT = 1,
  CENTER = 2,
  RIGHT = 3,
  RIGHT_RIGHT = 4,
  LEFT_RIGHT = 5,
  SWING = 6,
  AIRFLOW_CONTROL = 7,
  UNKNOWN = 8
};

enum class HPStage : uint8_t {
  IDLE = 0,
  LOW = 1,
  GENTLE = 2,
  MEDIUM = 3,
  MODERATE = 4,
  HIGH = 5,
  DIFFUSE = 6,
  UNKNOWN = 7
};

enum class HPSubMode : uint8_t { NORMAL = 0, WARMUP = 1, DEFROST = 2, PREHEAT = 3, STANDBY = 4, OFF = 5, UNKNOWN = 6 };

enum class HPAutoSubMode : uint8_t {
  AUTO_OFF = 0,
  AUTO_COOL = 1,
  AUTO_HEAT = 2,
  AUTO_LEADER = 3,
  AUTO_INACTIVE = 4,
  AUTO_IDLE = 5,
  AUTO_ACTIVE = 6,
  UNKNOWN = 7
};


enum class HPAirflowControl : uint8_t { EVEN = 0, INDIRECT = 1, DIRECT = 2, UNKNOWN = 3 };

// ════════════════════════════════════════════════════════════════
// Generic enum ↔ string / byte mapping infrastructure
// ════════════════════════════════════════════════════════════════

template<typename E> struct EnumEntry {
  E value;
  uint8_t protocol_byte;
  const char *label;
};

template<typename E, std::size_t N> using EnumTable = std::array<EnumEntry<E>, N>;

/// Look up the string label for an enum value. Returns "UNKNOWN" if not found.
template<typename E, std::size_t N> inline const char *enum_to_str(const EnumTable<E, N> &table, E val) {
  for (const auto &entry : table) {
    if (entry.value == val)
      return entry.label;
  }
  return "UNKNOWN";
}

/// Look up an enum value from a string label (case-insensitive). Returns fallback if not found.
template<typename E, std::size_t N> inline E enum_from_str(const EnumTable<E, N> &table, const char *str, E fallback) {
  if (!str)
    return fallback;
  for (const auto &entry : table) {
    if (strcasecmp(entry.label, str) == 0)
      return entry.value;
  }
  return fallback;
}

/// Find the index of an enum value in a table. Returns -1 if not found.
template<typename E, std::size_t N> inline int enum_index(const EnumTable<E, N> &table, E val) {
  for (std::size_t i = 0; i < N; i++) {
    if (table[i].value == val)
      return static_cast<int>(i);
  }
  return -1;
}

/// Decode a wire protocol byte to an enum value.
/// Returns std::nullopt if the byte is not found in the table.
template<typename E, std::size_t N>
inline std::optional<E> wire_to_enum(const EnumTable<E, N> &table, uint8_t byte_value) {
  for (const auto &entry : table) {
    if (entry.protocol_byte == byte_value)
      return entry.value;
  }
  return std::nullopt;
}

/// Encode an enum value to a wire protocol byte.
/// Returns std::nullopt if the enum value is not found in the table.
template<typename E, std::size_t N> inline std::optional<uint8_t> enum_to_wire(const EnumTable<E, N> &table, E val) {
  for (const auto &entry : table) {
    if (entry.value == val)
      return entry.protocol_byte;
  }
  return std::nullopt;
}

// ════════════════════════════════════════════════════════════════
// Enum tables — single source of truth for enum ↔ string mapping
// ════════════════════════════════════════════════════════════════

inline constexpr EnumTable<HPPower, 2> POWER_TABLE = {{
    {HPPower::OFF, 0x00, "OFF"},
    {HPPower::ON, 0x01, "ON"},
}};

inline constexpr EnumTable<HPMode, 5> MODE_TABLE = {{
    {HPMode::HEAT, 0x01, "HEAT"},
    {HPMode::DRY, 0x02, "DRY"},
    {HPMode::COOL, 0x03, "COOL"},
    {HPMode::FAN, 0x07, "FAN"},
    {HPMode::AUTO, 0x08, "AUTO"},
}};

inline constexpr EnumTable<HPFanMode, 6> FAN_TABLE = {{
    {HPFanMode::AUTO, 0x00, "AUTO"},
    {HPFanMode::QUIET, 0x01, "QUIET"},
    {HPFanMode::F1, 0x02, "1"},
    {HPFanMode::F2, 0x03, "2"},
    {HPFanMode::F3, 0x05, "3"},
    {HPFanMode::F4, 0x06, "4"},
}};

inline constexpr EnumTable<HPVaneMode, 7> VANE_TABLE = {{
    {HPVaneMode::AUTO, 0x00, "AUTO"},
    {HPVaneMode::V1, 0x01, "↑↑"},
    {HPVaneMode::V2, 0x02, "↑"},
    {HPVaneMode::V3, 0x03, "—"},
    {HPVaneMode::V4, 0x04, "↓"},
    {HPVaneMode::V5, 0x05, "↓↓"},
    {HPVaneMode::SWING, 0x07, "SWING"},
}};

inline constexpr EnumTable<HPWideVaneMode, 8> WIDEVANE_TABLE = {{
    {HPWideVaneMode::LEFT_LEFT, 0x01, "←←"},
    {HPWideVaneMode::LEFT, 0x02, "←"},
    {HPWideVaneMode::CENTER, 0x03, "|"},
    {HPWideVaneMode::RIGHT, 0x04, "→"},
    {HPWideVaneMode::RIGHT_RIGHT, 0x05, "→→"},
    {HPWideVaneMode::LEFT_RIGHT, 0x08, "←→"},
    {HPWideVaneMode::SWING, 0x0c, "SWING"},
    {HPWideVaneMode::AIRFLOW_CONTROL, 0x00, "AIRFLOW CONTROL"},
}};

inline constexpr EnumTable<HPStage, 7> STAGE_TABLE = {{
    {HPStage::IDLE, 0x00, "IDLE"},
    {HPStage::LOW, 0x01, "LOW"},
    {HPStage::GENTLE, 0x02, "GENTLE"},
    {HPStage::MEDIUM, 0x03, "MEDIUM"},
    {HPStage::MODERATE, 0x04, "MODERATE"},
    {HPStage::HIGH, 0x05, "HIGH"},
    {HPStage::DIFFUSE, 0x06, "DIFFUSE"},
}};

inline constexpr EnumTable<HPSubMode, 6> SUB_MODE_TABLE = {{
    {HPSubMode::NORMAL, 0x00, "NORMAL"},
    {HPSubMode::WARMUP, 0x01, "WARMUP"},
    {HPSubMode::DEFROST, 0x02, "DEFROST"},
    {HPSubMode::PREHEAT, 0x04, "PREHEAT"},
    {HPSubMode::STANDBY, 0x08, "STANDBY"},
    {HPSubMode::OFF, 0x10, "OFF"},
}};

inline constexpr EnumTable<HPAutoSubMode, 7> AUTO_SUB_MODE_TABLE = {{
    {HPAutoSubMode::AUTO_OFF, 0x00, "AUTO_OFF"},
    {HPAutoSubMode::AUTO_COOL, 0x01, "AUTO_COOL"},
    {HPAutoSubMode::AUTO_HEAT, 0x02, "AUTO_HEAT"},
    {HPAutoSubMode::AUTO_LEADER, 0x03, "AUTO_LEADER"},
    {HPAutoSubMode::AUTO_INACTIVE, 0x40, "AUTO_INACTIVE"},
    {HPAutoSubMode::AUTO_IDLE, 0x41, "AUTO_IDLE"},
    {HPAutoSubMode::AUTO_ACTIVE, 0x43, "AUTO_ACTIVE"},
}};

inline constexpr EnumTable<HPAirflowControl, 3> AIRFLOW_CONTROL_TABLE = {{
    {HPAirflowControl::EVEN, 0x00, "EVEN"},
    {HPAirflowControl::INDIRECT, 0x01, "INDIRECT"},
    {HPAirflowControl::DIRECT, 0x02, "DIRECT"},
}};

// ════════════════════════════════════════════════════════════════
// Convenience wrappers — thin inline aliases for type safety
// ════════════════════════════════════════════════════════════════

inline const char *hp_power_to_str(HPPower val) { return enum_to_str(POWER_TABLE, val); }
inline const char *hp_mode_to_str(HPMode val) { return enum_to_str(MODE_TABLE, val); }
inline const char *hp_fan_to_str(HPFanMode val) { return enum_to_str(FAN_TABLE, val); }
inline const char *hp_vane_to_str(HPVaneMode val) { return enum_to_str(VANE_TABLE, val); }
inline const char *hp_wide_vane_to_str(HPWideVaneMode val) { return enum_to_str(WIDEVANE_TABLE, val); }
inline const char *hp_stage_to_str(HPStage val) { return enum_to_str(STAGE_TABLE, val); }
inline const char *hp_sub_mode_to_str(HPSubMode val) { return enum_to_str(SUB_MODE_TABLE, val); }
inline const char *hp_auto_sub_mode_to_str(HPAutoSubMode val) { return enum_to_str(AUTO_SUB_MODE_TABLE, val); }
inline const char *hp_airflow_control_to_str(HPAirflowControl val) { return enum_to_str(AIRFLOW_CONTROL_TABLE, val); }

inline HPPower hp_power_from_str(const char *str) { return enum_from_str(POWER_TABLE, str, HPPower::UNKNOWN); }
inline HPMode hp_mode_from_str(const char *str) { return enum_from_str(MODE_TABLE, str, HPMode::UNKNOWN); }
inline HPFanMode hp_fan_from_str(const char *str) { return enum_from_str(FAN_TABLE, str, HPFanMode::UNKNOWN); }
inline HPVaneMode hp_vane_from_str(const char *str) { return enum_from_str(VANE_TABLE, str, HPVaneMode::UNKNOWN); }
inline HPWideVaneMode hp_wide_vane_from_str(const char *str) {
  return enum_from_str(WIDEVANE_TABLE, str, HPWideVaneMode::UNKNOWN);
}
inline HPStage hp_stage_from_str(const char *str) { return enum_from_str(STAGE_TABLE, str, HPStage::UNKNOWN); }
inline HPSubMode hp_sub_mode_from_str(const char *str) {
  return enum_from_str(SUB_MODE_TABLE, str, HPSubMode::UNKNOWN);
}
inline HPAutoSubMode hp_auto_sub_mode_from_str(const char *str) {
  return enum_from_str(AUTO_SUB_MODE_TABLE, str, HPAutoSubMode::UNKNOWN);
}
inline HPAirflowControl hp_airflow_control_from_str(const char *str) {
  return enum_from_str(AIRFLOW_CONTROL_TABLE, str, HPAirflowControl::UNKNOWN);
}

inline std::optional<HPPower> hp_power_from_wire(uint8_t byte) { return wire_to_enum(POWER_TABLE, byte); }
inline std::optional<HPMode> hp_mode_from_wire(uint8_t byte) { return wire_to_enum(MODE_TABLE, byte); }
inline std::optional<HPFanMode> hp_fan_from_wire(uint8_t byte) { return wire_to_enum(FAN_TABLE, byte); }
inline std::optional<HPVaneMode> hp_vane_from_wire(uint8_t byte) { return wire_to_enum(VANE_TABLE, byte); }
inline std::optional<HPWideVaneMode> hp_wide_vane_from_wire(uint8_t byte) { return wire_to_enum(WIDEVANE_TABLE, byte); }
inline std::optional<HPStage> hp_stage_from_wire(uint8_t byte) { return wire_to_enum(STAGE_TABLE, byte); }
inline std::optional<HPSubMode> hp_sub_mode_from_wire(uint8_t byte) { return wire_to_enum(SUB_MODE_TABLE, byte); }
inline std::optional<HPAutoSubMode> hp_auto_sub_mode_from_wire(uint8_t byte) {
  return wire_to_enum(AUTO_SUB_MODE_TABLE, byte);
}
inline std::optional<HPAirflowControl> hp_airflow_control_from_wire(uint8_t byte) {
  return wire_to_enum(AIRFLOW_CONTROL_TABLE, byte);
}

inline std::optional<uint8_t> hp_power_to_wire(HPPower val) { return enum_to_wire(POWER_TABLE, val); }
inline std::optional<uint8_t> hp_mode_to_wire(HPMode val) { return enum_to_wire(MODE_TABLE, val); }
inline std::optional<uint8_t> hp_fan_to_wire(HPFanMode val) { return enum_to_wire(FAN_TABLE, val); }
inline std::optional<uint8_t> hp_vane_to_wire(HPVaneMode val) { return enum_to_wire(VANE_TABLE, val); }
inline std::optional<uint8_t> hp_wide_vane_to_wire(HPWideVaneMode val) { return enum_to_wire(WIDEVANE_TABLE, val); }
inline std::optional<uint8_t> hp_stage_to_wire(HPStage val) { return enum_to_wire(STAGE_TABLE, val); }
inline std::optional<uint8_t> hp_sub_mode_to_wire(HPSubMode val) { return enum_to_wire(SUB_MODE_TABLE, val); }
inline std::optional<uint8_t> hp_auto_sub_mode_to_wire(HPAutoSubMode val) {
  return enum_to_wire(AUTO_SUB_MODE_TABLE, val);
}
inline std::optional<uint8_t> hp_airflow_control_to_wire(HPAirflowControl val) {
  return enum_to_wire(AIRFLOW_CONTROL_TABLE, val);
}

struct HeatpumpSettings {
  HPPower power = HPPower::UNKNOWN;
  HPMode mode = HPMode::UNKNOWN;
  std::optional<float> temperature = std::nullopt;
  HPFanMode fan = HPFanMode::UNKNOWN;
  HPVaneMode vane = HPVaneMode::UNKNOWN;
  HPWideVaneMode wide_vane = HPWideVaneMode::UNKNOWN;
  bool i_see = false;
  bool connected = false;
  HPStage stage = HPStage::UNKNOWN;
  HPSubMode sub_mode = HPSubMode::UNKNOWN;
  HPAutoSubMode auto_sub_mode = HPAutoSubMode::UNKNOWN;

  void reset_settings() {
    power = HPPower::UNKNOWN;
    mode = HPMode::UNKNOWN;
    temperature = std::nullopt;
    fan = HPFanMode::UNKNOWN;
    vane = HPVaneMode::UNKNOWN;
    wide_vane = HPWideVaneMode::UNKNOWN;
    stage = HPStage::UNKNOWN;
    sub_mode = HPSubMode::UNKNOWN;
    auto_sub_mode = HPAutoSubMode::UNKNOWN;
  }

  HeatpumpSettings &operator=(const HeatpumpSettings &other) = default;

  bool operator==(const HeatpumpSettings &other) const {
    bool temp_equal = false;
    if (!temperature.has_value() && !other.temperature.has_value()) {
      temp_equal = true;
    } else if (temperature.has_value() && other.temperature.has_value()) {
      temp_equal = std::abs(*temperature - *other.temperature) < 0.01f;
    }
    return power == other.power && mode == other.mode && temp_equal && fan == other.fan && vane == other.vane &&
           wide_vane == other.wide_vane && i_see == other.i_see;
  }

  bool operator!=(const HeatpumpSettings &other) const { return !(this->operator==(other)); }
};

struct WantedHeatpumpSettings : HeatpumpSettings {
  bool has_changed = false;
  bool has_been_sent = false;
  uint8_t nb_deferred_requests = 0;
  uint32_t last_change = 0;

  void reset_settings() {
    HeatpumpSettings::reset_settings();
    has_changed = false;
    has_been_sent = false;
  }

  WantedHeatpumpSettings &operator=(const WantedHeatpumpSettings &other) = default;

  WantedHeatpumpSettings &operator=(const HeatpumpSettings &other) {
    if (this != &other) {
      HeatpumpSettings::operator=(other);
    }
    return *this;
  }
};

struct HeatpumpStatus {
  float room_temperature = NAN;
  float outside_air_temperature = NAN;
  bool operating = false;
  float compressor_frequency = NAN;
  float input_power = NAN;
  float kwh = NAN;
  float runtime_hours = NAN;

  bool operator==(const HeatpumpStatus &other) const {
    return (std::isnan(room_temperature) ? std::isnan(other.room_temperature)
                                        : room_temperature == other.room_temperature) &&
           (std::isnan(outside_air_temperature) ? std::isnan(other.outside_air_temperature)
                                              : outside_air_temperature == other.outside_air_temperature) &&
           operating == other.operating &&
           (std::isnan(compressor_frequency) ? std::isnan(other.compressor_frequency)
                                            : compressor_frequency == other.compressor_frequency) &&
           (std::isnan(input_power) ? std::isnan(other.input_power) : input_power == other.input_power) &&
           (std::isnan(kwh) ? std::isnan(other.kwh) : kwh == other.kwh) &&
           (std::isnan(runtime_hours) ? std::isnan(other.runtime_hours) : runtime_hours == other.runtime_hours);
  }

  bool operator!=(const HeatpumpStatus &other) const { return !(*this == other); }
};

struct HeatpumpRunStates {
  HPAirflowControl airflow_control = HPAirflowControl::UNKNOWN;

  void reset_settings() { airflow_control = HPAirflowControl::UNKNOWN; }

  HeatpumpRunStates &operator=(const HeatpumpRunStates &other) = default;

  bool operator==(const HeatpumpRunStates &other) const { return airflow_control == other.airflow_control; }

  bool operator!=(const HeatpumpRunStates &other) const { return !(this->operator==(other)); }
};

struct WantedHeatpumpRunStates : HeatpumpRunStates {
  bool has_changed = false;
  bool has_been_sent = false;
  uint32_t last_change = 0;

  void reset_settings() {
    HeatpumpRunStates::reset_settings();
    has_changed = false;
    has_been_sent = false;
  }

  WantedHeatpumpRunStates &operator=(const WantedHeatpumpRunStates &other) = default;

  WantedHeatpumpRunStates &operator=(const HeatpumpRunStates &other) {
    if (this != &other) {
      HeatpumpRunStates::operator=(other);
    }
    return *this;
  }
};
