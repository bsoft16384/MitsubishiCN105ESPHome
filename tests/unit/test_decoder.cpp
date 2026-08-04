/// test_decoder.cpp — Tests for decoder.h, the pure CN105 response decoders.
/// Deps: decoder.h (production code, no mocks needed)
///
/// Payloads here are the payload section of a 0x62 response, i.e. frame offset 5
/// onwards, so data[0] is the response code. Most are lifted from live captures
/// (see test_real_frames.cpp for the matching whole frames and checksums).
#include <gtest/gtest.h>
#include <cmath>
#include "decoder.h"

using namespace cn105_decoder;

// ════════════════════════════════════════════════════════════════
// payload_byte() — bounds checking
// ════════════════════════════════════════════════════════════════

TEST(DecoderPayloadByte, ReadsInRange) {
    const uint8_t data[] = {0x02, 0x11, 0x22, 0x33};
    EXPECT_EQ(payload_byte(data, 4, 0), 0x02);
    EXPECT_EQ(payload_byte(data, 4, 3), 0x33);
}

TEST(DecoderPayloadByte, SubstitutesDefaultOutOfRange) {
    const uint8_t data[] = {0x02, 0x11};
    EXPECT_EQ(payload_byte(data, 2, 2), 0x00);
    EXPECT_EQ(payload_byte(data, 2, 99, 0xEE), 0xEE);
    EXPECT_EQ(payload_byte(data, 2, -1, 0xEE), 0xEE);
    EXPECT_EQ(payload_byte(nullptr, 2, 0, 0xEE), 0xEE);
}

// ════════════════════════════════════════════════════════════════
// resolve_or_keep() — how an unrecognised byte is absorbed
// ════════════════════════════════════════════════════════════════

TEST(DecoderResolveOrKeep, DecodedValueWins) {
    EXPECT_EQ(resolve_or_keep<HPMode>(HPMode::COOL, HPMode::HEAT, HPMode::FAN), HPMode::COOL);
}

// An unrecognised byte must not be read as "the setting changed".
TEST(DecoderResolveOrKeep, KeepsCurrentWhenDecodeFailed) {
    EXPECT_EQ(resolve_or_keep<HPMode>(std::nullopt, HPMode::HEAT, HPMode::FAN), HPMode::HEAT);
}

TEST(DecoderResolveOrKeep, FallsBackOnlyWhenNothingKnownYet) {
    EXPECT_EQ(resolve_or_keep<HPMode>(std::nullopt, HPMode::UNKNOWN, HPMode::FAN), HPMode::FAN);
}

// ════════════════════════════════════════════════════════════════
// decode_settings() — 0x02
// ════════════════════════════════════════════════════════════════

namespace {
// Captured boot state: OFF, HEAT, 19.5 C, AUTO fan, AUTO vane.
const uint8_t SETTINGS_OFF_HEAT_19_5[] = {0x02, 0x00, 0x00, 0x00, 0x01, 0x1C, 0x00, 0x00,
                                          0x00, 0x00, 0x03, 0xA7, 0x00, 0x00, 0x00, 0x00};
// Captured: ON, HEAT, 21.5 C, fan 2, vane up.
const uint8_t SETTINGS_ON_HEAT_FAN2_VANEUP[] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x03, 0x02,
                                                0x00, 0x00, 0x03, 0xAB, 0x00, 0x00, 0x00, 0x00};
}  // namespace

TEST(DecoderSettings, RejectsShortPayload) {
    EXPECT_FALSE(decode_settings(SETTINGS_OFF_HEAT_19_5, SETTINGS_MIN_LEN - 1).has_value());
    EXPECT_FALSE(decode_settings(nullptr, 32).has_value());
}

TEST(DecoderSettings, AcceptsExactlyMinimumLength) {
    EXPECT_TRUE(decode_settings(SETTINGS_OFF_HEAT_19_5, SETTINGS_MIN_LEN).has_value());
}

TEST(DecoderSettings, DecodesCapturedBootState) {
    auto d = decode_settings(SETTINGS_OFF_HEAT_19_5, sizeof(SETTINGS_OFF_HEAT_19_5));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->power, HPPower::OFF);
    EXPECT_EQ(d->mode, HPMode::HEAT);
    EXPECT_FALSE(d->i_see);
    ASSERT_TRUE(d->temperature.has_value());
    EXPECT_FLOAT_EQ(*d->temperature, 19.5f);
    EXPECT_FALSE(d->legacy_temperature_encoding);
    EXPECT_EQ(d->fan, HPFanMode::AUTO);
    EXPECT_EQ(d->vane, HPVaneMode::AUTO);
    // This capture does report a horizontal vane: data[10] == 0x03, centre, no adjustment.
    EXPECT_TRUE(d->wide_vane_present);
    EXPECT_EQ(d->wide_vane, HPWideVaneMode::CENTER);
    EXPECT_FALSE(d->wide_vane_adjustment);
}

TEST(DecoderSettings, DecodesCapturedRunningState) {
    auto d = decode_settings(SETTINGS_ON_HEAT_FAN2_VANEUP, sizeof(SETTINGS_ON_HEAT_FAN2_VANEUP));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->power, HPPower::ON);
    EXPECT_EQ(d->mode, HPMode::HEAT);
    EXPECT_FLOAT_EQ(*d->temperature, 21.5f);
    EXPECT_EQ(d->fan, HPFanMode::F2);
    EXPECT_EQ(d->vane, HPVaneMode::V2);
}

// data[4] carries the i-See flag in bit 3; the mode occupies the low bits.
TEST(DecoderSettings, FoldsISeeFlagOutOfModeByte) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x0B, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->i_see);
    EXPECT_EQ(d->mode_byte, 0x03);
    EXPECT_EQ(d->mode, HPMode::COOL);
}

TEST(DecoderSettings, ReportsUnitsOwnAutoModeVerbatim) {
    // Mapping AUTO onto something Home Assistant understands is a caller decision;
    // the decoder must report what the wire actually said.
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->mode, HPMode::AUTO);
}

TEST(DecoderSettings, FlagsLegacyTemperatureEncoding) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->legacy_temperature_encoding);
    EXPECT_FALSE(d->temperature.has_value());
}

// Unknown bytes must surface as empty optionals, never as a silently substituted
// value — the caller is what decides to keep the previous reading.
TEST(DecoderSettings, LeavesUnrecognisedBytesEmpty) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B,
                        0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->power.has_value());
    EXPECT_FALSE(d->mode.has_value());
    EXPECT_FALSE(d->fan.has_value());
    EXPECT_FALSE(d->vane.has_value());
    // Raw bytes are retained so the caller can log what it did not understand.
    EXPECT_EQ(d->power_byte, 0x7F);
    EXPECT_EQ(d->fan_byte, 0x7C);
}

// data[10] == 0 is how a unit without a horizontal vane reports; decoding it as a
// position would publish a vane the unit does not have.
TEST(DecoderSettings, WideVaneAbsentWhenByteIsZero) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->wide_vane_present);
    EXPECT_FALSE(d->wide_vane.has_value());
    EXPECT_FALSE(d->wide_vane_adjustment);
}

TEST(DecoderSettings, DecodesWideVaneAndAdjustmentBit) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x83, 0xAB, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->wide_vane_present);
    EXPECT_EQ(d->wide_vane_byte, 0x03);
    EXPECT_EQ(d->wide_vane, HPWideVaneMode::CENTER);
    EXPECT_TRUE(d->wide_vane_adjustment);  // high nibble 0x80
}

TEST(DecoderSettings, WideVaneAdjustmentClearForOtherHighNibbles) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x03, 0xAB, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->wide_vane_present);
    EXPECT_FALSE(d->wide_vane_adjustment);
}

TEST(DecoderSettings, DecodesTargetHumidityWhenInRange) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0xAB, 0x46, 0x00, 0x00, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    ASSERT_TRUE(d->target_humidity.has_value());
    EXPECT_EQ(*d->target_humidity, 70);
}

TEST(DecoderSettings, TargetHumidityEmptyWhenUnsupportedOrOutOfRange) {
    uint8_t unsupported[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00};
    auto a = decode_settings(unsupported, sizeof(unsupported));
    ASSERT_TRUE(a.has_value());
    EXPECT_FALSE(a->target_humidity.has_value());
    EXPECT_EQ(a->humidity_byte, 0x00);

    uint8_t out_of_range[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0xAB, 0xC8, 0x00, 0x00, 0x00};
    auto b = decode_settings(out_of_range, sizeof(out_of_range));
    ASSERT_TRUE(b.has_value());
    EXPECT_FALSE(b->target_humidity.has_value());
    EXPECT_EQ(b->humidity_byte, 0xC8);  // retained for the log
}

// Airflow control is only real when data[10] == 0x80 *and* i-See is active.
TEST(DecoderSettings, AirflowControlReportedWithISee) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x09, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x80, 0xAB, 0x00, 0x00, 0x02, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->i_see);
    EXPECT_TRUE(d->airflow_control_reported);
    EXPECT_EQ(d->airflow_control, HPAirflowControl::DIRECT);
}

TEST(DecoderSettings, AirflowControlIsEvenWithoutISee) {
    // data[10] == 0x80 but i-See inactive: some units do this and the real mode is
    // unknowable, so EVEN is what we report.
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x01, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x80, 0xAB, 0x00, 0x00, 0x02, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->airflow_control_reported);
    EXPECT_EQ(d->airflow_control, HPAirflowControl::EVEN);
}

TEST(DecoderSettings, AirflowControlEmptyOnUnknownByteWhenReported) {
    uint8_t data[16] = {0x02, 0x00, 0x00, 0x01, 0x09, 0x1A, 0x00, 0x00,
                        0x00, 0x00, 0x80, 0xAB, 0x00, 0x00, 0x7F, 0x00};
    auto d = decode_settings(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->airflow_control_reported);
    EXPECT_FALSE(d->airflow_control.has_value());
    EXPECT_EQ(d->airflow_byte, 0x7F);
}

// ════════════════════════════════════════════════════════════════
// decode_room_temperature() — 0x03
// ════════════════════════════════════════════════════════════════

TEST(DecoderRoomTemp, RejectsShortPayload) {
    const uint8_t data[] = {0x03, 0x00, 0x00, 0x0D, 0x00, 0x00};
    EXPECT_FALSE(decode_room_temperature(data, ROOM_TEMP_MIN_LEN - 1).has_value());
}

TEST(DecoderRoomTemp, DecodesCapturedHalfDegreeReading) {
    const uint8_t data[] = {0x03, 0x00, 0x00, 0x0D, 0x00, 0x00, 0xAE, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_room_temperature(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    ASSERT_TRUE(d->room_temperature.has_value());
    EXPECT_FLOAT_EQ(*d->room_temperature, 23.0f);
    EXPECT_FALSE(d->room_temperature_is_legacy);
    EXPECT_TRUE(std::isnan(d->outside_air_temperature));  // data[5] <= 1 means no sensor
}

// The frame documented in hp_readings.cpp: room 24.0, outside 10.0, 68196 minutes.
TEST(DecoderRoomTemp, DecodesDocumentedFrameIncludingRuntime) {
    const uint8_t data[] = {0x03, 0x00, 0x00, 0x0E, 0x00, 0x94, 0xB0, 0xB0,
                            0xFE, 0x42, 0x00, 0x01, 0x0A, 0x64, 0x00, 0x00};
    auto d = decode_room_temperature(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FLOAT_EQ(*d->room_temperature, 24.0f);
    EXPECT_FLOAT_EQ(d->outside_air_temperature, 10.0f);
    ASSERT_TRUE(d->runtime_hours.has_value());
    EXPECT_NEAR(*d->runtime_hours, 68196.0f / 60.0f, 0.01f);
}

TEST(DecoderRoomTemp, FallsBackToLegacyOneDegreeMap) {
    // data[6] == 0 forces the coarse data[3] table: 10 + index.
    const uint8_t data[] = {0x03, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_room_temperature(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->room_temperature_is_legacy);
    ASSERT_TRUE(d->room_temperature.has_value());
    EXPECT_FLOAT_EQ(*d->room_temperature, 23.0f);
}

TEST(DecoderRoomTemp, LegacyByteOutOfRangeYieldsNoReading) {
    const uint8_t data[] = {0x03, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_room_temperature(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->room_temperature.has_value());
    EXPECT_EQ(d->legacy_room_temp_byte, 0x40);
}

TEST(DecoderRoomTemp, RuntimeAbsentOnShortPayload) {
    const uint8_t data[] = {0x03, 0x00, 0x00, 0x0D, 0x00, 0x00, 0xAE, 0x00};
    auto d = decode_room_temperature(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->runtime_hours.has_value());
}

TEST(DecoderRoomTemp, DecodesNegativeOutsideTemperature) {
    const uint8_t data[] = {0x03, 0x00, 0x00, 0x0D, 0x00, 0x78, 0xAE, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_room_temperature(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FLOAT_EQ(d->outside_air_temperature, -4.0f);  // (0x78 - 128) / 2
}

// ════════════════════════════════════════════════════════════════
// unit_adopted_remote_temperature() — the "which sensor?" heuristic
// ════════════════════════════════════════════════════════════════

TEST(DecoderRemoteAdoption, TrueWhenEchoIsWithinMargin) {
    EXPECT_TRUE(unit_adopted_remote_temperature(21.0f, 21.2f, true, 0.4f));
    EXPECT_TRUE(unit_adopted_remote_temperature(21.0f, 21.4f, true, 0.4f));  // exactly at margin
}

TEST(DecoderRemoteAdoption, FalseWhenEchoDiverges) {
    EXPECT_FALSE(unit_adopted_remote_temperature(19.0f, 21.2f, true, 0.4f));
}

TEST(DecoderRemoteAdoption, FalseWhenNotFeedingARemoteValue) {
    EXPECT_FALSE(unit_adopted_remote_temperature(21.0f, 21.0f, false, 0.4f));      // keep-alive off
    EXPECT_FALSE(unit_adopted_remote_temperature(21.0f, std::nullopt, true, 0.4f));  // nothing sent
}

TEST(DecoderRemoteAdoption, FalseWhenEitherReadingIsUnusable) {
    EXPECT_FALSE(unit_adopted_remote_temperature(std::nullopt, 21.0f, true, 0.4f));
    EXPECT_FALSE(unit_adopted_remote_temperature(NAN, 21.0f, true, 0.4f));
    EXPECT_FALSE(unit_adopted_remote_temperature(21.0f, NAN, true, 0.4f));
}

// ════════════════════════════════════════════════════════════════
// decode_status() — 0x06
// ════════════════════════════════════════════════════════════════

TEST(DecoderStatus, RejectsShortPayload) {
    const uint8_t data[] = {0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    EXPECT_FALSE(decode_status(data, STATUS_MIN_LEN - 1).has_value());
}

TEST(DecoderStatus, DecodesIdleCapture) {
    const uint8_t data[] = {0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_status(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->operating);
    EXPECT_FLOAT_EQ(d->compressor_frequency, 0.0f);
}

// Documented capture: operating, 8 W input, 133.6 kWh (0x0550 = 1360).
TEST(DecoderStatus, DecodesOperatingCaptureWithCounters) {
    const uint8_t data[] = {0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x05,
                            0x50, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_status(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->operating);
    EXPECT_EQ(d->raw_input_power, 8);
    EXPECT_EQ(d->raw_energy_usage, 0x0550);
    EXPECT_FLOAT_EQ(convert_energy_usage_to_kwh(d->raw_energy_usage, false), 136.0f);
}

// Several models emit noise on the frequency field while stopped.
TEST(DecoderStatus, ZeroesCompressorFrequencyWhenNotOperating) {
    const uint8_t data[] = {0x06, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_status(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->operating);
    EXPECT_FLOAT_EQ(d->compressor_frequency, 0.0f);
}

TEST(DecoderStatus, ReportsCompressorFrequencyWhenOperating) {
    const uint8_t data[] = {0x06, 0x00, 0x00, 0x2A, 0x01, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_status(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FLOAT_EQ(d->compressor_frequency, 42.0f);
}

TEST(DecoderStatus, AssemblesSixteenBitCounters) {
    const uint8_t data[] = {0x06, 0x00, 0x00, 0x00, 0x01, 0x12, 0x34, 0x00,
                            0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_status(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->raw_input_power, 0x1234);
    EXPECT_EQ(d->raw_energy_usage, 0x00FF);
}

// ════════════════════════════════════════════════════════════════
// Energy unit conversion
// ════════════════════════════════════════════════════════════════

TEST(DecoderConversion, WattsPassThroughByDefault) {
    EXPECT_FLOAT_EQ(convert_input_power_to_w(750.0f, false), 750.0f);
}

TEST(DecoderConversion, BtuPerSecondConvertsToWatts) {
    // 1 BTU/s = 3600 / 1055.056 W ~= 3.412 W
    EXPECT_NEAR(convert_input_power_to_w(1.0f, true), 3.412f, 0.001f);
}

TEST(DecoderConversion, EnergyIsTenthsOfKwhByDefault) {
    EXPECT_FLOAT_EQ(convert_energy_usage_to_kwh(1360.0f, false), 136.0f);
}

TEST(DecoderConversion, KbtuConvertsToKwh) {
    // 1 kBTU = 1055.056 kJ = 0.293 kWh
    EXPECT_NEAR(convert_energy_usage_to_kwh(1.0f, true), 0.29307f, 0.0001f);
}

// ════════════════════════════════════════════════════════════════
// decode_sub_modes() — 0x09
// ════════════════════════════════════════════════════════════════

TEST(DecoderSubModes, RejectsShortPayload) {
    const uint8_t data[] = {0x09, 0x00, 0x00, 0x00, 0x00};
    EXPECT_FALSE(decode_sub_modes(data, POWER_MIN_LEN - 1).has_value());
}

TEST(DecoderSubModes, DecodesIdleCapture) {
    const uint8_t data[] = {0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_sub_modes(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->sub_mode, HPSubMode::NORMAL);
    EXPECT_EQ(d->stage, HPStage::IDLE);
    EXPECT_EQ(d->auto_sub_mode, HPAutoSubMode::AUTO_OFF);
}

TEST(DecoderSubModes, DecodesLowStageCapture) {
    const uint8_t data[] = {0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_sub_modes(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->stage, HPStage::LOW);
}

TEST(DecoderSubModes, DecodesDefrostAndAutoHeat) {
    const uint8_t data[] = {0x09, 0x00, 0x00, 0x02, 0x03, 0x02, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_sub_modes(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->sub_mode, HPSubMode::DEFROST);
    EXPECT_EQ(d->stage, HPStage::MEDIUM);
    EXPECT_EQ(d->auto_sub_mode, HPAutoSubMode::AUTO_HEAT);
}

TEST(DecoderSubModes, LeavesUnrecognisedBytesEmpty) {
    const uint8_t data[] = {0x09, 0x00, 0x00, 0x7F, 0x7E, 0x7D, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_sub_modes(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->sub_mode.has_value());
    EXPECT_FALSE(d->stage.has_value());
    EXPECT_FALSE(d->auto_sub_mode.has_value());
    EXPECT_EQ(d->stage_byte, 0x7E);
}

// ════════════════════════════════════════════════════════════════
// decode_error_info() — 0x04
// ════════════════════════════════════════════════════════════════

TEST(DecoderErrorInfo, RejectsShortPayload) {
    const uint8_t data[] = {0x04, 0x00, 0x00, 0x00, 0x00};
    EXPECT_FALSE(decode_error_info(data, ERROR_INFO_MIN_LEN - 1).has_value());
}

TEST(DecoderErrorInfo, ReportsNoErrorWhenBothBytesClear) {
    const uint8_t data[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto d = decode_error_info(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_TRUE(d->no_error);
}

// Bit 7 is an "error reporting available" protocol flag. Treating it as part of the
// code would report a permanent fault on every healthy unit that sets it.
TEST(DecoderErrorInfo, MasksTheReportingAvailableFlag) {
    const uint8_t data[] = {0x04, 0x00, 0x00, 0x00, 0x80, 0x00};
    auto d = decode_error_info(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->code, 0x00);
    EXPECT_TRUE(d->no_error);
}

TEST(DecoderErrorInfo, ReportsRealErrorCodes) {
    const uint8_t data[] = {0x04, 0x00, 0x00, 0x00, 0x85, 0x0A};
    auto d = decode_error_info(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->no_error);
    EXPECT_EQ(d->code, 0x05);
    EXPECT_EQ(d->sub_code, 0x0A);
}

TEST(DecoderErrorInfo, SubCodeAloneCountsAsAnError) {
    const uint8_t data[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x0A};
    auto d = decode_error_info(data, sizeof(data));
    ASSERT_TRUE(d.has_value());
    EXPECT_FALSE(d->no_error);
}
