// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HART / HART-IP codec (services/fieldbus/hart/hart.h).
//
// test_command_zero_frame is the load-bearing case. Command 0 (Read Unique Identifier) addressed to
// polling address 0 by the primary master is the one HART frame that appears verbatim in the
// FieldComm protocol literature - 02 80 00 00 82 - because it is what every host sends first, and
// the check byte is the longitudinal XOR parity of the four octets before it. The derivation is
// written out below, so a codec that checksummed the wrong span cannot reproduce it by accident.

#include "services/fieldbus/hart/hart.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The frame-type bits of the delimiter and the long-address bit are wire values.
void test_published_delimiter_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01u, HART_DELIM_BACK); // burst, field device unsolicited
    TEST_ASSERT_EQUAL_HEX8(0x02u, HART_DELIM_STX);  // master -> field device
    TEST_ASSERT_EQUAL_HEX8(0x06u, HART_DELIM_ACK);  // field device -> master
    TEST_ASSERT_EQUAL_HEX8(0x80u, HART_DELIM_LONG_ADDR);
    // The frame type occupies the low three bits, so the long-address bit never collides with it.
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(HART_DELIM_LONG_ADDR & 0x07u));

    TEST_ASSERT_EQUAL_UINT8(0u, HARTIP_MSG_REQUEST);
    TEST_ASSERT_EQUAL_UINT8(1u, HARTIP_MSG_RESPONSE);
    TEST_ASSERT_EQUAL_UINT8(2u, HARTIP_MSG_PUBLISH);
    TEST_ASSERT_EQUAL_UINT8(0u, HARTIP_ID_SESSION_INIT);
    TEST_ASSERT_EQUAL_UINT8(1u, HARTIP_ID_SESSION_CLOSE);
    TEST_ASSERT_EQUAL_UINT8(2u, HARTIP_ID_KEEPALIVE);
    TEST_ASSERT_EQUAL_UINT8(3u, HARTIP_ID_TOKEN_PDU);
    TEST_ASSERT_EQUAL_size_t(8u, (size_t)HARTIP_HEADER_LEN);
}

// Command 0, short (polling) address 0, primary master, no data:
//   02  STX, master -> field device
//   80  short address: bit 7 = primary master, bits 5..0 = polling address 0
//   00  command number 0
//   00  byte count
//   82  check byte = 0x02 ^ 0x80 ^ 0x00 ^ 0x00 = 0x82
// The preamble of 0xFF sync octets is transport and is not checksummed, so it is not built here.
void test_command_zero_frame(void)
{
    static const uint8_t ADDR[1] = {0x80};
    static const uint8_t WANT[5] = {0x02, 0x80, 0x00, 0x00, 0x82};
    uint8_t out[16];
    size_t n = protocore_hart_build(HART_DELIM_STX, ADDR, sizeof(ADDR), 0, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    HartFrame f;
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_TRUE(protocore_hart_parse(WANT, sizeof(WANT), &f));
    TEST_ASSERT_EQUAL_HEX8(0x02u, f.delimiter);
    TEST_ASSERT_EQUAL_size_t(1u, f.addr_len);
    TEST_ASSERT_EQUAL_HEX8(0x80u, f.addr[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, f.command);
    TEST_ASSERT_EQUAL_UINT8(0u, f.byte_count);
    TEST_ASSERT_NULL(f.data);
}

// A longitudinal XOR parity is self-inverse, so the frame with its check byte appended XORs to
// zero whatever the content. That is the property a receiver tests.
void test_checksum_folds_the_frame_to_zero(void)
{
    static const uint8_t DATA[6] = {0xFE, 0x00, 0x18, 0x9A, 0x02, 0x01};
    static const uint8_t ADDR[5] = {0x86, 0x1C, 0x11, 0x22, 0x33};
    uint8_t out[32];
    size_t n = protocore_hart_build(HART_DELIM_ACK | HART_DELIM_LONG_ADDR, ADDR, sizeof(ADDR), 0, DATA, sizeof(DATA),
                                    out, sizeof(out));
    // delimiter + 5 address + command + byte count + 6 data + check = 15.
    TEST_ASSERT_EQUAL_size_t(15u, n);
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_hart_checksum(out, n));

    // An empty span has nothing to fold.
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_hart_checksum(DATA, 0));
}

// The long-address bit in the delimiter, not the frame length, is what tells a parser the address
// is five octets. The same five data octets under a short delimiter parse into a different frame.
void test_long_address_is_driven_by_the_delimiter(void)
{
    static const uint8_t ADDR5[5] = {0x86, 0x1C, 0x11, 0x22, 0x33};
    static const uint8_t DATA[2] = {0xAA, 0xBB};
    uint8_t out[32];
    size_t n = protocore_hart_build((uint8_t)(HART_DELIM_STX | HART_DELIM_LONG_ADDR), ADDR5, sizeof(ADDR5), 3, DATA,
                                    sizeof(DATA), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(11u, n); // 1 + 5 + 1 + 1 + 2 + 1
    TEST_ASSERT_EQUAL_HEX8(0x82u, out[0]);

    HartFrame f;
    TEST_ASSERT_TRUE(protocore_hart_parse(out, n, &f));
    TEST_ASSERT_EQUAL_size_t(5u, f.addr_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ADDR5, f.addr, 5);
    TEST_ASSERT_EQUAL_HEX8(3u, f.command);
    TEST_ASSERT_EQUAL_UINT8(2u, f.byte_count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, f.data, 2);

    // Only one and five are legal address widths.
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hart_build(HART_DELIM_STX, ADDR5, 2, 0, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hart_build(HART_DELIM_STX, ADDR5, 0, 0, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hart_build(HART_DELIM_STX, NULL, 1, 0, NULL, 0, out, sizeof(out)));
}

// Every data length from empty to the widest this test carries survives build then parse.
void test_frame_round_trip(void)
{
    static const uint8_t DATA[8] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF, 0x55, 0xAA};
    static const uint8_t ADDR[1] = {0x81};
    for (size_t n = 0; n <= sizeof(DATA); n++)
    {
        uint8_t out[32];
        size_t len = protocore_hart_build(HART_DELIM_ACK, ADDR, sizeof(ADDR), 1, n ? DATA : NULL, n, out, sizeof(out));
        TEST_ASSERT_EQUAL_size_t(n + 5u, len);

        HartFrame f;
        memset(&f, 0, sizeof(f));
        TEST_ASSERT_TRUE(protocore_hart_parse(out, len, &f));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)n, f.byte_count);
        TEST_ASSERT_EQUAL_size_t(n, f.data_len);
        if (n)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, f.data, n);
        }
        else
        {
            TEST_ASSERT_NULL(f.data);
        }
    }
}

// A single-bit change anywhere in the frame flips a check-byte bit, so the parser refuses it.
void test_single_bit_corruption_is_refused(void)
{
    static const uint8_t ADDR[1] = {0x80};
    static const uint8_t DATA[3] = {0x11, 0x22, 0x33};
    uint8_t frame[16];
    size_t len = protocore_hart_build(HART_DELIM_STX, ADDR, sizeof(ADDR), 6, DATA, sizeof(DATA), frame, sizeof(frame));

    for (size_t i = 0; i < len; i++)
    {
        // The byte-count octet re-lengths the frame rather than breaking parity, so it is covered
        // by the truncation case instead.
        if (i == 3)
        {
            continue;
        }
        uint8_t bad[16];
        memcpy(bad, frame, len);
        bad[i] ^= 0x01;
        HartFrame f;
        TEST_ASSERT_FALSE(protocore_hart_parse(bad, len, &f));
    }
}

// A byte count that claims more data than arrived is refused, not read past.
void test_parse_refuses_a_truncated_frame(void)
{
    static const uint8_t ADDR[1] = {0x80};
    static const uint8_t DATA[4] = {1, 2, 3, 4};
    uint8_t frame[16];
    size_t len = protocore_hart_build(HART_DELIM_STX, ADDR, sizeof(ADDR), 3, DATA, sizeof(DATA), frame, sizeof(frame));
    TEST_ASSERT_EQUAL_size_t(9u, len);

    HartFrame f;
    TEST_ASSERT_TRUE(protocore_hart_parse(frame, len, &f));
    TEST_ASSERT_FALSE(protocore_hart_parse(frame, len - 1, &f)); // the check byte is missing
    TEST_ASSERT_FALSE(protocore_hart_parse(frame, 4, &f));       // shorter than the smallest frame
    TEST_ASSERT_FALSE(protocore_hart_parse(NULL, len, &f));
    TEST_ASSERT_FALSE(protocore_hart_parse(frame, len, NULL));
}

// The HART-IP header is eight octets: version, message type, message id, status, then a big-endian
// sequence number and a big-endian byte count that includes the header itself.
void test_hartip_header_octets(void)
{
    static const uint8_t WANT[8] = {
        0x01,       // protocol version 1
        0x00,       // message type: request
        0x03,       // message id: token-passing PDU
        0x00,       // status
        0x12, 0x34, // sequence number, big-endian
        0x00, 0x0D, // byte count: 8 header + 5 payload = 13
    };
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(
        8u, protocore_hartip_build_header(HARTIP_MSG_REQUEST, HARTIP_ID_TOKEN_PDU, 0, 0x1234, 13, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hartip_build_header(0, 0, 0, 0, 8, out, 7));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_hartip_build_header(0, 0, 0, 0, 8, NULL, sizeof(out)));
}

// The header's byte count slices the payload, which for a token PDU is a whole HART frame.
void test_hartip_payload_slice(void)
{
    static const uint8_t PDU[5] = {0x02, 0x80, 0x00, 0x00, 0x82}; // the command-0 frame
    uint8_t msg[16];
    TEST_ASSERT_EQUAL_size_t(8u, protocore_hartip_build_header(HARTIP_MSG_REQUEST, HARTIP_ID_TOKEN_PDU, 0, 1,
                                                               (uint16_t)(8 + sizeof(PDU)), msg, sizeof(msg)));
    memcpy(msg + 8, PDU, sizeof(PDU));

    HartIpHeader h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_TRUE(protocore_hartip_parse_header(msg, 13, &h));
    TEST_ASSERT_EQUAL_UINT8(1u, h.version);
    TEST_ASSERT_EQUAL_UINT8(HARTIP_MSG_REQUEST, h.msg_type);
    TEST_ASSERT_EQUAL_UINT8(HARTIP_ID_TOKEN_PDU, h.msg_id);
    TEST_ASSERT_EQUAL_UINT8(0u, h.status);
    TEST_ASSERT_EQUAL_HEX16(1u, h.seq);
    TEST_ASSERT_EQUAL_UINT16(13u, h.total_len);
    TEST_ASSERT_EQUAL_size_t(sizeof(PDU), h.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PDU, h.payload, sizeof(PDU));

    // The sliced payload is itself a valid HART frame.
    HartFrame f;
    TEST_ASSERT_TRUE(protocore_hart_parse(h.payload, h.payload_len, &f));
    TEST_ASSERT_EQUAL_HEX8(0x00u, f.command);
}

// A byte count below the header width, or above what arrived, is refused: both would hand out a
// payload slice that does not exist.
void test_hartip_refuses_impossible_byte_counts(void)
{
    uint8_t msg[16];
    memset(msg, 0, sizeof(msg));
    HartIpHeader h;

    msg[6] = 0x00;
    msg[7] = 0x07; // below the 8-octet header
    TEST_ASSERT_FALSE(protocore_hartip_parse_header(msg, 16, &h));

    msg[7] = 0x08; // exactly the header, no payload
    TEST_ASSERT_TRUE(protocore_hartip_parse_header(msg, 16, &h));
    TEST_ASSERT_EQUAL_size_t(0u, h.payload_len);
    TEST_ASSERT_NULL(h.payload);

    msg[7] = 0x11; // 17 octets declared, 16 present
    TEST_ASSERT_FALSE(protocore_hartip_parse_header(msg, 16, &h));

    msg[7] = 0x08;
    TEST_ASSERT_FALSE(protocore_hartip_parse_header(msg, 7, &h));
    TEST_ASSERT_FALSE(protocore_hartip_parse_header(NULL, 16, &h));
    TEST_ASSERT_FALSE(protocore_hartip_parse_header(msg, 16, NULL));
}
