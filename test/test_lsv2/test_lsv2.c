// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Heidenhain LSV/2 telegram codec (services/machine_tool/lsv2): the framer (4-byte big-endian
// payload-length prefix + 4-char mnemonic + payload), the typed request builders (login / logout,
// filename commands, run-info), and the response readers (T_OK / T_ER error-class+code, data replies,
// stream re-framing). Byte-exact golden vectors per the LSV/2-over-TCP framing (length counts the
// payload only, so a bare telegram is exactly 8 bytes), cross-checked against the pyLSV2 reference.

#include "services/machine_tool/lsv2/lsv2.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// ── framer ───────────────────────────────────────────────────────────────────────────────────

void test_build_no_payload()
{
    uint8_t buf[16];
    // R_ST with no payload -> exactly 8 bytes: 00 00 00 00 'R' '_' 'S' 'T'
    size_t n = pc_lsv2_build(buf, sizeof(buf), PC_LSV2_CMD_STATUS, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(8, n);
    const uint8_t want[] = {0x00, 0x00, 0x00, 0x00, 'R', '_', 'S', 'T'};
    TEST_ASSERT_EQUAL_MEMORY(want, buf, 8);
}

void test_build_with_payload()
{
    uint8_t buf[32];
    const uint8_t pay[] = {0xDE, 0xAD, 0xBE, 0xEF};
    size_t n = pc_lsv2_build(buf, sizeof(buf), PC_LSV2_CMD_PARAM, pay, sizeof(pay));
    TEST_ASSERT_EQUAL_size_t(12, n);
    // length prefix counts payload only (4)
    const uint8_t want[] = {0x00, 0x00, 0x00, 0x04, 'R', '_', 'P', 'R', 0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_MEMORY(want, buf, 12);
    // overflow fails closed
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build(buf, 8, PC_LSV2_CMD_PARAM, pay, sizeof(pay)));
}

void test_build_run_info()
{
    uint8_t buf[16];
    size_t n = pc_lsv2_build_run_info(buf, sizeof(buf), LSV2_RI_PGM_STATE); // 26 -> 0x001A
    TEST_ASSERT_EQUAL_size_t(10, n);
    const uint8_t want[] = {0x00, 0x00, 0x00, 0x02, 'R', '_', 'R', 'I', 0x00, 0x1A};
    TEST_ASSERT_EQUAL_MEMORY(want, buf, 10);
    // tiny buffer fails closed
    uint8_t tiny[9];
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_run_info(tiny, sizeof(tiny), LSV2_RI_EXEC_STATE));
}

void test_build_login()
{
    uint8_t buf[64];
    // login "INSPECT", no password -> payload "INSPECT\0" (8 bytes)
    size_t n = pc_lsv2_build_login(buf, sizeof(buf), PC_LSV2_LOGIN_INSPECT, NULL);
    TEST_ASSERT_EQUAL_size_t(16, n);
    const uint8_t want[] = {0x00, 0x00, 0x00, 0x08, 'A', '_', 'L', 'G', 'I', 'N', 'S', 'P', 'E', 'C', 'T', 0x00};
    TEST_ASSERT_EQUAL_MEMORY(want, buf, 16);

    // login "DNC" with password "pw" -> payload "DNC\0pw\0" (7 bytes)
    n = pc_lsv2_build_login(buf, sizeof(buf), PC_LSV2_LOGIN_DNC, "pw");
    TEST_ASSERT_EQUAL_size_t(15, n);
    const uint8_t want2[] = {0x00, 0x00, 0x00, 0x07, 'A', '_', 'L', 'G', 'D', 'N', 'C', 0x00, 'p', 'w', 0x00};
    TEST_ASSERT_EQUAL_MEMORY(want2, buf, 15);
}

void test_build_logout()
{
    uint8_t buf[32];
    // no login -> log out of everything -> empty payload, 8 bytes
    size_t n = pc_lsv2_build_logout(buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_size_t(8, n);
    const uint8_t want[] = {0x00, 0x00, 0x00, 0x00, 'A', '_', 'L', 'O'};
    TEST_ASSERT_EQUAL_MEMORY(want, buf, 8);
    // empty string is treated the same as no login
    TEST_ASSERT_EQUAL_size_t(8, pc_lsv2_build_logout(buf, sizeof(buf), ""));

    // named group -> payload "DNC\0"
    n = pc_lsv2_build_logout(buf, sizeof(buf), PC_LSV2_LOGIN_DNC);
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t want2[] = {0x00, 0x00, 0x00, 0x04, 'A', '_', 'L', 'O', 'D', 'N', 'C', 0x00};
    TEST_ASSERT_EQUAL_MEMORY(want2, buf, 12);
}

void test_build_filename()
{
    uint8_t buf[32];
    // R_FL "PGM.H" -> payload "PGM.H\0" (6 bytes)
    size_t n = pc_lsv2_build_filename(buf, sizeof(buf), PC_LSV2_CMD_FILE_LOAD, "PGM.H");
    TEST_ASSERT_EQUAL_size_t(14, n);
    const uint8_t want[] = {0x00, 0x00, 0x00, 0x06, 'R', '_', 'F', 'L', 'P', 'G', 'M', '.', 'H', 0x00};
    TEST_ASSERT_EQUAL_MEMORY(want, buf, 14);
}

// ── parser ───────────────────────────────────────────────────────────────────────────────────

void test_parse_ok()
{
    const uint8_t frame[] = {0x00, 0x00, 0x00, 0x00, 'T', '_', 'O', 'K'};
    Lsv2Telegram t;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(pc_lsv2_parse(frame, sizeof(frame), &t, &consumed));
    TEST_ASSERT_EQUAL_MEMORY("T_OK", t.mnemonic, 4);
    TEST_ASSERT_EQUAL_size_t(0, t.payload_len);
    TEST_ASSERT_NULL(t.payload);
    TEST_ASSERT_EQUAL_size_t(8, consumed);
    TEST_ASSERT_TRUE(pc_lsv2_is_ok(&t));
    TEST_ASSERT_FALSE(pc_lsv2_is_error(&t));
    TEST_ASSERT_TRUE(pc_lsv2_is(&t, PC_LSV2_RSP_OK));
}

void test_parse_error()
{
    // T_ER with a 2-byte error-class + error-code payload
    const uint8_t frame[] = {0x00, 0x00, 0x00, 0x02, 'T', '_', 'E', 'R', 0x01, 0x31};
    Lsv2Telegram t;
    TEST_ASSERT_TRUE(pc_lsv2_parse(frame, sizeof(frame), &t, NULL));
    TEST_ASSERT_TRUE(pc_lsv2_is_error(&t));
    TEST_ASSERT_FALSE(pc_lsv2_is_ok(&t));
    uint8_t ec = 0, code = 0;
    TEST_ASSERT_TRUE(pc_lsv2_error(&t, &ec, &code));
    TEST_ASSERT_EQUAL_UINT8(0x01, ec);
    TEST_ASSERT_EQUAL_UINT8(0x31, code);

    // T_BD (transfer error) is also an error
    const uint8_t xfer[] = {0x00, 0x00, 0x00, 0x02, 'T', '_', 'B', 'D', 0x02, 0x69};
    TEST_ASSERT_TRUE(pc_lsv2_parse(xfer, sizeof(xfer), &t, NULL));
    TEST_ASSERT_TRUE(pc_lsv2_is_error(&t));
    // a T_OK is not an error and has no 2-byte error payload
    const uint8_t ok[] = {0x00, 0x00, 0x00, 0x00, 'T', '_', 'O', 'K'};
    TEST_ASSERT_TRUE(pc_lsv2_parse(ok, sizeof(ok), &t, NULL));
    TEST_ASSERT_FALSE(pc_lsv2_error(&t, &ec, &code));
}

void test_parse_data_reply()
{
    // S_RI run-info reply carrying 3 payload bytes
    const uint8_t frame[] = {0x00, 0x00, 0x00, 0x03, 'S', '_', 'R', 'I', 0xAA, 0xBB, 0xCC};
    Lsv2Telegram t;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(pc_lsv2_parse(frame, sizeof(frame), &t, &consumed));
    TEST_ASSERT_TRUE(pc_lsv2_is(&t, PC_LSV2_RSP_RUN_INFO));
    TEST_ASSERT_EQUAL_size_t(3, t.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("\xAA\xBB\xCC", t.payload, 3);
    TEST_ASSERT_EQUAL_size_t(11, consumed);
}

void test_parse_incomplete()
{
    Lsv2Telegram t;
    // fewer than 8 header bytes -> false, and out is cleared
    const uint8_t partial[] = {0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_lsv2_parse(partial, sizeof(partial), &t, NULL));
    TEST_ASSERT_EQUAL_size_t(0, t.payload_len);
    TEST_ASSERT_NULL(t.payload);
    // header declares 4 payload bytes but only 2 are present -> incomplete
    const uint8_t truncated[] = {0x00, 0x00, 0x00, 0x04, 'S', '_', 'F', 'L', 0x11, 0x22};
    TEST_ASSERT_FALSE(pc_lsv2_parse(truncated, sizeof(truncated), &t, NULL));
}

void test_parse_stream_multi()
{
    // two telegrams back-to-back: T_OK then S_RI(2 bytes)
    const uint8_t stream[] = {0x00, 0x00, 0x00, 0x00, 'T', '_', 'O', 'K',  0x00,
                              0x00, 0x00, 0x02, 'S',  '_', 'R', 'I', 0x07, 0x08};
    Lsv2Telegram t;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(pc_lsv2_parse(stream, sizeof(stream), &t, &consumed));
    TEST_ASSERT_TRUE(pc_lsv2_is_ok(&t));
    TEST_ASSERT_EQUAL_size_t(8, consumed);

    size_t off = consumed;
    TEST_ASSERT_TRUE(pc_lsv2_parse(stream + off, sizeof(stream) - off, &t, &consumed));
    TEST_ASSERT_TRUE(pc_lsv2_is(&t, PC_LSV2_RSP_RUN_INFO));
    TEST_ASSERT_EQUAL_size_t(2, t.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("\x07\x08", t.payload, 2);
    TEST_ASSERT_EQUAL_size_t(10, consumed);
    TEST_ASSERT_EQUAL_size_t(sizeof(stream), off + consumed);
}

void test_roundtrip()
{
    // build then parse: run-info request survives a frame/parse round trip
    uint8_t buf[16];
    size_t n = pc_lsv2_build_run_info(buf, sizeof(buf), LSV2_RI_OVERRIDE);
    Lsv2Telegram t;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(pc_lsv2_parse(buf, n, &t, &consumed));
    TEST_ASSERT_TRUE(pc_lsv2_is(&t, PC_LSV2_CMD_RUN_INFO));
    TEST_ASSERT_EQUAL_size_t(2, t.payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x00, t.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(25, t.payload[1]); // LSV2_RI_OVERRIDE
    TEST_ASSERT_EQUAL_size_t(n, consumed);
}

// ── guards ───────────────────────────────────────────────────────────────────────────────────

void test_build_rejects_bad_args()
{
    // Null destination / null mnemonic / a buffer that cannot even hold the header, a declared
    // payload with a null pointer, and a length that will not fit the 32-bit length field.
    uint8_t buf[32];
    const uint8_t pay[] = {0x01, 0x02};
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build(NULL, sizeof(buf), PC_LSV2_CMD_STATUS, pay, sizeof(pay)));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build(buf, sizeof(buf), NULL, pay, sizeof(pay)));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build(buf, 7, PC_LSV2_CMD_STATUS, pay, sizeof(pay)));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build(buf, sizeof(buf), PC_LSV2_CMD_STATUS, NULL, 2));
#if SIZE_MAX > 0xFFFFFFFFu
    // no allocation is touched - the length field check fires before any copy
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build(buf, sizeof(buf), PC_LSV2_CMD_STATUS, pay, (size_t)0x100000000ULL));
#endif
    TEST_ASSERT_EQUAL_size_t(10, pc_lsv2_build(buf, sizeof(buf), PC_LSV2_CMD_STATUS, pay, sizeof(pay)));
}

void test_build_login_guards_and_overflow()
{
    // Null buffer / null login / a header-only buffer are refused, and a login (or password) that
    // does not fit with its terminating NUL fails the whole build.
    uint8_t buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_login(NULL, sizeof(buf), PC_LSV2_LOGIN_DNC, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_login(buf, sizeof(buf), NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_login(buf, 7, PC_LSV2_LOGIN_DNC, NULL));
    // "INSPECT" needs 8 payload bytes; cap 12 leaves only 4
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_login(buf, 12, PC_LSV2_LOGIN_INSPECT, NULL));
    // login fits exactly, leaving no room for the terminating NUL of the password
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_login(buf, 12, "DNC", "pw"));
    // cap 12 fits "DNC\0" exactly - the boundary the two cases above straddle
    TEST_ASSERT_EQUAL_size_t(12, pc_lsv2_build_login(buf, 12, "DNC", NULL));
}

void test_build_logout_and_filename_guards()
{
    uint8_t buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_logout(NULL, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_logout(buf, 7, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_logout(buf, 10, PC_LSV2_LOGIN_DNC)); // "DNC\0" needs 4
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_filename(NULL, sizeof(buf), PC_LSV2_CMD_FILE_LOAD, "A.H"));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_filename(buf, sizeof(buf), NULL, "A.H"));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_filename(buf, sizeof(buf), PC_LSV2_CMD_FILE_LOAD, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_filename(buf, 7, PC_LSV2_CMD_FILE_LOAD, "A.H"));
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_filename(buf, 11, PC_LSV2_CMD_FILE_LOAD, "A.H")); // needs 12
    TEST_ASSERT_EQUAL_size_t(0, pc_lsv2_build_run_info(NULL, sizeof(buf), LSV2_RI_OVERRIDE));
}

void test_parse_and_is_reject_null_args()
{
    // A null out struct, a null input buffer, and null arguments to the mnemonic comparison are
    // all refused rather than dereferenced.
    const uint8_t frame[] = {0x00, 0x00, 0x00, 0x00, 'T', '_', 'O', 'K'};
    Lsv2Telegram t;
    TEST_ASSERT_FALSE(pc_lsv2_parse(frame, sizeof(frame), NULL, NULL));
    TEST_ASSERT_FALSE(pc_lsv2_parse(NULL, sizeof(frame), &t, NULL));
    TEST_ASSERT_TRUE(pc_lsv2_parse(frame, sizeof(frame), &t, NULL));
    TEST_ASSERT_FALSE(pc_lsv2_is(NULL, PC_LSV2_RSP_OK));
    TEST_ASSERT_FALSE(pc_lsv2_is(&t, NULL));
}

void test_error_payload_shape_is_enforced()
{
    // pc_lsv2_error only reports on an error telegram carrying exactly the 2-byte class+code
    // pair, and both out pointers are optional.
    const uint8_t wrong_len[] = {0x00, 0x00, 0x00, 0x03, 'T', '_', 'E', 'R', 0x01, 0x02, 0x03};
    Lsv2Telegram t;
    TEST_ASSERT_TRUE(pc_lsv2_parse(wrong_len, sizeof(wrong_len), &t, NULL));
    TEST_ASSERT_TRUE(pc_lsv2_is_error(&t));
    TEST_ASSERT_FALSE(pc_lsv2_error(&t, NULL, NULL)); // 3 bytes is not class+code

    // a hand-built telegram claiming 2 payload bytes but carrying no payload slice
    Lsv2Telegram bogus;
    memcpy(bogus.mnemonic, PC_LSV2_RSP_ERROR, PC_LSV2_MNEMONIC_LEN);
    bogus.payload = NULL;
    bogus.payload_len = 2;
    TEST_ASSERT_FALSE(pc_lsv2_error(&bogus, NULL, NULL));

    // both out pointers are optional on the good path
    const uint8_t good[] = {0x00, 0x00, 0x00, 0x02, 'T', '_', 'E', 'R', 0x05, 0x06};
    TEST_ASSERT_TRUE(pc_lsv2_parse(good, sizeof(good), &t, NULL));
    TEST_ASSERT_TRUE(pc_lsv2_error(&t, NULL, NULL));
    uint8_t cls = 0;
    TEST_ASSERT_TRUE(pc_lsv2_error(&t, &cls, NULL));
    TEST_ASSERT_EQUAL_UINT8(0x05, cls);
    uint8_t code = 0;
    TEST_ASSERT_TRUE(pc_lsv2_error(&t, NULL, &code));
    TEST_ASSERT_EQUAL_UINT8(0x06, code);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_no_payload);
    RUN_TEST(test_build_with_payload);
    RUN_TEST(test_build_run_info);
    RUN_TEST(test_build_login);
    RUN_TEST(test_build_logout);
    RUN_TEST(test_build_filename);
    RUN_TEST(test_parse_ok);
    RUN_TEST(test_parse_error);
    RUN_TEST(test_parse_data_reply);
    RUN_TEST(test_parse_incomplete);
    RUN_TEST(test_parse_stream_multi);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_build_rejects_bad_args);
    RUN_TEST(test_build_login_guards_and_overflow);
    RUN_TEST(test_build_logout_and_filename_guards);
    RUN_TEST(test_parse_and_is_reject_null_args);
    RUN_TEST(test_error_payload_shape_is_enforced);
    return UNITY_END();
}
