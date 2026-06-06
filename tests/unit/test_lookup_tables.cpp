/// test_lookup_tables.cpp — Regression tests for the EnumTable mapping API.
/// Deps: cn105_types.h (enum tables + helpers — production code, no copies)
///
/// Validates the wire↔enum↔string lookups that replaced the old parallel
/// *_MAP / * byte arrays. Exercises the real production helpers directly.
#include <gtest/gtest.h>
#include "cn105_types.h"

// ════════════════════════════════════════════════════════════════
// wire byte → enum  (hp_*_from_wire / wire_to_enum)
// ════════════════════════════════════════════════════════════════

TEST(EnumTables, PowerFromWire) {
    EXPECT_EQ(hp_power_from_wire(0x00), HPPower::OFF);
    EXPECT_EQ(hp_power_from_wire(0x01), HPPower::ON);
    EXPECT_FALSE(hp_power_from_wire(0xFF).has_value());
}

TEST(EnumTables, ModeFromWire) {
    EXPECT_EQ(hp_mode_from_wire(0x01), HPMode::HEAT);
    EXPECT_EQ(hp_mode_from_wire(0x02), HPMode::DRY);
    EXPECT_EQ(hp_mode_from_wire(0x03), HPMode::COOL);
    EXPECT_EQ(hp_mode_from_wire(0x07), HPMode::FAN);
    EXPECT_EQ(hp_mode_from_wire(0x08), HPMode::AUTO);
    // 0x0A could be a future "ECO" mode — must be nullopt, NOT a silent fallback.
    EXPECT_FALSE(hp_mode_from_wire(0x0A).has_value());
}

TEST(EnumTables, FanFromWire) {
    EXPECT_EQ(hp_fan_from_wire(0x00), HPFanMode::AUTO);
    EXPECT_EQ(hp_fan_from_wire(0x01), HPFanMode::QUIET);
    EXPECT_EQ(hp_fan_from_wire(0x06), HPFanMode::F4);
    EXPECT_FALSE(hp_fan_from_wire(0x09).has_value());
}

TEST(EnumTables, VaneFromWire) {
    EXPECT_EQ(hp_vane_from_wire(0x00), HPVaneMode::AUTO);
    EXPECT_EQ(hp_vane_from_wire(0x07), HPVaneMode::SWING);
    EXPECT_FALSE(hp_vane_from_wire(0x06).has_value());
}

TEST(EnumTables, WideVaneFromWire) {
    EXPECT_EQ(hp_wide_vane_from_wire(0x01), HPWideVaneMode::LEFT_LEFT);
    EXPECT_EQ(hp_wide_vane_from_wire(0x0c), HPWideVaneMode::SWING);
    EXPECT_EQ(hp_wide_vane_from_wire(0x00), HPWideVaneMode::AIRFLOW_CONTROL);
}

TEST(EnumTables, StageFromWire) {
    EXPECT_EQ(hp_stage_from_wire(0x00), HPStage::IDLE);
    EXPECT_EQ(hp_stage_from_wire(0x01), HPStage::LOW);
    EXPECT_EQ(hp_stage_from_wire(0x06), HPStage::DIFFUSE);
}

TEST(EnumTables, SubModeFromWire) {
    EXPECT_EQ(hp_sub_mode_from_wire(0x00), HPSubMode::NORMAL);
    EXPECT_EQ(hp_sub_mode_from_wire(0x02), HPSubMode::DEFROST);
}

TEST(EnumTables, AutoSubModeFromWire) {
    EXPECT_EQ(hp_auto_sub_mode_from_wire(0x00), HPAutoSubMode::AUTO_OFF);
    EXPECT_EQ(hp_auto_sub_mode_from_wire(0x43), HPAutoSubMode::AUTO_ACTIVE);
}

TEST(EnumTables, AirflowControlFromWire) {
    EXPECT_EQ(hp_airflow_control_from_wire(0x00), HPAirflowControl::EVEN);
    EXPECT_EQ(hp_airflow_control_from_wire(0x02), HPAirflowControl::DIRECT);
    EXPECT_FALSE(hp_airflow_control_from_wire(0x03).has_value());
}

// ════════════════════════════════════════════════════════════════
// enum → string  (hp_*_to_str)
// ════════════════════════════════════════════════════════════════

TEST(EnumTables, ModeToStr) {
    EXPECT_STREQ(hp_mode_to_str(HPMode::HEAT), "HEAT");
    EXPECT_STREQ(hp_mode_to_str(HPMode::AUTO), "AUTO");
    EXPECT_STREQ(hp_mode_to_str(HPMode::UNKNOWN), "UNKNOWN");
}

TEST(EnumTables, FanToStr) {
    EXPECT_STREQ(hp_fan_to_str(HPFanMode::AUTO), "AUTO");
    EXPECT_STREQ(hp_fan_to_str(HPFanMode::F4), "4");
}

TEST(EnumTables, VaneSwingToStr) {
    EXPECT_STREQ(hp_vane_to_str(HPVaneMode::SWING), "SWING");
}

TEST(EnumTables, AirflowControlToStr) {
    EXPECT_STREQ(hp_airflow_control_to_str(HPAirflowControl::EVEN), "EVEN");
    EXPECT_STREQ(hp_airflow_control_to_str(HPAirflowControl::DIRECT), "DIRECT");
}

// ════════════════════════════════════════════════════════════════
// string → enum  (hp_*_from_str, case-insensitive)
// ════════════════════════════════════════════════════════════════

TEST(EnumTables, ModeFromStr) {
    EXPECT_EQ(hp_mode_from_str("COOL"), HPMode::COOL);
    EXPECT_EQ(hp_mode_from_str("cool"), HPMode::COOL);  // case-insensitive
    EXPECT_EQ(hp_mode_from_str("TURBO"), HPMode::UNKNOWN);
}

TEST(EnumTables, PowerFromStr) {
    EXPECT_EQ(hp_power_from_str("ON"), HPPower::ON);
    EXPECT_EQ(hp_power_from_str("off"), HPPower::OFF);
}

// ════════════════════════════════════════════════════════════════
// enum → wire byte  (hp_*_to_wire) + round-trip
// ════════════════════════════════════════════════════════════════

TEST(EnumTables, ModeToWire) {
    EXPECT_EQ(hp_mode_to_wire(HPMode::COOL), std::optional<uint8_t>(0x03));
    EXPECT_FALSE(hp_mode_to_wire(HPMode::UNKNOWN).has_value());
}

TEST(EnumTables, WireRoundTrip) {
    // Every protocol byte that decodes must re-encode to the same byte.
    for (const auto &entry : MODE_TABLE) {
        auto decoded = hp_mode_from_wire(entry.protocol_byte);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(hp_mode_to_wire(*decoded), std::optional<uint8_t>(entry.protocol_byte));
    }
}

// ════════════════════════════════════════════════════════════════
// Table consistency — protocol bytes must be unique within a table
// ════════════════════════════════════════════════════════════════

template<typename Table> static void expect_unique_bytes(const Table &table, const char *name) {
    for (size_t i = 0; i < table.size(); ++i) {
        for (size_t j = i + 1; j < table.size(); ++j) {
            EXPECT_NE(table[i].protocol_byte, table[j].protocol_byte)
                << name << " bytes at index " << i << " and " << j << " collide";
        }
    }
}

TEST(EnumTables, NoByteCollisions) {
    expect_unique_bytes(POWER_TABLE, "POWER");
    expect_unique_bytes(MODE_TABLE, "MODE");
    expect_unique_bytes(FAN_TABLE, "FAN");
    expect_unique_bytes(VANE_TABLE, "VANE");
    expect_unique_bytes(WIDEVANE_TABLE, "WIDEVANE");
    expect_unique_bytes(STAGE_TABLE, "STAGE");
    expect_unique_bytes(SUB_MODE_TABLE, "SUB_MODE");
    expect_unique_bytes(AUTO_SUB_MODE_TABLE, "AUTO_SUB_MODE");
    expect_unique_bytes(AIRFLOW_CONTROL_TABLE, "AIRFLOW_CONTROL");
}
