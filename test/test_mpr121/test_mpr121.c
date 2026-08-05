// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the MPR121 capacitive-touch codec (services/peripherals/mpr121): decoding the touch-status
// word into an electrode bitmask (masking proximity / over-current), the per-electrode
// touched test, the proximity / over-current flags, the 10-bit filtered/baseline word, and the
// exact bytes of the register bring-up sequence. The I2C read/write is ESP32-only.

#include "services/peripherals/mpr121/mpr121.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_touched_decode()
{
    // low byte -> electrodes 0..7; here electrodes 0 and 2.
    uint16_t m = pc_mpr121_touched(0x05, 0x00);
    TEST_ASSERT_EQUAL_HEX16(0x0005, m);
    TEST_ASSERT_TRUE(pc_mpr121_is_touched(m, 0));
    TEST_ASSERT_TRUE(pc_mpr121_is_touched(m, 2));
    TEST_ASSERT_FALSE(pc_mpr121_is_touched(m, 1));

    // high byte bits 0..3 -> electrodes 8..11.
    m = pc_mpr121_touched(0x00, 0x0F);
    TEST_ASSERT_EQUAL_HEX16(0x0F00, m);
    TEST_ASSERT_TRUE(pc_mpr121_is_touched(m, 8));
    TEST_ASSERT_TRUE(pc_mpr121_is_touched(m, 11));
    TEST_ASSERT_FALSE(pc_mpr121_is_touched(m, 7));
    TEST_ASSERT_FALSE(pc_mpr121_is_touched(m, 12)); // out of range
}

void test_prox_and_overcurrent_masked()
{
    // Proximity (status bit 12 = high-byte bit 4) and OVCF (bit 15 = high-byte bit 7) must not
    // leak into the 12-electrode mask.
    TEST_ASSERT_EQUAL_HEX16(0x0000, pc_mpr121_touched(0x00, 0x10));
    TEST_ASSERT_TRUE(pc_mpr121_proximity(0x10));
    TEST_ASSERT_FALSE(pc_mpr121_proximity(0x00));

    TEST_ASSERT_EQUAL_HEX16(0x0000, pc_mpr121_touched(0x00, 0x80));
    TEST_ASSERT_TRUE(pc_mpr121_overcurrent(0x80));
    TEST_ASSERT_FALSE(pc_mpr121_overcurrent(0x10));
}

void test_word10()
{
    TEST_ASSERT_EQUAL_UINT16(1023, pc_mpr121_word10(0xFF, 0x03)); // max 10-bit
    TEST_ASSERT_EQUAL_UINT16(0x012A, pc_mpr121_word10(0x2A, 0x01));
    TEST_ASSERT_EQUAL_UINT16(0, pc_mpr121_word10(0x00, 0x04)); // bits above 10 dropped
}

void test_build_init_bytes()
{
    uint8_t seq[MPR121_INIT_MAX];
    size_t n = pc_mpr121_build_init(seq, sizeof(seq), MPR121_ELECTRODES, 12, 6);
    TEST_ASSERT_EQUAL_INT(82, (int)n);

    TEST_ASSERT_EQUAL_HEX8(0x80, seq[0]); // soft reset ...
    TEST_ASSERT_EQUAL_HEX8(0x63, seq[1]);
    TEST_ASSERT_EQUAL_HEX8(0x5E, seq[2]); // ECR stop
    TEST_ASSERT_EQUAL_HEX8(0x00, seq[3]);
    TEST_ASSERT_EQUAL_HEX8(0x2B, seq[4]); // MHDR filter default
    TEST_ASSERT_EQUAL_HEX8(0x01, seq[5]);

    // first threshold pair (electrode 0) begins right after the 26-byte fixed prologue.
    TEST_ASSERT_EQUAL_HEX8(0x41, seq[26]); // touch th reg
    TEST_ASSERT_EQUAL_HEX8(12, seq[27]);
    TEST_ASSERT_EQUAL_HEX8(0x42, seq[28]); // release th reg
    TEST_ASSERT_EQUAL_HEX8(6, seq[29]);
    // electrode 11 thresholds: 0x41 + 22 = 0x57 / 0x58 at offset 26 + 11*4 = 70.
    TEST_ASSERT_EQUAL_HEX8(0x57, seq[70]);
    TEST_ASSERT_EQUAL_HEX8(0x58, seq[72]);

    // ECR-start must be the final pair, enabling 12 electrodes with baseline tracking.
    TEST_ASSERT_EQUAL_HEX8(0x5E, seq[80]);
    TEST_ASSERT_EQUAL_HEX8(0x8C, seq[81]);
}

void test_build_init_guards()
{
    uint8_t seq[MPR121_INIT_MAX];
    // one electrode: 26 fixed + 4 threshold + 8 tail = 38 bytes; ECR enables 1 electrode.
    size_t n = pc_mpr121_build_init(seq, sizeof(seq), 1, 12, 6);
    TEST_ASSERT_EQUAL_INT(38, (int)n);
    TEST_ASSERT_EQUAL_HEX8(0x81, seq[n - 1]);

    TEST_ASSERT_EQUAL_INT(0, (int)pc_mpr121_build_init(seq, 10, 12, 12, 6));           // buffer too small
    TEST_ASSERT_EQUAL_INT(0, (int)pc_mpr121_build_init(seq, sizeof(seq), 0, 12, 6));   // n = 0
    TEST_ASSERT_EQUAL_INT(0, (int)pc_mpr121_build_init(seq, sizeof(seq), 13, 12, 6));  // n > 12
    TEST_ASSERT_EQUAL_INT(0, (int)pc_mpr121_build_init(NULL, sizeof(seq), 12, 12, 6)); // null buf
}

// begin() replays the bring-up sequence as register/value pairs, soft reset first. The capture
// shows every pair actually reached the bus in the order the builder laid them down.
void test_begin_replays_the_init_sequence()
{
    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_mpr121_begin(0x5A));

    uint8_t want[MPR121_INIT_MAX];
    size_t n = pc_mpr121_build_init(want, sizeof(want), MPR121_ELECTRODES, PC_MPR121_TOUCH_THRESHOLD,
                                    PC_MPR121_RELEASE_THRESHOLD);
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    // One transfer per pair, and the concatenated stream is the sequence itself.
    TEST_ASSERT_EQUAL_UINT32(n / 2, pc_bus_host_count());
    uint32_t got = 0;
    const uint8_t *tx = pc_bus_host_written(&got);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, tx, n);

    // The soft reset is first, which is what lets the rest of the sequence take.
    TEST_ASSERT_EQUAL_HEX8(0x80, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(0x63, tx[1]);
}

// A touch read addresses register 0 and decodes the two status bytes that come back.
void test_read_touched_decodes_the_reply()
{
    pc_bus_host_reset();
    const uint8_t status[2] = {0x05, 0x08}; // electrodes 0 and 2 touched, plus bit 11
    pc_bus_host_preload(status, sizeof(status));

    TEST_ASSERT_EQUAL_UINT16(pc_mpr121_touched(0x05, 0x08), pc_mpr121_read_touched());

    uint32_t n = 0;
    const uint8_t *w = pc_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, w[0]); // the touch status register
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_touched_decode);
    RUN_TEST(test_prox_and_overcurrent_masked);
    RUN_TEST(test_word10);
    RUN_TEST(test_build_init_bytes);
    RUN_TEST(test_build_init_guards);
    RUN_TEST(test_begin_replays_the_init_sequence);
    RUN_TEST(test_read_touched_decodes_the_reply);
    return UNITY_END();
}
