// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the IEEE C37.118.2 synchrophasor frame codec (services/energy/c37118): the
// CRC-CCITT, the frame builder, the Command frame, and the CRC-validating parser.
// Pure host tests.

#include "services/energy/c37118/c37118.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// CRC-CCITT (0x1021, init 0xFFFF, no reflection, no final mask): the canonical check
// value for the ASCII string "123456789" is 0x29B1.
void test_crc_check_value()
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, pc_c37118_crc((const uint8_t *)"123456789", 9));
}

// A Command frame has the documented header bytes and an 18-octet size.
void test_build_command_bytes()
{
    uint8_t buf[32];
    size_t n = pc_c37118_build_command(buf, sizeof(buf), 7, 0x5F000000, 0x00000000, C37118_CMD_DATA_ON);
    TEST_ASSERT_EQUAL_size_t(18, n);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x42, buf[1]); // type CMD (4<<4) | version 2
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[3]); // framesize 18
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[5]);  // idcode 7
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[14]); // cmd word high byte
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[15]); // cmd word low byte (DATA_ON = 2)
    // The appended CHK (octets 16..17) matches a fresh CRC over the first 16 octets.
    uint16_t chk = (uint16_t)((buf[16] << 8) | buf[17]);
    TEST_ASSERT_EQUAL_HEX16(pc_c37118_crc(buf, 16), chk);
}

void test_command_round_trip()
{
    uint8_t buf[32];
    size_t n = pc_c37118_build_command(buf, sizeof(buf), 0x1234, 0x5F5E1100, 0x00ABCDEF, C37118_CMD_SEND_CFG2);
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    C37118Frame f;
    TEST_ASSERT_TRUE(pc_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(C37118_TYPE_CMD, f.type);
    TEST_ASSERT_EQUAL_UINT8(C37118_VERSION_2011, f.version);
    TEST_ASSERT_EQUAL_UINT16(18, f.framesize);
    TEST_ASSERT_EQUAL_HEX16(0x1234, f.idcode);
    TEST_ASSERT_EQUAL_HEX32(0x5F5E1100, f.soc);
    TEST_ASSERT_EQUAL_HEX32(0x00ABCDEF, f.fracsec);
    TEST_ASSERT_EQUAL_size_t(2, f.data_len);
    uint16_t cmd;
    TEST_ASSERT_TRUE(pc_c37118_parse_command(&f, &cmd));
    TEST_ASSERT_EQUAL_UINT16(C37118_CMD_SEND_CFG2, cmd);
}

// A data frame carries an arbitrary payload (e.g. STAT + one phasor) and round-trips.
void test_data_frame_payload()
{
    const uint8_t payload[] = {0x00, 0x00, 0x12, 0x34, 0x56, 0x78}; // STAT + a 4-byte value
    uint8_t buf[64];
    size_t n = pc_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 60, 100, 200, payload,
                                     sizeof(payload));
    TEST_ASSERT_EQUAL_size_t(C37118_MIN_FRAME + sizeof(payload), n);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]); // type DATA (0) | version 2

    C37118Frame f;
    TEST_ASSERT_TRUE(pc_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(C37118_TYPE_DATA, f.type);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), f.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.data, sizeof(payload));
    uint16_t cmd;
    TEST_ASSERT_FALSE(pc_c37118_parse_command(&f, &cmd)); // not a command frame
}

void test_decode_stat()
{
    // A data frame whose STAT word 0xFB63 exercises a mix of flags:
    //   data invalid, PMU error, not in sync, sorted by arrival, trigger, data modified,
    //   time quality 5, unlocked 100..1000 s, trigger reason = phase-angle difference.
    const uint8_t payload[] = {0xFB, 0x63, 0x12, 0x34}; // STAT + one phasor value stub
    uint8_t buf[64];
    size_t n = pc_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 60, 100, 200, payload,
                                     sizeof(payload));
    C37118Frame f;
    TEST_ASSERT_TRUE(pc_c37118_parse_frame(buf, n, &f));

    C37118Stat st;
    TEST_ASSERT_TRUE(pc_c37118_decode_stat(&f, &st));
    TEST_ASSERT_EQUAL_HEX16(0xFB63, st.raw);
    TEST_ASSERT_FALSE(st.data_valid);
    TEST_ASSERT_TRUE(st.pmu_error);
    TEST_ASSERT_FALSE(st.in_sync);
    TEST_ASSERT_TRUE(st.sorted_by_arrival);
    TEST_ASSERT_TRUE(st.trigger);
    TEST_ASSERT_FALSE(st.config_change);
    TEST_ASSERT_TRUE(st.data_modified);
    TEST_ASSERT_EQUAL_UINT8(5, st.time_quality);
    TEST_ASSERT_EQUAL_UINT8(C37118_UNLOCKED_100_1000S, st.unlocked_time);
    TEST_ASSERT_EQUAL_UINT8(C37118_TRIGGER_PHASE_ANGLE, st.trigger_reason);

    // An all-zero STAT is the nominal healthy state: valid, in sync, no trigger.
    const uint8_t healthy[] = {0x00, 0x00, 0x00, 0x00};
    n = pc_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 60, 1, 2, healthy,
                              sizeof(healthy));
    TEST_ASSERT_TRUE(pc_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_TRUE(pc_c37118_decode_stat(&f, &st));
    TEST_ASSERT_TRUE(st.data_valid);
    TEST_ASSERT_TRUE(st.in_sync);
    TEST_ASSERT_FALSE(st.trigger);
    TEST_ASSERT_EQUAL_UINT8(C37118_TRIGGER_MANUAL, st.trigger_reason);

    // A command frame is not a data frame; a data frame with < 2 payload octets is rejected; nulls too.
    n = pc_c37118_build_command(buf, sizeof(buf), 1, 2, 3, C37118_CMD_DATA_ON);
    C37118Frame cf;
    pc_c37118_parse_frame(buf, n, &cf);
    TEST_ASSERT_FALSE(pc_c37118_decode_stat(&cf, &st));
    TEST_ASSERT_FALSE(pc_c37118_decode_stat(NULL, &st));
    TEST_ASSERT_FALSE(pc_c37118_decode_stat(&f, NULL));
}

void test_parse_rejects_bad()
{
    uint8_t buf[32];
    size_t n = pc_c37118_build_command(buf, sizeof(buf), 7, 1, 2, C37118_CMD_DATA_ON);
    C37118Frame f;

    // A flipped payload bit must fail the CRC check.
    uint8_t corrupt[32];
    memcpy(corrupt, buf, n);
    corrupt[14] ^= 0x01;
    TEST_ASSERT_FALSE(pc_c37118_parse_frame(corrupt, n, &f));

    // Wrong SYNC leader.
    memcpy(corrupt, buf, n);
    corrupt[0] = 0xAB;
    TEST_ASSERT_FALSE(pc_c37118_parse_frame(corrupt, n, &f));

    // Truncated buffer (framesize claims more than is present).
    TEST_ASSERT_FALSE(pc_c37118_parse_frame(buf, n - 1, &f));

    // Too short to be a frame at all.
    TEST_ASSERT_FALSE(pc_c37118_parse_frame(buf, 4, &f));
}

void test_build_overflow_fails_closed()
{
    uint8_t small[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_c37118_build_command(small, sizeof(small), 1, 0, 0, C37118_CMD_DATA_ON));
}

// pc_c37118_build_frame's guard clause: a null destination buffer, a null payload with a
// non-zero payload_len, and (on the success side) a zero-length payload all fail closed / or
// succeed correctly, exercising the full "!buf || (payload_len && !payload)" branch matrix.
void test_build_frame_null_and_zero_payload()
{
    uint8_t buf[32];
    // Null destination buffer.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_c37118_build_frame(NULL, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 1, 2, 3, NULL, 0));

    // Non-zero payload_len but a null payload pointer.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 1, 2, 3, NULL, 5));

    // Zero-length payload (a null payload pointer is fine here): header-only frame.
    size_t n = pc_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_HEADER, C37118_VERSION_2011, 9, 10, 11, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(C37118_MIN_FRAME, n);

    C37118Frame f;
    TEST_ASSERT_TRUE(pc_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(C37118_TYPE_HEADER, f.type);
    TEST_ASSERT_EQUAL_size_t(0, f.data_len);
}

// A payload_len large enough to push the frame's total size past the 16-bit FRAMESIZE field
// must fail closed, independent of the destination capacity.
void test_build_frame_size_field_overflow()
{
    uint8_t buf[8];
    uint8_t dummy_payload[1] = {0}; // never dereferenced: the size check rejects before any copy
    TEST_ASSERT_EQUAL_size_t(0, pc_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 1, 2, 3,
                                                      dummy_payload, 0xFFF0));
}

// pc_c37118_parse_frame's null-argument guards.
void test_parse_frame_null_args()
{
    uint8_t buf[32];
    size_t n = pc_c37118_build_command(buf, sizeof(buf), 7, 1, 2, C37118_CMD_DATA_ON);
    C37118Frame f;

    TEST_ASSERT_FALSE(pc_c37118_parse_frame(NULL, n, &f));
    TEST_ASSERT_FALSE(pc_c37118_parse_frame(buf, n, NULL));
}

// A FRAMESIZE field that claims fewer octets than the minimum legal frame must be rejected,
// even though the caller-supplied buffer length is itself long enough.
void test_parse_frame_framesize_too_small()
{
    uint8_t buf[32];
    size_t n = pc_c37118_build_command(buf, sizeof(buf), 7, 1, 2, C37118_CMD_DATA_ON);
    // Spoof an under-sized FRAMESIZE field (big-endian, at octets 2-3).
    buf[2] = 0x00;
    buf[3] = (uint8_t)(C37118_MIN_FRAME - 1);

    C37118Frame f;
    TEST_ASSERT_FALSE(pc_c37118_parse_frame(buf, n, &f));
}

// pc_c37118_parse_command's remaining guards: a null frame pointer, a too-short DATA (parsed
// from a genuine Command-type frame with only 1 payload octet), and a null cmd out-param on an
// otherwise-valid Command frame (the command word is simply not written back).
void test_parse_command_edge_cases()
{
    uint16_t cmd = 0xDEAD;
    TEST_ASSERT_FALSE(pc_c37118_parse_command(NULL, &cmd));

    uint8_t buf[32];
    uint8_t short_payload[1] = {0x01};
    size_t n = pc_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_CMD, C37118_VERSION_2011, 1, 2, 3, short_payload,
                                     sizeof(short_payload));
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    C37118Frame f;
    TEST_ASSERT_TRUE(pc_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(C37118_TYPE_CMD, f.type);
    TEST_ASSERT_EQUAL_size_t(1, f.data_len);
    TEST_ASSERT_FALSE(pc_c37118_parse_command(&f, &cmd));

    size_t n2 = pc_c37118_build_command(buf, sizeof(buf), 7, 1, 2, C37118_CMD_DATA_ON);
    C37118Frame f2;
    TEST_ASSERT_TRUE(pc_c37118_parse_frame(buf, n2, &f2));
    TEST_ASSERT_TRUE(pc_c37118_parse_command(&f2, NULL)); // valid frame, caller doesn't want cmd
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_value);
    RUN_TEST(test_build_command_bytes);
    RUN_TEST(test_command_round_trip);
    RUN_TEST(test_data_frame_payload);
    RUN_TEST(test_decode_stat);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_build_frame_null_and_zero_payload);
    RUN_TEST(test_build_frame_size_field_overflow);
    RUN_TEST(test_parse_frame_null_args);
    RUN_TEST(test_parse_frame_framesize_too_small);
    RUN_TEST(test_parse_command_edge_cases);
    return UNITY_END();
}
