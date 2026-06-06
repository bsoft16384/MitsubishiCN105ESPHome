/// test_temperature.cpp — Temperature encoding tests against real captured bytes.
/// Deps: cn105_protocol.h (production encoders, no copies)
///
/// The expected byte values come from real UART captures, so they are an
/// independent ground truth for the production encode_* functions.
#include <gtest/gtest.h>
#include "cn105_protocol.h"

using cn105_protocol::encode_temperature_b;
using cn105_protocol::encode_remote_temperature;

// ════════════════════════════════════════════════════════════════
// encode_temperature_b() — target setpoint byte (encoding B)
// ════════════════════════════════════════════════════════════════

struct TempByte { float celsius; uint8_t byte; };

TEST(TemperatureEncodeTest, KnownCapturedBytes) {
    const TempByte cases[] = {
        {10.0f, 0x94}, {16.0f, 0xA0}, {18.5f, 0xA5}, {19.5f, 0xA7},
        {21.5f, 0xAB}, {22.0f, 0xAC}, {26.0f, 0xB4}, {26.5f, 0xB5}, {31.0f, 0xBE},
    };
    for (const auto &c : cases) {
        EXPECT_EQ(encode_temperature_b(c.celsius), c.byte)
            << "encode_temperature_b(" << c.celsius << ")";
    }
}

TEST(TemperatureEncodeTest, MonotonicAcrossRange) {
    // Each +0.5°C step must increase the encoded byte by exactly 1.
    for (float t = 16.0f; t < 31.0f; t += 0.5f) {
        EXPECT_EQ(encode_temperature_b(t + 0.5f), encode_temperature_b(t) + 1)
            << "step at " << t << "°C";
    }
}

TEST(TemperatureEncodeTest, RoundsToNearestHalfDegree) {
    EXPECT_EQ(encode_temperature_b(26.3f), encode_temperature_b(26.5f));
    EXPECT_EQ(encode_temperature_b(26.2f), encode_temperature_b(26.0f));
}

// ════════════════════════════════════════════════════════════════
// encode_remote_temperature() — two-byte remote temp (enc_a + enc_b)
// ════════════════════════════════════════════════════════════════

TEST(RemoteTempEncodeTest, KnownCapturedBytes) {
    uint8_t a, b;

    encode_remote_temperature(22.8f, a, b);  // real SET packet: 1E AE
    EXPECT_EQ(a, 0x1E);
    EXPECT_EQ(b, 0xAE);

    encode_remote_temperature(10.0f, a, b);
    EXPECT_EQ(a, 0x04);  // 20 - 16
    EXPECT_EQ(b, 0x94);  // 20 + 128
}

TEST(RemoteTempEncodeTest, EncAEncBDifferBy144) {
    // By construction enc_b = enc_a + 144 (the two encodings differ by a fixed offset).
    for (float t = 8.0f; t <= 37.5f; t += 0.5f) {
        uint8_t a, b;
        encode_remote_temperature(t, a, b);
        EXPECT_EQ(static_cast<uint8_t>(a + 144), b) << "at " << t << "°C";
    }
}
