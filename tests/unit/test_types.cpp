/// test_types.cpp — Regression tests for the CN105 protocol structs.
/// Deps: cn105_types.h (production structs + enums, no copies)
///
/// Covers reset_settings(), equality operators (including NaN and optional
/// temperature handling) on the enum-based settings/status/run-state structs.
#include <gtest/gtest.h>
#include <cmath>
#include "cn105_types.h"

// ════════════════════════════════════════════════════════════════
// HeatpumpSettings
// ════════════════════════════════════════════════════════════════

TEST(HeatpumpSettingsTest, ResetSettingsRestoresUnknown) {
    HeatpumpSettings s{};
    s.power = HPPower::ON;
    s.mode = HPMode::COOL;
    s.temperature = 24.0f;
    s.fan = HPFanMode::AUTO;
    s.vane = HPVaneMode::SWING;
    s.wide_vane = HPWideVaneMode::CENTER;
    s.stage = HPStage::HIGH;

    s.reset_settings();

    EXPECT_EQ(s.power, HPPower::UNKNOWN);
    EXPECT_EQ(s.mode, HPMode::UNKNOWN);
    EXPECT_FALSE(s.temperature.has_value());
    EXPECT_EQ(s.fan, HPFanMode::UNKNOWN);
    EXPECT_EQ(s.vane, HPVaneMode::UNKNOWN);
    EXPECT_EQ(s.wide_vane, HPWideVaneMode::UNKNOWN);
    EXPECT_EQ(s.stage, HPStage::UNKNOWN);
}

TEST(HeatpumpSettingsTest, EqualityIdentical) {
    HeatpumpSettings a{};
    a.power = HPPower::ON;
    a.mode = HPMode::HEAT;
    a.temperature = 22.0f;
    a.fan = HPFanMode::AUTO;
    a.vane = HPVaneMode::AUTO;
    a.wide_vane = HPWideVaneMode::CENTER;

    HeatpumpSettings b = a;
    EXPECT_TRUE(a == b);
}

TEST(HeatpumpSettingsTest, EqualityBothTemperatureUnset) {
    HeatpumpSettings a{};
    HeatpumpSettings b{};
    // Both temperatures are nullopt → considered equal.
    EXPECT_TRUE(a == b);
}

TEST(HeatpumpSettingsTest, InequalityOneTemperatureUnset) {
    HeatpumpSettings a{};
    a.temperature = 22.0f;
    HeatpumpSettings b{};  // temperature stays nullopt
    EXPECT_TRUE(a != b);
}

TEST(HeatpumpSettingsTest, TemperatureToleranceWithinEpsilon) {
    HeatpumpSettings a{};
    a.temperature = 22.000f;
    HeatpumpSettings b = a;
    b.temperature = 22.005f;  // within the 0.01 tolerance
    EXPECT_TRUE(a == b);
}

TEST(HeatpumpSettingsTest, InequalityDifferentMode) {
    HeatpumpSettings a{};
    a.mode = HPMode::HEAT;
    HeatpumpSettings b = a;
    b.mode = HPMode::COOL;
    EXPECT_TRUE(a != b);
}

TEST(HeatpumpSettingsTest, EqualityIgnoresStageAndSubMode) {
    // operator== intentionally does not compare stage / sub_mode / auto_sub_mode.
    HeatpumpSettings a{};
    a.mode = HPMode::HEAT;
    HeatpumpSettings b = a;
    b.stage = HPStage::HIGH;
    b.sub_mode = HPSubMode::DEFROST;
    EXPECT_TRUE(a == b);
}

// ════════════════════════════════════════════════════════════════
// WantedHeatpumpSettings
// ════════════════════════════════════════════════════════════════

TEST(WantedHeatpumpSettingsTest, ResetClearsFlags) {
    WantedHeatpumpSettings ws{};
    ws.has_changed = true;
    ws.has_been_sent = true;
    ws.power = HPPower::ON;
    ws.temperature = 24.0f;

    ws.reset_settings();

    EXPECT_FALSE(ws.has_changed);
    EXPECT_FALSE(ws.has_been_sent);
    EXPECT_EQ(ws.power, HPPower::UNKNOWN);
    EXPECT_FALSE(ws.temperature.has_value());
}

TEST(WantedHeatpumpSettingsTest, AssignFromBaseSettings) {
    HeatpumpSettings base{};
    base.mode = HPMode::COOL;
    base.temperature = 20.0f;

    WantedHeatpumpSettings ws{};
    ws = base;  // uses the HeatpumpSettings assignment overload

    EXPECT_EQ(ws.mode, HPMode::COOL);
    ASSERT_TRUE(ws.temperature.has_value());
    EXPECT_FLOAT_EQ(*ws.temperature, 20.0f);
}

// ════════════════════════════════════════════════════════════════
// HeatpumpStatus — NaN-aware equality
// ════════════════════════════════════════════════════════════════

TEST(HeatpumpStatusTest, EqualityWithNaN) {
    HeatpumpStatus a{};  // room/outside/freq/power/kwh/runtime default to NAN
    HeatpumpStatus b = a;
    EXPECT_TRUE(a == b) << "Two fresh statuses (all NaN) should compare equal";
}

TEST(HeatpumpStatusTest, InequalityDifferentRoomTemp) {
    HeatpumpStatus a{};
    a.room_temperature = 21.0f;
    HeatpumpStatus b = a;
    b.room_temperature = 22.0f;
    EXPECT_TRUE(a != b);
}

TEST(HeatpumpStatusTest, InequalityNaNVsValue) {
    HeatpumpStatus a{};
    a.room_temperature = NAN;
    HeatpumpStatus b = a;
    b.room_temperature = 21.0f;
    EXPECT_TRUE(a != b);
}

TEST(HeatpumpStatusTest, InequalityOperating) {
    HeatpumpStatus a{};
    a.operating = false;
    HeatpumpStatus b = a;
    b.operating = true;
    EXPECT_TRUE(a != b);
}

// ════════════════════════════════════════════════════════════════
// HeatpumpRunStates
// ════════════════════════════════════════════════════════════════

TEST(HeatpumpRunStatesTest, ResetSettings) {
    HeatpumpRunStates rs{};
    rs.airflow_control = HPAirflowControl::DIRECT;

    rs.reset_settings();

    EXPECT_EQ(rs.airflow_control, HPAirflowControl::UNKNOWN);
}

TEST(HeatpumpRunStatesTest, EqualityOperator) {
    HeatpumpRunStates a{};
    a.airflow_control = HPAirflowControl::EVEN;

    HeatpumpRunStates b = a;
    EXPECT_TRUE(a == b);

    b.airflow_control = HPAirflowControl::DIRECT;
    EXPECT_TRUE(a != b);
}

TEST(WantedRunStatesTest, ResetClearsFlags) {
    WantedHeatpumpRunStates wrs{};
    wrs.has_changed = true;
    wrs.has_been_sent = true;
    wrs.airflow_control = HPAirflowControl::DIRECT;

    wrs.reset_settings();

    EXPECT_FALSE(wrs.has_changed);
    EXPECT_FALSE(wrs.has_been_sent);
    EXPECT_EQ(wrs.airflow_control, HPAirflowControl::UNKNOWN);
}

// ════════════════════════════════════════════════════════════════
// WantedHeatpumpSettings::has_payload()
// ════════════════════════════════════════════════════════════════

// Regression: a control() call flagged as changed but with every field left unset
// (e.g. because the reconciler suppressed a repeat command) used to build a SET frame
// with no control flags — a no-op on the wire that still deferred the next info cycle.
TEST(WantedHeatpumpSettingsTest, HasNoPayloadWhenNothingIsSet) {
    WantedHeatpumpSettings wanted;
    wanted.has_changed = true;
    wanted.last_change = 12345;
    EXPECT_FALSE(wanted.has_payload());
}

TEST(WantedHeatpumpSettingsTest, HasPayloadForEachIndividualField) {
    {
        WantedHeatpumpSettings w;
        w.power = HPPower::ON;
        EXPECT_TRUE(w.has_payload());
    }
    {
        WantedHeatpumpSettings w;
        w.mode = HPMode::HEAT;
        EXPECT_TRUE(w.has_payload());
    }
    {
        WantedHeatpumpSettings w;
        w.temperature = 21.0f;
        EXPECT_TRUE(w.has_payload());
    }
    {
        WantedHeatpumpSettings w;
        w.fan = HPFanMode::AUTO;  // AUTO is a real request, not "unset"
        EXPECT_TRUE(w.has_payload());
    }
    {
        WantedHeatpumpSettings w;
        w.vane = HPVaneMode::SWING;
        EXPECT_TRUE(w.has_payload());
    }
    {
        WantedHeatpumpSettings w;
        w.wide_vane = HPWideVaneMode::CENTER;
        EXPECT_TRUE(w.has_payload());
    }
}

TEST(WantedHeatpumpSettingsTest, ResetClearsPayload) {
    WantedHeatpumpSettings wanted;
    wanted.mode = HPMode::COOL;
    wanted.temperature = 24.0f;
    ASSERT_TRUE(wanted.has_payload());

    wanted.reset_settings();
    EXPECT_FALSE(wanted.has_payload());
}
