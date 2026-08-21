// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Sigfox modem AT-command codec (services/radio/sigfox/sigfox.h).
//
// Sigfox's device documentation and the Wisol modem AT command set are vendor specifications, not
// IETF documents. Two things they publish anchor this suite: the uplink payload is at most 12
// octets, and the "getting started" walkthrough prints the command
// AT$SF=496F54456173746572456767 for the twelve-octet payload "IoTEasterEgg".
//
// test_sigfox_published_uplink_example is the load-bearing case. It is that published command, and
// its hex is uppercase, most significant nibble first, two characters per octet, with no separator
// - a modem rejects any other rendering, and the payload silently becomes a different one if the
// nibble order is swapped.

#include "services/radio/sigfox/sigfox.h"
#include <string.h>

#include <unity.h>

static uint8_t sigfox_work[16]; // the borrow an entry takes; Sigfox never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// "IoTEasterEgg" is 12 octets: I=0x49 o=0x6F T=0x54 E=0x45 a=0x61 s=0x73 t=0x74 e=0x65 r=0x72
// E=0x45 g=0x67 g=0x67, which is the published 496F54456173746572456767.
void test_sigfox_published_uplink_example(void)
{
    static const uint8_t PAYLOAD[12] = {'I', 'o', 'T', 'E', 'a', 's', 't', 'e', 'r', 'E', 'g', 'g'};
    char out[64];
    uint16_t sigfox_value = Sigfox.build_uplink(sigfox_work, PAYLOAD, sizeof(PAYLOAD), out, sizeof(out));
    const uint16_t n = sigfox_value;
    TEST_ASSERT_EQUAL_STRING("AT$SF=496F54456173746572456767\r\n", out);
    // "AT$SF=" is 6, the hex is 2 per octet, and the command ends CR LF; the NUL is past the count.
    TEST_ASSERT_EQUAL_UINT16(6 + 24 + 2, n);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)strlen(out), n);
    TEST_ASSERT_EQUAL_CHAR('\0', out[n]);
}

// Two characters per octet, most significant nibble first, digits 0-9 then uppercase A-F.
void test_hex_is_uppercase_and_msb_nibble_first(void)
{
    char out[64];

    // 0x0A and 0xB3 separate the two nibbles and cover both halves of the digit alphabet.
    static const uint8_t NIBBLES[2] = {0x0A, 0xB3};
    uint16_t sigfox_value = Sigfox.build_uplink(sigfox_work, NIBBLES, sizeof(NIBBLES), out, sizeof(out));
    (void)sigfox_value;
    TEST_ASSERT_EQUAL_STRING("AT$SF=0AB3\r\n", out);

    // Every hex digit, in order, from six octets.
    static const uint8_t ALL[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    sigfox_value = Sigfox.build_uplink(sigfox_work, ALL, sizeof(ALL), out, sizeof(out));
    (void)sigfox_value;
    TEST_ASSERT_EQUAL_STRING("AT$SF=0123456789ABCDEF\r\n", out);

    // The extremes of one octet.
    static const uint8_t EDGES[2] = {0x00, 0xFF};
    sigfox_value = Sigfox.build_uplink(sigfox_work, EDGES, sizeof(EDGES), out, sizeof(out));
    (void)sigfox_value;
    TEST_ASSERT_EQUAL_STRING("AT$SF=00FF\r\n", out);
}

// Sigfox caps an uplink message at 12 octets, so 12 builds and 13 has no command at all.
void test_payload_cap_is_twelve_octets(void)
{
    static const uint8_t PAYLOAD[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    char out[64];

    TEST_ASSERT_EQUAL_INT(12, PROTOCORE_SIGFOX_MAX_PAYLOAD);
    uint16_t sigfox_value = Sigfox.build_uplink(sigfox_work, PAYLOAD, 12, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(6 + 24 + 2, sigfox_value);
    sigfox_value = Sigfox.build_uplink(sigfox_work, PAYLOAD, 13, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(0, sigfox_value);

    // A zero-length uplink is not a message either: AT$SF= carries no payload to send.
    sigfox_value = Sigfox.build_uplink(sigfox_work, PAYLOAD, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(0, sigfox_value);
}

// The command is written whole or not at all, and the room it needs is the prefix, two characters
// per octet, CR LF, and the terminator.
void test_build_fails_closed(void)
{
    static const uint8_t PAYLOAD[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    char out[32];
    const uint16_t need = 6 + 2 * 4 + 2 + 1; // 17 octets including the NUL

    uint16_t sigfox_value = Sigfox.build_uplink(sigfox_work, PAYLOAD, 4, out, need);
    TEST_ASSERT_EQUAL_UINT16(need - 1, sigfox_value);
    TEST_ASSERT_EQUAL_STRING("AT$SF=DEADBEEF\r\n", out);
    sigfox_value = Sigfox.build_uplink(sigfox_work, PAYLOAD, 4, out, (uint16_t)(need - 1));
    TEST_ASSERT_EQUAL_UINT16(0, sigfox_value);
    sigfox_value = Sigfox.build_uplink(sigfox_work, PAYLOAD, 4, NULL, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(0, sigfox_value);
    sigfox_value = Sigfox.build_uplink(sigfox_work, NULL, 4, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(0, sigfox_value);
}

// The modem answers "OK" when it took the command and "ERROR" when it did not; anything else is
// not yet an answer, so the caller keeps reading.
void test_response_classification(void)
{
    protocore_sigfox_result sigfox_status = Sigfox.parse_response(sigfox_work, "OK\r\n", 4);
    TEST_ASSERT_EQUAL_INT(SIGFOX_OK, sigfox_status);
    sigfox_status = Sigfox.parse_response(sigfox_work, "\r\nOK\r\n", 6);
    TEST_ASSERT_EQUAL_INT(SIGFOX_OK, sigfox_status);
    sigfox_status = Sigfox.parse_response(sigfox_work, "ERROR\r\n", 7);
    TEST_ASSERT_EQUAL_INT(SIGFOX_ERROR, sigfox_status);
    sigfox_status = Sigfox.parse_response(sigfox_work, "\r\nERROR: 5\r\n", 12);
    TEST_ASSERT_EQUAL_INT(SIGFOX_ERROR, sigfox_status);

    // The command echo the modem sends back first is not an answer.
    sigfox_status = Sigfox.parse_response(sigfox_work, "AT$SF=DEADBEEF\r\n", 16);
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, sigfox_status);
    sigfox_status = Sigfox.parse_response(sigfox_work, "O", 1);
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, sigfox_status); // half of "OK"

    // An error answer wins over an "OK" elsewhere in the same buffer: a failed uplink must not be
    // reported as sent.
    sigfox_status = Sigfox.parse_response(sigfox_work, "OK\r\nERROR\r\n", 11);
    TEST_ASSERT_EQUAL_INT(SIGFOX_ERROR, sigfox_status);
}

// The classification reads only the octets it is given: a match that starts inside the buffer but
// runs past the stated length is not a match yet.
void test_response_respects_the_stated_length(void)
{
    static const char *const BUF = "ERROR";
    for (uint16_t len = 0; len < 5; len++)
    {
        protocore_sigfox_result sigfox_status = Sigfox.parse_response(sigfox_work, BUF, len);
        TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, sigfox_status);
    }
    protocore_sigfox_result sigfox_status = Sigfox.parse_response(sigfox_work, BUF, 5);
    TEST_ASSERT_EQUAL_INT(SIGFOX_ERROR, sigfox_status);

    static const char *const OKBUF = "OK";
    sigfox_status = Sigfox.parse_response(sigfox_work, OKBUF, 1);
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, sigfox_status);
    sigfox_status = Sigfox.parse_response(sigfox_work, OKBUF, 2);
    TEST_ASSERT_EQUAL_INT(SIGFOX_OK, sigfox_status);

    sigfox_status = Sigfox.parse_response(sigfox_work, NULL, 10);
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, sigfox_status);
    sigfox_status = Sigfox.parse_response(sigfox_work, "OK", 0);
    TEST_ASSERT_EQUAL_INT(SIGFOX_PENDING, sigfox_status);
}
