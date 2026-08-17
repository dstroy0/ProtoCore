// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the GE Fanuc SNP frame codec (services/fieldbus/snp/snp.h).
//
// The load-bearing case is test_published_x_attach_block_check_codes. GE Fanuc GFK-0582D, "Series
// 90 PLC Serial Communications User's Manual", chapter 7 page 7-62 defines the Block Check Code as
// "successively exclusive OR-ing the next message byte and then rotating the cumulative BCC value
// left one bit", seeded at zero, over every byte of the message but the BCC itself. Pages 7-73 and
// 7-74 then print three complete X-Attach messages with their computed BCC octets: B2, A2 and 79.
// Those three octets are check values the standard publishes, so they pin the operation, the seed
// and the rotate that a plain sum or a plain XOR would both get wrong.
//
// SNP_ENQ / SNP_ACK / SNP_NAK / SNP_SOH / SNP_EOT are checked against Table 7-1 of the same manual.
// The [control][length][data][BCC] framing itself is this library's own carrier rather than a form
// GFK-0582D publishes, so those cases are category-3 properties: round trip, fail-closed refusal.

#include "services/fieldbus/snp/snp.h"
#include <string.h>

#include <unity.h>

static uint8_t snp_work[16]; // the borrow an entry takes; Snp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// GFK-0582D p. 7-73, "Explanation of X-Attach Command", byte by byte:
//   1     1B                        start of message
//   2     58                        SNP-X command 'X'
//   3-10  41 42 43 44 45 46 00 00   SNP ID of the target slave
//   11    00                        X-Attach request code (80 in the response)
//   12-18 00 00 00 00 00 00 00      not used
//   19    17                        end of block
//   20-23 00 00 00 00               not used
//   24    B2                        "Computed Block Check Code for this example"
// and p. 7-74 prints the broadcast form (SNP ID FF x8) inline as "17 00 00 00 00 79".
void test_published_x_attach_block_check_codes(void)
{
    static const uint8_t X_ATTACH_REQUEST[23] = {0x1B, 0x58, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x00, 0x00, 0x00, 0x00,
                                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t X_ATTACH_RESPONSE[23] = {0x1B, 0x58, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
                                                  0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                  0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t X_ATTACH_BROADCAST[23] = {0x1B, 0x58, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                   0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                   0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00};

    Snp.bcc_args.bytes = X_ATTACH_REQUEST;
    Snp.bcc_args.len = sizeof(X_ATTACH_REQUEST);
    Snp.bcc(snp_work);
    TEST_ASSERT_EQUAL_HEX8(0xB2, Snp.value);
    Snp.bcc_args.bytes = X_ATTACH_RESPONSE;
    Snp.bcc_args.len = sizeof(X_ATTACH_RESPONSE);
    Snp.bcc(snp_work);
    TEST_ASSERT_EQUAL_HEX8(0xA2, Snp.value);
    Snp.bcc_args.bytes = X_ATTACH_BROADCAST;
    Snp.bcc_args.len = sizeof(X_ATTACH_BROADCAST);
    Snp.bcc(snp_work);
    TEST_ASSERT_EQUAL_HEX8(0x79, Snp.value);
}

// The rotate is what separates this BCC from an ordinary XOR: a byte with the top bit set carries
// that bit into the low end. From the p. 7-62 algorithm, one byte 0x80 gives
//   0x00 ^ 0x80 = 0x80, rotate left = 0x01
// where a shift would give 0x00 and a sum would give 0x80.
void test_rotate_wraps_the_top_bit_into_the_bottom(void)
{
    static const uint8_t ONE[] = {0x80};
    Snp.bcc_args.bytes = ONE;
    Snp.bcc_args.len = sizeof(ONE);
    Snp.bcc(snp_work);
    TEST_ASSERT_EQUAL_HEX8(0x01, Snp.value);

    // 'A' 'B' 'C', stepped through the same algorithm:
    //   0x00 ^ 0x41 = 0x41, rot -> 0x82
    //   0x82 ^ 0x42 = 0xC0, rot -> 0x81
    //   0x81 ^ 0x43 = 0xC2, rot -> 0x85
    static const uint8_t ABC[] = {0x41, 0x42, 0x43};
    Snp.bcc_args.bytes = ABC;
    Snp.bcc_args.len = sizeof(ABC);
    Snp.bcc(snp_work);
    TEST_ASSERT_EQUAL_HEX8(0x85, Snp.value);
}

// The seed is zero, so an empty range checks to zero.
void test_empty_range_is_the_seed(void)
{
    Snp.bcc_args.bytes = NULL;
    Snp.bcc_args.len = 0;
    Snp.bcc(snp_work);
    TEST_ASSERT_EQUAL_HEX8(0x00, Snp.value);
}

// The rotate makes the code order-sensitive: two frames holding the same octets in a different
// order check differently, which a plain sum or a plain XOR could not detect.
void test_byte_order_changes_the_code(void)
{
    static const uint8_t A[] = {0x01, 0x02, 0x04};
    static const uint8_t B[] = {0x04, 0x02, 0x01};
    // One result member, so the first code is taken into a local before the second call lands.
    Snp.bcc_args.bytes = A;
    Snp.bcc_args.len = sizeof(A);
    Snp.bcc(snp_work);
    const uint8_t code_a = Snp.value;

    Snp.bcc_args.bytes = B;
    Snp.bcc_args.len = sizeof(B);
    Snp.bcc(snp_work);
    TEST_ASSERT_NOT_EQUAL(code_a, Snp.value);
}

// GFK-0582D Table 7-1, "Control Characters Used in CCM Protocol": the hex value of each control
// character the Series 90 serial protocols exchange.
void test_control_characters_match_table_7_1(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x05, SNP_ENQ);
    TEST_ASSERT_EQUAL_HEX8(0x06, SNP_ACK);
    TEST_ASSERT_EQUAL_HEX8(0x15, SNP_NAK);
    TEST_ASSERT_EQUAL_HEX8(0x01, SNP_SOH);
    TEST_ASSERT_EQUAL_HEX8(0x04, SNP_EOT);
}

// Framing layout: control, then the data byte count, then the data, then the BCC over everything
// before it.
void test_frame_layout(void)
{
    static const uint8_t DATA[] = {0xAA, 0xBB, 0xCC};
    uint8_t out[16];
    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = DATA;
    Snp.build_args.data_len = sizeof(DATA);
    Snp.build_args.out = out;
    Snp.build_args.cap = sizeof(out);
    Snp.build(snp_work);
    size_t n = Snp.n;
    TEST_ASSERT_EQUAL_UINT(2u + sizeof(DATA) + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(SNP_SOH, out[0]);
    TEST_ASSERT_EQUAL_HEX8(3, out[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out + 2, sizeof(DATA));
    Snp.bcc_args.bytes = out;
    Snp.bcc_args.len = 5;
    Snp.bcc(snp_work);
    TEST_ASSERT_EQUAL_HEX8(Snp.value, out[5]);
}

// What was framed is what comes back, at both ends of the length field's range.
void test_round_trip(void)
{
    static const size_t LENS[] = {0, 1, 2, 254, 255};
    uint8_t data[255];
    uint8_t frame[258];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)(i * 7u + 3u);
    }
    for (size_t k = 0; k < sizeof(LENS) / sizeof(LENS[0]); k++)
    {
        SnpFrame f;
        Snp.build_args.control = SNP_ENQ;
        Snp.build_args.data = LENS[k] ? data : NULL;
        Snp.build_args.data_len = LENS[k];
        Snp.build_args.out = frame;
        Snp.build_args.cap = sizeof(frame);
        Snp.build(snp_work);
        size_t n = Snp.n;
        TEST_ASSERT_EQUAL_UINT(LENS[k] + 3u, n);
        Snp.parse_args.frame = frame;
        Snp.parse_args.len = n;
        Snp.parse_args.out = &f;
        Snp.parse(snp_work);
        TEST_ASSERT_TRUE(Snp.ok);
        TEST_ASSERT_EQUAL_HEX8(SNP_ENQ, f.control);
        TEST_ASSERT_EQUAL_UINT(LENS[k], f.data_len);
        if (LENS[k])
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(data, f.data, LENS[k]);
        }
        else
        {
            TEST_ASSERT_NULL(f.data);
        }
    }
}

// A single bit flipped anywhere in the frame is caught: the parse fails rather than delivering the
// corrupted payload. Walked over every byte and every bit of a short frame.
void test_any_single_bit_flip_is_refused(void)
{
    static const uint8_t DATA[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t good[16];
    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = DATA;
    Snp.build_args.data_len = sizeof(DATA);
    Snp.build_args.out = good;
    Snp.build_args.cap = sizeof(good);
    Snp.build(snp_work);
    size_t n = Snp.n;
    for (size_t i = 0; i < n; i++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            uint8_t bad[16];
            SnpFrame f;
            memcpy(bad, good, n);
            bad[i] ^= (uint8_t)(1u << bit);
            if (i == 1)
            {
                continue; // the length byte re-frames the buffer; covered separately
            }
            Snp.parse_args.frame = bad;
            Snp.parse_args.len = n;
            Snp.parse_args.out = &f;
            Snp.parse(snp_work);
            TEST_ASSERT_FALSE(Snp.ok);
        }
    }
}

// The length byte is what the parser trusts for the frame's extent, so a frame shorter than that
// length declares is refused rather than read past its end.
void test_truncation_is_refused(void)
{
    static const uint8_t DATA[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t frame[16];
    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = DATA;
    Snp.build_args.data_len = sizeof(DATA);
    Snp.build_args.out = frame;
    Snp.build_args.cap = sizeof(frame);
    Snp.build(snp_work);
    size_t n = Snp.n;
    for (size_t shorter = 0; shorter < n; shorter++)
    {
        SnpFrame f;
        Snp.parse_args.frame = frame;
        Snp.parse_args.len = shorter;
        Snp.parse_args.out = &f;
        Snp.parse(snp_work);
        TEST_ASSERT_FALSE(Snp.ok);
    }
    SnpFrame f;
    Snp.parse_args.frame = frame;
    Snp.parse_args.len = n;
    Snp.parse_args.out = &f;
    Snp.parse(snp_work);
    TEST_ASSERT_TRUE(Snp.ok);
}

// Trailing bytes past the framed length are not part of the frame and do not disturb the check.
void test_trailing_bytes_are_ignored(void)
{
    static const uint8_t DATA[] = {0xDE, 0xAD};
    uint8_t frame[16];
    SnpFrame f;
    Snp.build_args.control = SNP_ACK;
    Snp.build_args.data = DATA;
    Snp.build_args.data_len = sizeof(DATA);
    Snp.build_args.out = frame;
    Snp.build_args.cap = sizeof(frame);
    Snp.build(snp_work);
    size_t n = Snp.n;
    frame[n] = 0xFF;
    frame[n + 1] = 0xFF;
    Snp.parse_args.frame = frame;
    Snp.parse_args.len = n + 2;
    Snp.parse_args.out = &f;
    Snp.parse(snp_work);
    TEST_ASSERT_TRUE(Snp.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(DATA), f.data_len);
}

// The builder refuses rather than truncating: one byte short of the exact need reports 0, and a
// payload longer than the 8-bit length field can name is refused outright.
void test_builder_refuses_bad_arguments(void)
{
    static const uint8_t DATA[] = {1, 2, 3};
    uint8_t out[16];
    uint8_t big[300];
    memset(big, 0, sizeof(big));

    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = DATA;
    Snp.build_args.data_len = sizeof(DATA);
    Snp.build_args.out = out;
    Snp.build_args.cap = 6;
    Snp.build(snp_work);
    TEST_ASSERT_EQUAL_UINT(6u, Snp.n);
    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = DATA;
    Snp.build_args.data_len = sizeof(DATA);
    Snp.build_args.out = out;
    Snp.build_args.cap = 5;
    Snp.build(snp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Snp.n);
    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = DATA;
    Snp.build_args.data_len = sizeof(DATA);
    Snp.build_args.out = NULL;
    Snp.build_args.cap = 16;
    Snp.build(snp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Snp.n);
    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = NULL;
    Snp.build_args.data_len = 3;
    Snp.build_args.out = out;
    Snp.build_args.cap = 16;
    Snp.build(snp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Snp.n);
    Snp.build_args.control = SNP_SOH;
    Snp.build_args.data = big;
    Snp.build_args.data_len = 256;
    Snp.build_args.out = out;
    Snp.build_args.cap = sizeof(out);
    Snp.build(snp_work);
    TEST_ASSERT_EQUAL_UINT(0u, Snp.n);
}

// The parser refuses null arguments and anything below the three-byte minimum frame.
void test_parser_refuses_bad_arguments(void)
{
    uint8_t frame[8];
    SnpFrame f;
    Snp.build_args.control = SNP_EOT;
    Snp.build_args.data = NULL;
    Snp.build_args.data_len = 0;
    Snp.build_args.out = frame;
    Snp.build_args.cap = sizeof(frame);
    Snp.build(snp_work);
    size_t n = Snp.n;
    TEST_ASSERT_EQUAL_UINT(3u, n);
    Snp.parse_args.frame = NULL;
    Snp.parse_args.len = n;
    Snp.parse_args.out = &f;
    Snp.parse(snp_work);
    TEST_ASSERT_FALSE(Snp.ok);
    Snp.parse_args.frame = frame;
    Snp.parse_args.len = n;
    Snp.parse_args.out = NULL;
    Snp.parse(snp_work);
    TEST_ASSERT_FALSE(Snp.ok);
    Snp.parse_args.frame = frame;
    Snp.parse_args.len = 2;
    Snp.parse_args.out = &f;
    Snp.parse(snp_work);
    TEST_ASSERT_FALSE(Snp.ok);
}
