// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the MELSEC MC binary 3E codec (services/fieldbus/melsec/melsec.h).
//
// The load-bearing case is test_mitsubishi_batch_read_example: the MELSEC Communication Protocol
// Reference Manual (SH(NA)-080008, appendix 7 "Setting Examples") prints a complete binary 3E batch
// read request and its response, octet by octet, for M100 with two word points. Both are reproduced
// here verbatim. Every little-endian field this codec writes - the 0x03FF destination module I/O
// number, the request data length, the command, the 3-octet head device - is fixed by that one
// example, so a byte order or an offset that drifts cannot pass.

#include "services/fieldbus/melsec/melsec.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// SH(NA)-080008 appendix 7, "Device reading", binary code / 3E frame: batch read in word units
// (command 0401) of 2 points from M100, monitoring timer 0010h (16 x 250 ms = 4 seconds).
static const uint8_t MITSUBISHI_REQ[MELSEC_3E_READ_REQ_LEN] = {
    0x50, 0x00,       // subheader (request)
    0x00,             // network number
    0xFF,             // PC number
    0xFF, 0x03,       // request destination module I/O number 03FFh
    0x00,             // request destination module station number
    0x0C, 0x00,       // request data length = 12
    0x10, 0x00,       // monitoring timer 0010h
    0x01, 0x04,       // command 0401h
    0x00, 0x00,       // subcommand 0000h
    0x64, 0x00, 0x00, // head device number 100
    0x90,             // device code M
    0x02, 0x00,       // number of device points = 2
};

// The published response to that request: end code 0000h and 4 octets of read data.
static const uint8_t MITSUBISHI_RES[] = {
    0xD0, 0x00,                               // subheader (response)
    0x00, 0xFF, 0xFF, 0x03, 0x00, 0x06, 0x00, // response data length = 6 (end code 2 + data 4)
    0x00, 0x00,                               // end code 0000h
    0x34, 0x12, 0x02, 0x00,                   // read data
};

void test_mitsubishi_batch_read_example(void)
{
    uint8_t buf[64];
    size_t n = protocore_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_M, 100, 2, 0x0010);
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MITSUBISHI_REQ, buf, MELSEC_3E_READ_REQ_LEN);

    MelsecResponse r;
    TEST_ASSERT_TRUE(protocore_melsec_parse_response(MITSUBISHI_RES, sizeof(MITSUBISHI_RES), &r));
    TEST_ASSERT_EQUAL_HEX16(MELSEC_ENDCODE_OK, r.end_code);
    TEST_ASSERT_EQUAL_UINT(4u, r.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MITSUBISHI_RES + MELSEC_3E_RES_DATA_OFFSET, r.data, 4);
}

// SH(NA)-080008 chapter 8.1, "Data communication in binary code": reading D350 and D351 is written
//     5EH 01H 00H  A8H  02H 00H  ABH 56H  0FH 17H
// i.e. head device 350 as three little-endian octets, device code A8h for D, then the point count.
// The device / head / points tail of a batch-read request is exactly that layout.
void test_head_device_and_device_code_layout(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                           protocore_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_D, 350, 2, 0));
    static const uint8_t TAIL[6] = {0x5E, 0x01, 0x00, 0xA8, 0x02, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TAIL, buf + 15, 6);

    // the head device number field is 24 bits wide, so its top octet carries bits 16..23
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                           protocore_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_D, 0xFFFFFFu, 1, 0));
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[15]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[17]);
    // anything above 24 bits belongs to no field and is dropped
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                           protocore_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_D, 0x01000064u, 1, 0));
    TEST_ASSERT_EQUAL_HEX8(0x64, buf[15]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[17]);
}

// SH(NA)-080008 chapter 8.1, "Device code list": the binary device codes for the MELSEC-Q/L series.
void test_device_code_list(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xA8, MELSEC_DEV_D);  // data register D
    TEST_ASSERT_EQUAL_HEX8(0xAF, MELSEC_DEV_R);  // file register R (block switching method)
    TEST_ASSERT_EQUAL_HEX8(0x90, MELSEC_DEV_M);  // internal relay M
    TEST_ASSERT_EQUAL_HEX8(0x98, MELSEC_DEV_S);  // step relay S
    TEST_ASSERT_EQUAL_HEX8(0x9C, MELSEC_DEV_X);  // input X
    TEST_ASSERT_EQUAL_HEX8(0x9D, MELSEC_DEV_Y);  // output Y
    TEST_ASSERT_EQUAL_HEX8(0xC2, MELSEC_DEV_TN); // timer current value TN
    TEST_ASSERT_EQUAL_HEX8(0xC1, MELSEC_DEV_TS); // timer contact TS
    TEST_ASSERT_EQUAL_HEX8(0xC5, MELSEC_DEV_CN); // counter current value CN
    TEST_ASSERT_EQUAL_HEX8(0xC4, MELSEC_DEV_CS); // counter contact CS

    // the device code is one octet in the frame, written whatever it is
    uint8_t buf[64];
    for (size_t i = 0; i < 4; i++)
    {
        static const uint8_t CODES[4] = {MELSEC_DEV_D, MELSEC_DEV_M, MELSEC_DEV_X, MELSEC_DEV_CN};
        TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                               protocore_melsec_build_read(buf, sizeof(buf), CODES[i], 0, 1, 0));
        TEST_ASSERT_EQUAL_HEX8(CODES[i], buf[18]);
    }
}

// SH(NA)-080008 chapter 5.3: the request data length counts the octets from the monitoring timer to
// the end of the request data. For a batch read that is timer(2) + command(2) + subcommand(2) +
// head device(3) + device code(1) + points(2) = 12, and the whole request is the 9 routing octets
// before it plus those 12 = 21.
void test_request_data_length_counts_from_the_monitoring_timer(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                           protocore_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_D, 0, 1, 0));
    uint16_t declared = (uint16_t)(buf[7] | (buf[8] << 8));
    TEST_ASSERT_EQUAL_UINT16(MELSEC_3E_READ_REQ_DATA_LEN, declared);
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN, 9u + declared);
}

// SH(NA)-080008 chapter 5.3: batch write in word units is command 1401h, and its data length is the
// read's 12 plus the write data octets (two per word point).
void test_batch_write_command_and_length(void)
{
    static const uint8_t DATA[4] = {0x34, 0x12, 0x02, 0x00};
    uint8_t buf[64];
    size_t n = protocore_melsec_build_write(buf, sizeof(buf), MELSEC_DEV_D, 100, 2, 0x0010, DATA, sizeof(DATA));
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN + sizeof(DATA), n);

    TEST_ASSERT_EQUAL_UINT16(MELSEC_3E_READ_REQ_DATA_LEN + sizeof(DATA), (uint16_t)(buf[7] | (buf[8] << 8)));
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[11]); // command 1401h, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x14, buf[12]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[13]); // subcommand 0000h (word units)
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[14]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, buf + MELSEC_3E_READ_REQ_LEN, sizeof(DATA));

    // the routing prefix and device tail are the same fields the read writes
    uint8_t rd[64];
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                           protocore_melsec_build_read(rd, sizeof(rd), MELSEC_DEV_D, 100, 2, 0x0010));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rd, buf, 7);           // subheader + access route
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rd + 15, buf + 15, 6); // head device + code + points
}

// SH(NA)-080008 chapter 5.3: a request with no write data is still a well-formed frame, and its
// data length falls back to the fixed 12.
void test_write_with_no_data(void)
{
    uint8_t buf[64];
    size_t n = protocore_melsec_build_write(buf, sizeof(buf), MELSEC_DEV_D, 0, 0, 0, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN, n);
    TEST_ASSERT_EQUAL_UINT16(MELSEC_3E_READ_REQ_DATA_LEN, (uint16_t)(buf[7] | (buf[8] << 8)));
}

// SH(NA)-080008 chapter 5.3: the monitoring timer is a 2-octet value sent low byte first, 0000h
// meaning wait indefinitely.
void test_monitoring_timer_is_little_endian(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                           protocore_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_D, 0, 1, 0x1234));
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[10]);

    TEST_ASSERT_EQUAL_UINT(MELSEC_3E_READ_REQ_LEN,
                           protocore_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_D, 0, 1, 0));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[10]);
}

// A response whose subheader is not D0 00 is not a 3E response.
void test_response_subheader_is_checked(void)
{
    uint8_t bad[sizeof(MITSUBISHI_RES)];
    MelsecResponse r;

    memcpy(bad, MITSUBISHI_RES, sizeof(bad));
    bad[0] = 0x50; // the request subheader, echoed back by mistake
    TEST_ASSERT_FALSE(protocore_melsec_parse_response(bad, sizeof(bad), &r));

    memcpy(bad, MITSUBISHI_RES, sizeof(bad));
    bad[1] = 0x01;
    TEST_ASSERT_FALSE(protocore_melsec_parse_response(bad, sizeof(bad), &r));
}

// SH(NA)-080008 chapter 5.3: at abnormal completion the end code carries the access target's error
// code and no read data follows, so the length field is the 2 end-code octets alone.
void test_error_end_code_response(void)
{
    static const uint8_t ERR[] = {
        0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, // response data length = 2 (the end code only)
        0x51, 0xC0,                                           // end code C051h
    };
    MelsecResponse r;
    TEST_ASSERT_TRUE(protocore_melsec_parse_response(ERR, sizeof(ERR), &r));
    TEST_ASSERT_EQUAL_HEX16(0xC051, r.end_code);
    TEST_ASSERT_EQUAL_UINT(0u, r.data_len);
}

// The response data length counts the end code, so it can never be below 2, and it must not claim
// more octets than the frame carries.
void test_response_length_field_is_validated(void)
{
    uint8_t bad[sizeof(MITSUBISHI_RES)];
    MelsecResponse r;

    memcpy(bad, MITSUBISHI_RES, sizeof(bad));
    bad[MELSEC_3E_RES_LEN_OFFSET] = 0x01; // shorter than the end code itself
    bad[MELSEC_3E_RES_LEN_OFFSET + 1] = 0x00;
    TEST_ASSERT_FALSE(protocore_melsec_parse_response(bad, sizeof(bad), &r));

    memcpy(bad, MITSUBISHI_RES, sizeof(bad));
    bad[MELSEC_3E_RES_LEN_OFFSET] = 0x40; // claims 64 octets the frame does not hold
    TEST_ASSERT_FALSE(protocore_melsec_parse_response(bad, sizeof(bad), &r));

    // a frame shorter than subheader..end code cannot be parsed at all
    for (size_t n = 0; n < MELSEC_3E_RES_MIN_LEN; n++)
    {
        TEST_ASSERT_FALSE(protocore_melsec_parse_response(MITSUBISHI_RES, n, &r));
    }
}

// The builders write nothing and report 0 when the buffer cannot hold the whole frame, or when a
// nonzero data length comes with a null pointer.
void test_builders_refuse_bad_arguments(void)
{
    uint8_t buf[64];
    static const uint8_t DATA[4] = {1, 2, 3, 4};

    for (size_t cap = 0; cap < MELSEC_3E_READ_REQ_LEN; cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_melsec_build_read(buf, cap, MELSEC_DEV_D, 0, 1, 0));
    }
    TEST_ASSERT_EQUAL_UINT(0u, protocore_melsec_build_read(NULL, sizeof(buf), MELSEC_DEV_D, 0, 1, 0));

    TEST_ASSERT_EQUAL_UINT(
        0u, protocore_melsec_build_write(buf, MELSEC_3E_READ_REQ_LEN + 3, MELSEC_DEV_D, 0, 2, 0, DATA, sizeof(DATA)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_melsec_build_write(buf, sizeof(buf), MELSEC_DEV_D, 0, 2, 0, NULL, 4));
    // the request-length field is 16 bits, so the write data cannot exceed 0xFFFF minus the fixed 12
    TEST_ASSERT_EQUAL_UINT(0u, protocore_melsec_build_write(buf, sizeof(buf), MELSEC_DEV_D, 0, 2, 0, DATA,
                                                            (size_t)0xFFFFu - MELSEC_3E_READ_REQ_DATA_LEN + 1u));

    MelsecResponse r;
    TEST_ASSERT_FALSE(protocore_melsec_parse_response(NULL, sizeof(MITSUBISHI_RES), &r));
    TEST_ASSERT_FALSE(protocore_melsec_parse_response(MITSUBISHI_RES, sizeof(MITSUBISHI_RES), NULL));
}
