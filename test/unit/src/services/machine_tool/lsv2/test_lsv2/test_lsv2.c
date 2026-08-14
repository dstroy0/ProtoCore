// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Heidenhain LSV/2 telegram codec (services/machine_tool/lsv2/lsv2.h).
//
// Heidenhain publishes no open LSV/2 wire document, so no standard-published vector is obtainable.
// Expectations come from the framing lsv2.h states (a 4-octet big-endian payload-length prefix, a
// 4-character ASCII mnemonic, then the payload, with the length counting the payload ONLY) and from
// PROPERTIES: build then parse is the identity, a stream re-frames on the consumed count, a
// telegram short of its declared payload is refused, and a full buffer is never half-written.
//
// test_bare_t_ok_is_eight_octets is the load-bearing case. lsv2.h states the empty acknowledgement
// verbatim as `00 00 00 00 'T' '_' 'O' 'K'`, and that single telegram pins all three framing
// decisions at once: the length field excludes the mnemonic, the prefix is big-endian, and the
// mnemonic is four raw ASCII octets with no terminator.

#include "services/machine_tool/lsv2/lsv2.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The mnemonic is four octets and is not NUL-terminated.
static void assert_mnemonic(const Lsv2Telegram *t, const char *want4)
{
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want4, t->mnemonic, PROTOCORE_LSV2_MNEMONIC_LEN, want4);
}

// The documented empty acknowledgement, byte for byte, in both directions.
void test_bare_t_ok_is_eight_octets(void)
{
    static const uint8_t WIRE[] = {0x00, 0x00, 0x00, 0x00, 'T', '_', 'O', 'K'};
    Lsv2Telegram t;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(protocore_lsv2_parse(WIRE, sizeof(WIRE), &t, &consumed));
    assert_mnemonic(&t, PROTOCORE_LSV2_RSP_OK);
    TEST_ASSERT_EQUAL_size_t(0u, t.payload_len);
    TEST_ASSERT_NULL(t.payload);
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_LSV2_HEADER_LEN, consumed);
    TEST_ASSERT_TRUE(protocore_lsv2_is_ok(&t));

    uint8_t buf[16];
    TEST_ASSERT_EQUAL_size_t(sizeof(WIRE), protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_RSP_OK, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIRE, buf, sizeof(WIRE));
}

// The 4-octet prefix is the payload length alone, big-endian, so the telegram is 8 + payload_len.
void test_length_prefix_counts_only_the_payload(void)
{
    static const uint8_t PAYLOAD[] = {0xDE, 0xAD, 0xBE};
    uint8_t buf[32];
    size_t n = protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_CMD_STATUS, PAYLOAD, sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_LSV2_HEADER_LEN + sizeof(PAYLOAD), n);

    static const uint8_t WANT[] = {0x00, 0x00, 0x00, 0x03, 'R', '_', 'S', 'T', 0xDE, 0xAD, 0xBE};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    Lsv2Telegram t;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(protocore_lsv2_parse(buf, n, &t, &consumed));
    assert_mnemonic(&t, PROTOCORE_LSV2_CMD_STATUS);
    TEST_ASSERT_EQUAL_size_t(sizeof(PAYLOAD), t.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, t.payload, sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_size_t(n, consumed);
}

// A_LG carries the privilege group as a NUL-terminated string, so "INSPECT" is 8 payload octets.
void test_login_payload_is_a_nul_terminated_group(void)
{
    uint8_t buf[32];
    size_t n = protocore_lsv2_build_login(buf, sizeof(buf), PROTOCORE_LSV2_LOGIN_INSPECT, NULL);
    static const uint8_t WANT[] = {0x00, 0x00, 0x00, 0x08, 'A', '_', 'L', 'G', 'I', 'N', 'S', 'P', 'E', 'C', 'T', 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, n);
}

// With a password the payload is two NUL-terminated strings back to back.
void test_login_appends_the_password_as_a_second_string(void)
{
    uint8_t buf[64];
    size_t n = protocore_lsv2_build_login(buf, sizeof(buf), PROTOCORE_LSV2_LOGIN_PLCDEBUG, "807667");
    // payload = 9 ("PLCDEBUG" + NUL) + 7 ("807667" + NUL) = 16 = 0x10
    static const uint8_t WANT[] = {0x00, 0x00, 0x00, 0x10, 'A',  '_', 'L', 'G', 'P', 'L', 'C', 'D',
                                   'E',  'B',  'U',  'G',  0x00, '8', '0', '7', '6', '6', '7', 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, n);
}

// A_LO with no group is an empty payload: log out of everything.
void test_logout_with_and_without_a_group(void)
{
    uint8_t buf[32];
    static const uint8_t BARE[] = {0x00, 0x00, 0x00, 0x00, 'A', '_', 'L', 'O'};
    TEST_ASSERT_EQUAL_size_t(sizeof(BARE), protocore_lsv2_build_logout(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BARE, buf, sizeof(BARE));

    // an empty string is the same as none
    TEST_ASSERT_EQUAL_size_t(sizeof(BARE), protocore_lsv2_build_logout(buf, sizeof(buf), ""));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BARE, buf, sizeof(BARE));

    static const uint8_t WITH[] = {0x00, 0x00, 0x00, 0x05, 'A', '_', 'L', 'O', 'F', 'I', 'L', 'E', 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WITH), protocore_lsv2_build_logout(buf, sizeof(buf), PROTOCORE_LSV2_LOGIN_FILE));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WITH, buf, sizeof(WITH));
}

// A file command is its mnemonic plus the NUL-terminated name.
void test_filename_command_frames_the_name(void)
{
    uint8_t buf[64];
    size_t n = protocore_lsv2_build_filename(buf, sizeof(buf), PROTOCORE_LSV2_CMD_FILE_LOAD, "TNC:\\test.H");
    static const uint8_t WANT[] = {0x00, 0x00, 0x00, 0x0C, 'R', '_', 'F', 'L', 'T', 'N',
                                   'C',  ':',  '\\', 't',  'e', 's', 't', '.', 'H', 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, n);
}

// R_RI takes its selector as a 2-octet big-endian argument: 23 is 0x00 0x17, 26 is 0x00 0x1A.
void test_run_info_selector_is_big_endian(void)
{
    uint8_t buf[32];
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_LSV2_HEADER_LEN + 2,
                             protocore_lsv2_build_run_info(buf, sizeof(buf), LSV2_RI_EXEC_STATE));
    static const uint8_t WANT23[] = {0x00, 0x00, 0x00, 0x02, 'R', '_', 'R', 'I', 0x00, 0x17};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT23, buf, sizeof(WANT23));

    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_LSV2_HEADER_LEN + 2,
                             protocore_lsv2_build_run_info(buf, sizeof(buf), LSV2_RI_PGM_STATE));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x1A, buf[9]);

    // a selector above 255 puts its high octet first
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_LSV2_HEADER_LEN + 2,
                             protocore_lsv2_build_run_info(buf, sizeof(buf), 0x1234));
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[9]);
}

// Two telegrams arriving in one read: the consumed count is what lets the caller reach the second.
void test_stream_reframes_on_the_consumed_count(void)
{
    uint8_t wire[64];
    size_t first = protocore_lsv2_build_run_info(wire, sizeof(wire), LSV2_RI_OVERRIDE);
    size_t second = protocore_lsv2_build(wire + first, sizeof(wire) - first, PROTOCORE_LSV2_RSP_OK, NULL, 0);
    size_t total = first + second;

    Lsv2Telegram t;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(protocore_lsv2_parse(wire, total, &t, &consumed));
    assert_mnemonic(&t, PROTOCORE_LSV2_CMD_RUN_INFO);
    TEST_ASSERT_EQUAL_size_t(first, consumed);

    size_t consumed2 = 0;
    TEST_ASSERT_TRUE(protocore_lsv2_parse(wire + consumed, total - consumed, &t, &consumed2));
    assert_mnemonic(&t, PROTOCORE_LSV2_RSP_OK);
    TEST_ASSERT_EQUAL_size_t(second, consumed2);
    TEST_ASSERT_EQUAL_size_t(total, consumed + consumed2);
}

// Fewer octets than the header, or fewer than the declared payload, is "not yet" rather than a
// telegram with a short payload.
void test_incomplete_telegram_is_refused(void)
{
    uint8_t buf[32];
    size_t n = protocore_lsv2_build_run_info(buf, sizeof(buf), LSV2_RI_SELECTED_PGM);
    Lsv2Telegram t;
    size_t consumed = 12345;

    TEST_ASSERT_FALSE(protocore_lsv2_parse(buf, n - 1, &t, &consumed));
    TEST_ASSERT_FALSE(protocore_lsv2_parse(buf, (size_t)PROTOCORE_LSV2_HEADER_LEN - 1, &t, &consumed));
    TEST_ASSERT_FALSE(protocore_lsv2_parse(NULL, n, &t, &consumed));
    TEST_ASSERT_EQUAL_size_t(12345u, consumed); // untouched on refusal
    TEST_ASSERT_TRUE(protocore_lsv2_parse(buf, n, &t, &consumed));
    TEST_ASSERT_EQUAL_size_t(n, consumed);
}

// T_ER and T_BD carry an error class then an error code, exactly two octets.
void test_error_payload_is_class_then_code(void)
{
    uint8_t buf[32];
    static const uint8_t ERR[] = {0x03, 0x0B};
    Lsv2Telegram t;
    uint8_t cls = 0;
    uint8_t code = 0;

    size_t n = protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_RSP_ERROR, ERR, sizeof(ERR));
    TEST_ASSERT_TRUE(protocore_lsv2_parse(buf, n, &t, NULL));
    TEST_ASSERT_TRUE(protocore_lsv2_is_error(&t));
    TEST_ASSERT_FALSE(protocore_lsv2_is_ok(&t));
    TEST_ASSERT_TRUE(protocore_lsv2_error(&t, &cls, &code));
    TEST_ASSERT_EQUAL_HEX8(0x03, cls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, code);

    // the transfer error is the same shape
    n = protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_RSP_XFER_ERR, ERR, sizeof(ERR));
    TEST_ASSERT_TRUE(protocore_lsv2_parse(buf, n, &t, NULL));
    TEST_ASSERT_TRUE(protocore_lsv2_is_error(&t));
    TEST_ASSERT_TRUE(protocore_lsv2_error(&t, &cls, &code));

    // a payload of any other length is not a decodable error
    n = protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_RSP_ERROR, ERR, 1);
    TEST_ASSERT_TRUE(protocore_lsv2_parse(buf, n, &t, NULL));
    TEST_ASSERT_TRUE(protocore_lsv2_is_error(&t));
    TEST_ASSERT_FALSE(protocore_lsv2_error(&t, &cls, &code));
}

// T_OK is not an error and a data reply is neither.
void test_response_mnemonics_are_discriminated(void)
{
    uint8_t buf[32];
    Lsv2Telegram t;

    TEST_ASSERT_TRUE(
        protocore_lsv2_parse(buf, protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_RSP_OK, NULL, 0), &t, NULL));
    TEST_ASSERT_TRUE(protocore_lsv2_is_ok(&t));
    TEST_ASSERT_FALSE(protocore_lsv2_is_error(&t));
    TEST_ASSERT_TRUE(protocore_lsv2_is(&t, PROTOCORE_LSV2_RSP_OK));
    TEST_ASSERT_FALSE(protocore_lsv2_is(&t, PROTOCORE_LSV2_RSP_FIN));

    static const uint8_t DATA[] = {0x01};
    TEST_ASSERT_TRUE(protocore_lsv2_parse(
        buf, protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_RSP_RUN_INFO, DATA, sizeof(DATA)), &t, NULL));
    TEST_ASSERT_FALSE(protocore_lsv2_is_ok(&t));
    TEST_ASSERT_FALSE(protocore_lsv2_is_error(&t));
    TEST_ASSERT_TRUE(protocore_lsv2_is(&t, PROTOCORE_LSV2_RSP_RUN_INFO));
}

// A buffer that cannot hold header plus payload reports 0 and is never partly framed.
void test_builders_refuse_a_short_buffer(void)
{
    uint8_t buf[32];
    static const uint8_t PAYLOAD[] = {1, 2, 3, 4};
    memset(buf, 0x5A, sizeof(buf));

    TEST_ASSERT_EQUAL_size_t(0u,
                             protocore_lsv2_build(buf, PROTOCORE_LSV2_HEADER_LEN - 1, PROTOCORE_LSV2_RSP_OK, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lsv2_build(buf, PROTOCORE_LSV2_HEADER_LEN + 3, PROTOCORE_LSV2_CMD_STATUS,
                                                      PAYLOAD, sizeof(PAYLOAD)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lsv2_build(NULL, sizeof(buf), PROTOCORE_LSV2_RSP_OK, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lsv2_build(buf, sizeof(buf), PROTOCORE_LSV2_RSP_OK, NULL, 1)); // no payload
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lsv2_build_run_info(buf, PROTOCORE_LSV2_HEADER_LEN + 1, LSV2_RI_OVERRIDE));
    // "INSPECT" needs 8 payload octets; 7 available past the header
    TEST_ASSERT_EQUAL_size_t(
        0u, protocore_lsv2_build_login(buf, PROTOCORE_LSV2_HEADER_LEN + 7, PROTOCORE_LSV2_LOGIN_INSPECT, NULL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lsv2_build_filename(buf, sizeof(buf), PROTOCORE_LSV2_CMD_FILE_LOAD, NULL));
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[0]); // header never written on refusal
}
