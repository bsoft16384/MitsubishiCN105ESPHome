/// test_protocol.cpp — Tests for cn105_protocol.h pure protocol functions.
/// Deps: cn105_protocol.h (production code, no mocks needed)
///
/// These tests validate the ACTUAL production functions, not standalone copies.
/// Any regression in cn105_protocol.h will be caught here.
#include <gtest/gtest.h>
#include "cn105_protocol.h"

using namespace cn105_protocol;

// ════════════════════════════════════════════════════════════════
// checksum()
// ════════════════════════════════════════════════════════════════

TEST(ProtocolChecksum, ConnectPacket) {
    uint8_t pkt[] = {0xfc, 0x5a, 0x01, 0x30, 0x02, 0xca, 0x01};
    EXPECT_EQ(checksum(pkt, 7), 0xa8);
}

TEST(ProtocolChecksum, InfoPacket) {
    uint8_t pkt[] = {0xfc, 0x42, 0x01, 0x30, 0x10,
                     0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(checksum(pkt, 21), 0x7b);
}

TEST(ProtocolChecksum, ZeroLength) {
    uint8_t pkt[] = {0x00};
    EXPECT_EQ(checksum(pkt, 0), 0xfc);
}

TEST(ProtocolChecksum, Overflow) {
    // 0xfc + 0x04 = 0x100 → wraps to 0x00 in uint8_t, checksum = (0xfc - 0x00) & 0xff = 0xfc
    uint8_t pkt[] = {0xfc, 0x04};
    EXPECT_EQ(checksum(pkt, 2), 0xfc);
}

TEST(ProtocolChecksum, RealSettingsResponse) {
    // Real captured frame: FC 62 01 30 10 02 00 00 00 08 09 00 01 00 00 03 AC 00 00 00 00 (9A)
    uint8_t pkt[] = {0xFC, 0x62, 0x01, 0x30, 0x10,
                     0x02, 0x00, 0x00, 0x00, 0x08, 0x09, 0x00, 0x01,
                     0x00, 0x00, 0x03, 0xAC, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(checksum(pkt, 21), 0x9A);
}

// ════════════════════════════════════════════════════════════════
// encode_temperature_b() — half-degree target temperature byte
// ════════════════════════════════════════════════════════════════

TEST(ProtocolEncodeTemp, KnownValues) {
    EXPECT_EQ(encode_temperature_b(16.0f), 0xA0);
    EXPECT_EQ(encode_temperature_b(19.5f), 0xA7);
    EXPECT_EQ(encode_temperature_b(21.5f), 0xAB);
    EXPECT_EQ(encode_temperature_b(22.0f), 0xAC);
    EXPECT_EQ(encode_temperature_b(26.0f), 0xB4);
    EXPECT_EQ(encode_temperature_b(31.0f), 0xBE);
}

TEST(ProtocolEncodeTemp, RoundsToHalfDegree) {
    // 22.3 → round(44.6) = 45 → 45 + 128 = 173 = 0xAD (22.5°C)
    EXPECT_EQ(encode_temperature_b(22.3f), encode_temperature_b(22.5f));
}

// ════════════════════════════════════════════════════════════════
// encode_remote_temperature() — two-byte SET remote temp packet
// ════════════════════════════════════════════════════════════════

TEST(ProtocolEncodeRemoteTemp, KeepAlive_20_9C) {
    uint8_t enc_a, enc_b;
    encode_remote_temperature(20.9f, enc_a, enc_b);
    // round(20.9*2) = round(41.8) = 42 ; enc_a = 42-16 = 0x1A ; enc_b = 42+128 = 0xAA
    EXPECT_EQ(enc_a, 0x1A);
    EXPECT_EQ(enc_b, 0xAA);
}

TEST(ProtocolEncodeRemoteTemp, Exact_21_0C) {
    uint8_t enc_a, enc_b;
    encode_remote_temperature(21.0f, enc_a, enc_b);
    EXPECT_EQ(enc_a, 0x1A);  // 42 - 16
    EXPECT_EQ(enc_b, 0xAA);  // 42 + 128
}

TEST(ProtocolEncodeRemoteTemp, ClampsLow) {
    uint8_t enc_a, enc_b;
    encode_remote_temperature(2.0f, enc_a, enc_b);  // below 8.0 floor
    // clamped to 8.0 → round(16) = 16 ; enc_a = 0x00 ; enc_b = 0x90
    EXPECT_EQ(enc_a, 0x00);
    EXPECT_EQ(enc_b, 0x90);
}

TEST(ProtocolEncodeRemoteTemp, ClampsHigh) {
    uint8_t enc_a, enc_b;
    encode_remote_temperature(45.0f, enc_a, enc_b);  // above 37.5 ceiling
    // clamped to 37.5 → round(75) = 75 ; enc_a = 75-16 = 0x3B ; enc_b = 75+128 = 0xCB
    EXPECT_EQ(enc_a, 0x3B);
    EXPECT_EQ(enc_b, 0xCB);
}

// ════════════════════════════════════════════════════════════════
// normalize_setpoint() — half-degree rounding + clamp to the unit's range
// ════════════════════════════════════════════════════════════════

TEST(ProtocolNormalizeSetpoint, RoundsToHalfDegree) {
    EXPECT_FLOAT_EQ(normalize_setpoint(22.3f), 22.5f);
    EXPECT_FLOAT_EQ(normalize_setpoint(22.1f), 22.0f);
    EXPECT_FLOAT_EQ(normalize_setpoint(20.75f), 21.0f);
}

TEST(ProtocolNormalizeSetpoint, PassesThroughInRangeValues) {
    EXPECT_FLOAT_EQ(normalize_setpoint(SETPOINT_MIN_C), SETPOINT_MIN_C);
    EXPECT_FLOAT_EQ(normalize_setpoint(SETPOINT_MAX_C), SETPOINT_MAX_C);
    EXPECT_FLOAT_EQ(normalize_setpoint(21.5f), 21.5f);
}

// Regression: low-temperature protection asks for low_temp_temp + low_temp_hysteresis,
// which is 12 C with the shipped defaults. Clamping to anything below the unit's real
// minimum meant the unit raised it to 16, the readback never matched what we commanded,
// and the reconciler re-issued the command for as long as LTP stayed active.
TEST(ProtocolNormalizeSetpoint, ClampsBelowUnitMinimum) {
    EXPECT_FLOAT_EQ(normalize_setpoint(12.0f), SETPOINT_MIN_C);
    EXPECT_FLOAT_EQ(normalize_setpoint(8.0f), SETPOINT_MIN_C);
    EXPECT_FLOAT_EQ(normalize_setpoint(-5.0f), SETPOINT_MIN_C);
}

TEST(ProtocolNormalizeSetpoint, ClampsAboveUnitMaximum) {
    EXPECT_FLOAT_EQ(normalize_setpoint(35.0f), SETPOINT_MAX_C);
    EXPECT_FLOAT_EQ(normalize_setpoint(31.4f), SETPOINT_MAX_C);
}

// Every normalized setpoint must survive the encode step the SET packet uses.
TEST(ProtocolNormalizeSetpoint, RoundTripsThroughEncoding) {
    for (float raw = 0.0f; raw <= 40.0f; raw += 0.1f) {
        const float normalized = normalize_setpoint(raw);
        const uint8_t encoded = encode_temperature_b(normalized);
        EXPECT_FLOAT_EQ((encoded - 128) / 2.0f, normalized) << "raw=" << raw;
    }
}
