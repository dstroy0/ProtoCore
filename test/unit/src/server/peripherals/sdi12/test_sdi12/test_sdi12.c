// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SDI-12 sensor-bus codec (server/peripherals/sdi12/sdi12.h).
//
// Expected values come from the SDI-12 specification: Table 5 for the command / response set,
// Table 7 for the aI! identification field widths, Tables 9 / 10 for the aM! atttn and aC! atttnn
// timing responses, Table 11 for the data-value form, and section 4.4.12 for the CRC-16 and its
// three-character ASCII encoding.
//
// test_spec_crc_vectors is the load-bearing case. Section 4.4.12.3 prints five complete
// data-plus-CRC responses, and section 4.4.8.1 a sixth, so the CRC is checked against octets the
// standard itself publishes rather than against this implementation's own output. The catalogued
// CRC-16/ARC check value for "123456789" is asserted beside them as an independent second anchor.

#include "server/peripherals/sdi12/sdi12.h"
#include <string.h>

#include <unity.h>

static uint8_t sdi12_work[16]; // the borrow an entry takes; Sdi12 never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// SDI-12 spec section 4.4.12.3 and 4.4.8.1: each data response and the three CRC octets the sensor
// appends to it, printed in the standard.
void test_spec_crc_vectors(void)
{
    static const char *const CASES[6] = {
        "0+3.14OqZ",                          // 4.4.12.3 a
        "0+3.14+2.718+1.414Ipz",              // 4.4.12.3 b
        "0+1.11+2.22+3.33+4.44+5.55+6.66I]q", // 4.4.12.3 c, first group
        "0+7.77+8.88+9.99IvW",                // 4.4.12.3 c, second group
        "0+3.14+2.718IWO",                    // 4.4.12.3 d
        "0AP@",                               // 4.4.8.1: address only, no data
    };
    for (size_t i = 0; i < 6; i++)
    {
        const size_t n = strlen(CASES[i]);
        Sdi12V.check_crc_args.resp = CASES[i];
        Sdi12V.check_crc_args.len = n;
        Sdi12.check_crc(sdi12_work);
        TEST_ASSERT_TRUE_MESSAGE(Sdi12V.ok, CASES[i]);

        // and the encoder reproduces those same three octets from the data before them
        char enc[SDI12_CRC_CHARS];
        // The checksum is captured before the encode: both report through the one namespace.
        Sdi12V.crc16_args.data = (const uint8_t *)CASES[i];
        Sdi12V.crc16_args.len = n - SDI12_CRC_CHARS;
        Sdi12.crc16(sdi12_work);
        const uint16_t crc = Sdi12V.crc;
        Sdi12V.crc_encode_args.crc = crc;
        Sdi12V.crc_encode_args.out = enc;
        Sdi12.crc_encode(sdi12_work);
        TEST_ASSERT_EQUAL_CHAR_ARRAY(CASES[i] + n - SDI12_CRC_CHARS, enc, SDI12_CRC_CHARS);
    }
}

// Section 4.4.12.1 defines the CRC as initial value zero, reflected, polynomial 0xA001 - which is
// the catalogued CRC-16/ARC. Its published check value is the CRC of the nine octets "123456789",
// 0xBB3D. Encoded per 4.4.12.2 that is
//   1st = 0x40 | (0xBB3D >> 12)         = 0x40 | 0x0B = 0x4B = 'K'
//   2nd = 0x40 | ((0xBB3D >> 6) & 0x3F) = 0x40 | 0x2C = 0x6C = 'l'
//   3rd = 0x40 | (0xBB3D & 0x3F)        = 0x40 | 0x3D = 0x7D = '}'
void test_crc16_arc_check_value(void)
{
    static const uint8_t CHECK[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    Sdi12V.crc16_args.data = CHECK;
    Sdi12V.crc16_args.len = sizeof(CHECK);
    Sdi12.crc16(sdi12_work);
    TEST_ASSERT_EQUAL_HEX16(0xBB3D, Sdi12V.crc);
    TEST_ASSERT_EQUAL_HEX16(0xA001, SDI12_CRC_POLY);
    TEST_ASSERT_EQUAL_INT(3, SDI12_CRC_CHARS);

    char enc[SDI12_CRC_CHARS];
    Sdi12V.crc_encode_args.crc = 0xBB3D;
    Sdi12V.crc_encode_args.out = enc;
    Sdi12.crc_encode(sdi12_work);
    TEST_ASSERT_EQUAL_CHAR('K', enc[0]);
    TEST_ASSERT_EQUAL_CHAR('l', enc[1]);
    TEST_ASSERT_EQUAL_CHAR('}', enc[2]);
}

// Section 4.4.12.2 ORs 0x40 into three six-bit-or-less fields, so every octet lands in 0x40..0x7F -
// printable ASCII, which is what lets the CRC ride the same 7-bit line as the data.
void test_crc_encoding_is_always_printable(void)
{
    static const uint16_t CRCS[5] = {0x0000, 0xFFFF, 0xBB3D, 0x8000, 0x0FFF};
    for (size_t i = 0; i < 5; i++)
    {
        char enc[SDI12_CRC_CHARS];
        Sdi12V.crc_encode_args.crc = CRCS[i];
        Sdi12V.crc_encode_args.out = enc;
        Sdi12.crc_encode(sdi12_work);
        for (int c = 0; c < SDI12_CRC_CHARS; c++)
        {
            TEST_ASSERT_TRUE((uint8_t)enc[c] >= 0x40 && (uint8_t)enc[c] <= 0x7F);
        }
        // and the three fields are the CRC's own bits, per the published algorithm
        TEST_ASSERT_EQUAL_HEX8(0x40 | (CRCS[i] >> 12), (uint8_t)enc[0]);
        TEST_ASSERT_EQUAL_HEX8(0x40 | ((CRCS[i] >> 6) & 0x3F), (uint8_t)enc[1]);
        TEST_ASSERT_EQUAL_HEX8(0x40 | (CRCS[i] & 0x3F), (uint8_t)enc[2]);
    }
}

// A corrupted octet must fail the check, or the CRC buys nothing.
void test_corrupt_data_fails_the_crc(void)
{
    char resp[32];
    strcpy(resp, "0+3.14OqZ");
    Sdi12V.check_crc_args.resp = resp;
    Sdi12V.check_crc_args.len = 9;
    Sdi12.check_crc(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);

    // section 4.4.12.2 puts the CRC before the <CR><LF>, which the check trims
    strcpy(resp, "0+3.14OqZ\r\n");
    Sdi12V.check_crc_args.resp = resp;
    Sdi12V.check_crc_args.len = 11;
    Sdi12.check_crc(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);

    strcpy(resp, "0+3.15OqZ"); // one data digit moved
    Sdi12V.check_crc_args.resp = resp;
    Sdi12V.check_crc_args.len = 9;
    Sdi12.check_crc(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
    strcpy(resp, "0+3.14OqY"); // one CRC octet moved
    Sdi12V.check_crc_args.resp = resp;
    Sdi12V.check_crc_args.len = 9;
    Sdi12.check_crc(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);

    Sdi12V.check_crc_args.resp = NULL;
    Sdi12V.check_crc_args.len = 9;
    Sdi12.check_crc(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
    Sdi12V.check_crc_args.resp = "OqZ";
    Sdi12V.check_crc_args.len = 3;
    Sdi12.check_crc(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok); // CRC but no data octet
    Sdi12V.check_crc_args.resp = "\r\n";
    Sdi12V.check_crc_args.len = 2;
    Sdi12.check_crc(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok); // nothing left after the trim
}

// Table 5: the basic command set. Every command starts with the address and ends with '!'.
void test_spec_command_set(void)
{
    char buf[16];

    Sdi12V.build_ack_args.buf = buf;
    Sdi12V.build_ack_args.cap = sizeof(buf);
    Sdi12V.build_ack_args.addr = '0';
    Sdi12.build_ack(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(2, Sdi12V.n);
    TEST_ASSERT_EQUAL_STRING("0!", buf); // section 4.4.1.1 example
    Sdi12V.build_ack_args.buf = buf;
    Sdi12V.build_ack_args.cap = sizeof(buf);
    Sdi12V.build_ack_args.addr = '1';
    Sdi12.build_ack(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("1!", buf);

    Sdi12V.build_identify_args.buf = buf;
    Sdi12V.build_identify_args.cap = sizeof(buf);
    Sdi12V.build_identify_args.addr = '0';
    Sdi12.build_identify(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(3, Sdi12V.n);
    TEST_ASSERT_EQUAL_STRING("0I!", buf);

    Sdi12V.build_measure_args.buf = buf;
    Sdi12V.build_measure_args.cap = sizeof(buf);
    Sdi12V.build_measure_args.addr = '0';
    Sdi12V.build_measure_args.with_crc = PROTO_FALSE;
    Sdi12.build_measure(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("0M!", buf);
    Sdi12V.build_measure_args.buf = buf;
    Sdi12V.build_measure_args.cap = sizeof(buf);
    Sdi12V.build_measure_args.addr = '0';
    Sdi12V.build_measure_args.with_crc = PROTO_TRUE;
    Sdi12.build_measure(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("0MC!", buf); // section 4.4.12: the letter with a C appended

    Sdi12V.build_concurrent_args.buf = buf;
    Sdi12V.build_concurrent_args.cap = sizeof(buf);
    Sdi12V.build_concurrent_args.addr = '1';
    Sdi12V.build_concurrent_args.with_crc = PROTO_FALSE;
    Sdi12.build_concurrent(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("1C!", buf);
    Sdi12V.build_concurrent_args.buf = buf;
    Sdi12V.build_concurrent_args.cap = sizeof(buf);
    Sdi12V.build_concurrent_args.addr = '1';
    Sdi12V.build_concurrent_args.with_crc = PROTO_TRUE;
    Sdi12.build_concurrent(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("1CC!", buf);

    Sdi12V.build_verify_args.buf = buf;
    Sdi12V.build_verify_args.cap = sizeof(buf);
    Sdi12V.build_verify_args.addr = '7';
    Sdi12.build_verify(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("7V!", buf);

    Sdi12V.build_change_address_args.buf = buf;
    Sdi12V.build_change_address_args.cap = sizeof(buf);
    Sdi12V.build_change_address_args.addr = '0';
    Sdi12V.build_change_address_args.new_addr = '5';
    Sdi12.build_change_address(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("0A5!", buf); // Table 8, aAb!

    // section 4.4.3: '?' is the wild card address used with the acknowledge active command
    Sdi12V.build_query_address_args.buf = buf;
    Sdi12V.build_query_address_args.cap = sizeof(buf);
    Sdi12.build_query_address(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(2, Sdi12V.n);
    TEST_ASSERT_EQUAL_STRING("?!", buf);
}

// Table 5 again: the indexed families aD0!..aD9!, aM1!..aM9!, aC1!..aC9! and aR0!..aR9!, each with
// the CRC-requesting spelling that inserts a 'C' after the command letter.
void test_spec_indexed_commands(void)
{
    char buf[16];

    for (uint8_t d = 0; d <= 9; d++)
    {
        char want[5] = {'0', 'D', (char)('0' + d), '!', '\0'};
        Sdi12V.build_data_args.buf = buf;
        Sdi12V.build_data_args.cap = sizeof(buf);
        Sdi12V.build_data_args.addr = '0';
        Sdi12V.build_data_args.d_index = d;
        Sdi12.build_data(sdi12_work);
        TEST_ASSERT_EQUAL_size_t(4, Sdi12V.n);
        TEST_ASSERT_EQUAL_STRING(want, buf);
    }
    // section 4.4.8: the send data commands stop at D9
    Sdi12V.build_data_args.buf = buf;
    Sdi12V.build_data_args.cap = sizeof(buf);
    Sdi12V.build_data_args.addr = '0';
    Sdi12V.build_data_args.d_index = 10;
    Sdi12.build_data(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);

    Sdi12V.build_measure_additional_args.buf = buf;
    Sdi12V.build_measure_additional_args.cap = sizeof(buf);
    Sdi12V.build_measure_additional_args.addr = '0';
    Sdi12V.build_measure_additional_args.m_index = 1;
    Sdi12V.build_measure_additional_args.with_crc = PROTO_FALSE;
    Sdi12.build_measure_additional(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("0M1!", buf);
    Sdi12V.build_measure_additional_args.buf = buf;
    Sdi12V.build_measure_additional_args.cap = sizeof(buf);
    Sdi12V.build_measure_additional_args.addr = '3';
    Sdi12V.build_measure_additional_args.m_index = 9;
    Sdi12V.build_measure_additional_args.with_crc = PROTO_FALSE;
    Sdi12.build_measure_additional(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("3M9!", buf);
    Sdi12V.build_measure_additional_args.buf = buf;
    Sdi12V.build_measure_additional_args.cap = sizeof(buf);
    Sdi12V.build_measure_additional_args.addr = '1';
    Sdi12V.build_measure_additional_args.m_index = 2;
    Sdi12V.build_measure_additional_args.with_crc = PROTO_TRUE;
    Sdi12.build_measure_additional(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("1MC2!", buf);

    Sdi12V.build_concurrent_additional_args.buf = buf;
    Sdi12V.build_concurrent_additional_args.cap = sizeof(buf);
    Sdi12V.build_concurrent_additional_args.addr = '0';
    Sdi12V.build_concurrent_additional_args.c_index = 1;
    Sdi12V.build_concurrent_additional_args.with_crc = PROTO_FALSE;
    Sdi12.build_concurrent_additional(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("0C1!", buf);
    Sdi12V.build_concurrent_additional_args.buf = buf;
    Sdi12V.build_concurrent_additional_args.cap = sizeof(buf);
    Sdi12V.build_concurrent_additional_args.addr = '2';
    Sdi12V.build_concurrent_additional_args.c_index = 4;
    Sdi12V.build_concurrent_additional_args.with_crc = PROTO_TRUE;
    Sdi12.build_concurrent_additional(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("2CC4!", buf);

    Sdi12V.build_continuous_args.buf = buf;
    Sdi12V.build_continuous_args.cap = sizeof(buf);
    Sdi12V.build_continuous_args.addr = '0';
    Sdi12V.build_continuous_args.r_index = 0;
    Sdi12V.build_continuous_args.with_crc = PROTO_FALSE;
    Sdi12.build_continuous(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("0R0!", buf);
    Sdi12V.build_continuous_args.buf = buf;
    Sdi12V.build_continuous_args.cap = sizeof(buf);
    Sdi12V.build_continuous_args.addr = '2';
    Sdi12V.build_continuous_args.r_index = 5;
    Sdi12V.build_continuous_args.with_crc = PROTO_FALSE;
    Sdi12.build_continuous(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("2R5!", buf);
    Sdi12V.build_continuous_args.buf = buf;
    Sdi12V.build_continuous_args.cap = sizeof(buf);
    Sdi12V.build_continuous_args.addr = '1';
    Sdi12V.build_continuous_args.r_index = 3;
    Sdi12V.build_continuous_args.with_crc = PROTO_TRUE;
    Sdi12.build_continuous(sdi12_work);
    TEST_ASSERT_EQUAL_STRING("1RC3!", buf);

    // Table 5 gives M and C indices 1..9 (index 0 is the base aM! / aC!) and R indices 0..9.
    Sdi12V.build_measure_additional_args.buf = buf;
    Sdi12V.build_measure_additional_args.cap = sizeof(buf);
    Sdi12V.build_measure_additional_args.addr = '0';
    Sdi12V.build_measure_additional_args.m_index = 0;
    Sdi12V.build_measure_additional_args.with_crc = PROTO_FALSE;
    Sdi12.build_measure_additional(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);
    Sdi12V.build_measure_additional_args.buf = buf;
    Sdi12V.build_measure_additional_args.cap = sizeof(buf);
    Sdi12V.build_measure_additional_args.addr = '0';
    Sdi12V.build_measure_additional_args.m_index = 10;
    Sdi12V.build_measure_additional_args.with_crc = PROTO_FALSE;
    Sdi12.build_measure_additional(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);
    Sdi12V.build_concurrent_additional_args.buf = buf;
    Sdi12V.build_concurrent_additional_args.cap = sizeof(buf);
    Sdi12V.build_concurrent_additional_args.addr = '0';
    Sdi12V.build_concurrent_additional_args.c_index = 0;
    Sdi12V.build_concurrent_additional_args.with_crc = PROTO_FALSE;
    Sdi12.build_concurrent_additional(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);
    Sdi12V.build_concurrent_additional_args.buf = buf;
    Sdi12V.build_concurrent_additional_args.cap = sizeof(buf);
    Sdi12V.build_concurrent_additional_args.addr = '0';
    Sdi12V.build_concurrent_additional_args.c_index = 10;
    Sdi12V.build_concurrent_additional_args.with_crc = PROTO_FALSE;
    Sdi12.build_concurrent_additional(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);
    Sdi12V.build_continuous_args.buf = buf;
    Sdi12V.build_continuous_args.cap = sizeof(buf);
    Sdi12V.build_continuous_args.addr = '0';
    Sdi12V.build_continuous_args.r_index = 10;
    Sdi12V.build_continuous_args.with_crc = PROTO_FALSE;
    Sdi12.build_continuous(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);
}

// Table 9: the aM! response is atttn, ttt the seconds until the data is ready and n the number of
// values. Every case here is a response the specification prints in section 4.4.5 or 4.4.8.4.
void test_spec_measurement_responses(void)
{
    struct
    {
        const char *resp;
        char addr;
        uint16_t ready;
        uint8_t values;
    } static const CASES[5] = {
        {"00001\r\n", '0', 0, 1},  // 4.4.8.4 a: one value, immediately available
        {"00053\r\n", '0', 5, 3},  // 4.4.8.4 b: three values in 5 seconds
        {"00359\r\n", '0', 35, 9}, // 4.4.8.4 c: nine values in 35 seconds
        {"00012\r\n", '0', 1, 2},  // 4.4.8.4 d: two values in 1 second
        {"00101\r\n", '0', 10, 1}, // 4.4.5: "one data value will be ready in 10 seconds"
    };
    for (size_t i = 0; i < 5; i++)
    {
        char addr = 0;
        uint16_t ready = 0xFFFF;
        uint8_t n = 0xFF;
        Sdi12V.parse_measure_args.resp = CASES[i].resp;
        Sdi12V.parse_measure_args.len = strlen(CASES[i].resp);
        Sdi12V.parse_measure_args.addr = &addr;
        Sdi12V.parse_measure_args.ready_sec = &ready;
        Sdi12V.parse_measure_args.num_values = &n;
        Sdi12.parse_measure(sdi12_work);
        TEST_ASSERT_TRUE_MESSAGE(Sdi12V.ok, CASES[i].resp);
        TEST_ASSERT_EQUAL_CHAR(CASES[i].addr, addr);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].ready, ready);
        TEST_ASSERT_EQUAL_UINT8(CASES[i].values, n);
    }
}

// Table 10: the aC! response is atttnn - the same ttt, but a two-digit value count, which section
// 4.4.7 caps at 20. A parser that reads only one digit would report 2 values where 20 were made.
void test_spec_concurrent_response_has_two_count_digits(void)
{
    char addr = 0;
    uint16_t ready = 0;
    uint8_t n = 0;
    Sdi12V.parse_measure_args.resp = "001320\r\n";
    Sdi12V.parse_measure_args.len = 8;
    Sdi12V.parse_measure_args.addr = &addr;
    Sdi12V.parse_measure_args.ready_sec = &ready;
    Sdi12V.parse_measure_args.num_values = &n;
    Sdi12.parse_measure(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_CHAR('0', addr);
    TEST_ASSERT_EQUAL_UINT16(13, ready);
    TEST_ASSERT_EQUAL_UINT8(20, n);

    // and the ttt field keeps its full three-digit range
    Sdi12V.parse_measure_args.resp = "099901\r\n";
    Sdi12V.parse_measure_args.len = 8;
    Sdi12V.parse_measure_args.addr = NULL;
    Sdi12V.parse_measure_args.ready_sec = &ready;
    Sdi12V.parse_measure_args.num_values = &n;
    Sdi12.parse_measure(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_UINT16(999, ready);
    TEST_ASSERT_EQUAL_UINT8(1, n);
}

// The three outputs are each optional, and a malformed timing response is refused rather than
// half-decoded into a wait the recorder would then honor.
void test_measurement_response_edges(void)
{
    char addr = 0;
    uint16_t ready = 0;
    uint8_t n = 0;
    Sdi12V.parse_measure_args.resp = "00122\r\n";
    Sdi12V.parse_measure_args.len = 7;
    Sdi12V.parse_measure_args.addr = NULL;
    Sdi12V.parse_measure_args.ready_sec = NULL;
    Sdi12V.parse_measure_args.num_values = NULL;
    Sdi12.parse_measure(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);

    Sdi12V.parse_measure_args.resp = NULL;
    Sdi12V.parse_measure_args.len = 7;
    Sdi12V.parse_measure_args.addr = &addr;
    Sdi12V.parse_measure_args.ready_sec = &ready;
    Sdi12V.parse_measure_args.num_values = &n;
    Sdi12.parse_measure(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
    Sdi12V.parse_measure_args.resp = "012";
    Sdi12V.parse_measure_args.len = 3;
    Sdi12V.parse_measure_args.addr = &addr;
    Sdi12V.parse_measure_args.ready_sec = &ready;
    Sdi12V.parse_measure_args.num_values = &n;
    Sdi12.parse_measure(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok); // shorter than atttn
    Sdi12V.parse_measure_args.resp = "0X122";
    Sdi12V.parse_measure_args.len = 5;
    Sdi12V.parse_measure_args.addr = &addr;
    Sdi12V.parse_measure_args.ready_sec = &ready;
    Sdi12V.parse_measure_args.num_values = &n;
    Sdi12.parse_measure(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok); // non-digit in ttt
    Sdi12V.parse_measure_args.resp = "0120X";
    Sdi12V.parse_measure_args.len = 5;
    Sdi12V.parse_measure_args.addr = &addr;
    Sdi12V.parse_measure_args.ready_sec = &ready;
    Sdi12V.parse_measure_args.num_values = &n;
    Sdi12.parse_measure(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok); // non-digit count
}

// Table 11: a value is pd.d - a polarity sign, digits, an optional decimal point, digits. The sign
// is what separates one value from the next, so a run of them needs no other delimiter. The
// responses below are printed in sections 4.4.8.2 and 4.4.8.4.
void test_spec_data_responses(void)
{
    float v[8];
    size_t n = 0;

    // 4.4.8.2 / 4.4.8.4 a: one value
    Sdi12V.parse_values_args.resp = "0+3.14\r\n";
    Sdi12V.parse_values_args.len = 8;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, v[0]);

    // 4.4.8.4 b: three values
    Sdi12V.parse_values_args.resp = "0+3.14+2.718+1.414\r\n";
    Sdi12V.parse_values_args.len = 20;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.718f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.414f, v[2]);

    // 4.4.8.4 c, first group: six values
    const char *six = "0+1.11+2.22+3.33+4.44+5.55+6.66\r\n";
    Sdi12V.parse_values_args.resp = six;
    Sdi12V.parse_values_args.len = strlen(six);
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.11f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 6.66f, v[5]);

    // a negative value, since Table 11's polarity sign is either + or -
    Sdi12V.parse_values_args.resp = "0+3.14-2.5+0.001\r\n";
    Sdi12V.parse_values_args.len = 18;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -2.5f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, v[2]);
}

// The CRC octets sit between the last value and the <CR><LF>, and they never begin with a sign, so
// the splitter walks past them instead of decoding one as a value.
void test_values_ignore_the_appended_crc(void)
{
    float v[8];
    size_t n = 0;
    const char *resp = "0+3.14+2.718+1.414Ipz\r\n"; // section 4.4.12.3 b
    Sdi12V.parse_values_args.resp = resp;
    Sdi12V.parse_values_args.len = strlen(resp);
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.414f, v[2]);
}

// A caller's array is never overrun, and the response need not be terminated.
void test_values_are_bounded(void)
{
    float v[8];
    size_t n = 0;
    Sdi12V.parse_values_args.resp = "0+1+2+3\r\n";
    Sdi12V.parse_values_args.len = 9;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 2;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, v[1]);

    Sdi12V.parse_values_args.resp = "0+1.5";
    Sdi12V.parse_values_args.len = 5;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(1, n);

    Sdi12V.parse_values_args.resp = "0\r\n";
    Sdi12V.parse_values_args.len = 3;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok); // an aborted measurement
    TEST_ASSERT_EQUAL_size_t(0, n);

    Sdi12V.parse_values_args.resp = NULL;
    Sdi12V.parse_values_args.len = 3;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
    Sdi12V.parse_values_args.resp = "0+1";
    Sdi12V.parse_values_args.len = 3;
    Sdi12V.parse_values_args.out = NULL;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
    Sdi12V.parse_values_args.resp = "0+1";
    Sdi12V.parse_values_args.len = 3;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = NULL;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);

    // a sign with no digits after it is not a value
    Sdi12V.parse_values_args.resp = "0-X+2.5\r\n";
    Sdi12V.parse_values_args.len = 9;
    Sdi12V.parse_values_args.out = v;
    Sdi12V.parse_values_args.max = 8;
    Sdi12V.parse_values_args.n = &n;
    Sdi12.parse_values(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, v[0]);
}

// Table 7: the aI! response is a | ll | cccccccc | mmmmmm | vvv | optional, so the fixed part is
// 1 + 2 + 8 + 6 + 3 = 20 octets and each field is read at its own offset. The spec's own example of
// the version encoding is "version 1.3 is encoded as 13".
void test_spec_identify_field_widths(void)
{
    Sdi12Identity id;
    // vendor and model space-padded to their widths, the way the spec pads short values
    const char *resp = "013ACMEINC SNS1001.0";
    TEST_ASSERT_EQUAL_size_t(20, strlen(resp));
    Sdi12V.parse_identify_args.resp = resp;
    Sdi12V.parse_identify_args.len = strlen(resp);
    Sdi12V.parse_identify_args.out = &id;
    Sdi12.parse_identify(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_CHAR('0', id.addr);
    TEST_ASSERT_EQUAL_STRING("13", id.sdi_version);
    TEST_ASSERT_EQUAL_STRING("ACMEINC ", id.vendor);
    TEST_ASSERT_EQUAL_STRING("SNS100", id.model);
    TEST_ASSERT_EQUAL_STRING("1.0", id.sensor_version);

    // Table 7's optional field, up to 13 characters, follows the 20 and is not part of any field
    const char *with_opt = "114MYVENDORMODEL92.5SERIAL0001";
    Sdi12V.parse_identify_args.resp = with_opt;
    Sdi12V.parse_identify_args.len = strlen(with_opt);
    Sdi12V.parse_identify_args.out = &id;
    Sdi12.parse_identify(sdi12_work);
    TEST_ASSERT_TRUE(Sdi12V.ok);
    TEST_ASSERT_EQUAL_CHAR('1', id.addr);
    TEST_ASSERT_EQUAL_STRING("14", id.sdi_version);
    TEST_ASSERT_EQUAL_STRING("MYVENDOR", id.vendor);
    TEST_ASSERT_EQUAL_STRING("MODEL9", id.model);
    TEST_ASSERT_EQUAL_STRING("2.5", id.sensor_version);

    // one octet short of the fixed part is refused rather than filled from past the buffer
    Sdi12V.parse_identify_args.resp = resp;
    Sdi12V.parse_identify_args.len = 19;
    Sdi12V.parse_identify_args.out = &id;
    Sdi12.parse_identify(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
    Sdi12V.parse_identify_args.resp = NULL;
    Sdi12V.parse_identify_args.len = 20;
    Sdi12V.parse_identify_args.out = &id;
    Sdi12.parse_identify(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
    Sdi12V.parse_identify_args.resp = resp;
    Sdi12V.parse_identify_args.len = 20;
    Sdi12V.parse_identify_args.out = NULL;
    Sdi12.parse_identify(sdi12_work);
    TEST_ASSERT_FALSE(Sdi12V.ok);
}

// A command is written whole and NUL-terminated, or not at all: half a command on a 1200-baud line
// is a different command.
void test_build_refuses_a_short_buffer(void)
{
    char buf[16];
    buf[0] = 'x';
    Sdi12V.build_args.buf = NULL;
    Sdi12V.build_args.cap = sizeof(buf);
    Sdi12V.build_args.addr = '0';
    Sdi12V.build_args.body = "M";
    Sdi12.build(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);
    Sdi12V.build_args.buf = buf;
    Sdi12V.build_args.cap = sizeof(buf);
    Sdi12V.build_args.addr = '0';
    Sdi12V.build_args.body = NULL;
    Sdi12.build(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n);
    Sdi12V.build_args.buf = buf;
    Sdi12V.build_args.cap = 3;
    Sdi12V.build_args.addr = '0';
    Sdi12V.build_args.body = "M";
    Sdi12.build(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(0, Sdi12V.n); // "0M!" plus the NUL needs 4
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
    Sdi12V.build_args.buf = buf;
    Sdi12V.build_args.cap = 4;
    Sdi12V.build_args.addr = '0';
    Sdi12V.build_args.body = "M";
    Sdi12.build(sdi12_work);
    TEST_ASSERT_EQUAL_size_t(3, Sdi12V.n);
    TEST_ASSERT_EQUAL_STRING("0M!", buf);
}
