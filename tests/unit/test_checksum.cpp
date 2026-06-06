/// test_checksum.cpp — Regression tests for cn105_protocol::checksum().
/// Deps: cn105_protocol.h (production checksum), cn105_types.h (CONNECT constant)
///
/// These tests call the ACTUAL production function — no local copy.
#include <gtest/gtest.h>
#include "cn105_protocol.h"
#include "cn105_types.h"

using cn105_protocol::checksum;

TEST(CheckSumTest, ConnectPacketHasCorrectChecksum) {
    // CONNECT constant: {0xfc, 0x5a, 0x01, 0x30, 0x02, 0xca, 0x01, 0xa8}
    // Last byte (0xa8) IS the checksum of the first 7 bytes.
    uint8_t packet[CONNECT_LEN];
    memcpy(packet, CONNECT, CONNECT_LEN);

    uint8_t expected = packet[CONNECT_LEN - 1];  // 0xa8
    uint8_t computed = checksum(packet, CONNECT_LEN - 1);

    EXPECT_EQ(computed, expected)
        << "checksum of CONNECT packet should equal its last byte (0xA8)";
}

TEST(CheckSumTest, InfoPacketSettings) {
    // Info request for settings (0x02):
    // FC 42 01 30 10 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 7B
    uint8_t packet[22] = {0xFC, 0x42, 0x01, 0x30, 0x10, 0x02, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(checksum(packet, 21), 0x7B);
}

TEST(CheckSumTest, AllZeroPacket) {
    uint8_t packet[10] = {0};
    // sum=0, (0xfc - 0) & 0xff = 0xfc
    EXPECT_EQ(checksum(packet, 10), 0xFC);
}

TEST(CheckSumTest, OverflowHandling) {
    // sum of all bytes = 0xFF * 4 = 0x3FC, truncated to uint8_t = 0xFC
    // (0xfc - 0xfc) & 0xff = 0x00
    uint8_t packet[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(checksum(packet, 4), 0x00);
}

TEST(CheckSumTest, SingleBytePacket) {
    uint8_t packet[1] = {0x42};
    // (0xfc - 0x42) & 0xff = 0xBA
    EXPECT_EQ(checksum(packet, 1), 0xBA);
}

TEST(CheckSumTest, ZeroLength) {
    uint8_t packet[1] = {0x00};
    EXPECT_EQ(checksum(packet, 0), 0xFC);
}
