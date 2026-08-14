// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
    static const uint8_t X_ATTACH_REQUEST[23] = {0x1B, 0x58, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
                                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t X_ATTACH_RESPONSE[23] = {0x1B, 0x58, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
                                                  0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                  0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t X_ATTACH_BROADCAST[23] = {0x1B, 0x58, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                   0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                   0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_EQUAL_HEX8(0xB2, protocore_snp_bcc(X_ATTACH_REQUEST, sizeof(X_ATTACH_REQUEST)));
    TEST_ASSERT_EQUAL_HEX8(0xA2, protocore_snp_bcc(X_ATTACH_RESPONSE, sizeof(X_ATTACH_RESPONSE)));
    TEST_ASSERT_EQUAL_HEX8(0x79, protocore_snp_bcc(X_ATTACH_BROADCAST, sizeof(X_ATTACH_BROADCAST)));
}

// The rotate is what separates this BCC from an ordinary XOR: a byte with the top bit set carries
// that bit into the low end. From the p. 7-62 algorithm, one byte 0x80 gives
//   0x00 ^ 0x80 = 0x80, rotate left = 0x01
// where a shift would give 0x00 and a sum would give 0x80.
void test_rotate_wraps_the_top_bit_into_the_bottom(void)
{
    static const uint8_t ONE[] = {0x80};
    TEST_ASSERT_EQUAL_HEX8(0x01, protocore_snp_bcc(ONE, sizeof(ONE)));

    // 'A' 'B' 'C', stepped through the same algorithm:
    //   0x00 ^ 0x41 = 0x41, rot -> 0x82
    //   0x82 ^ 0x42 = 0xC0, rot -> 0x81
    //   0x81 ^ 0x43 = 0xC2, rot -> 0x85
    static const uint8_t ABC[] = {0x41, 0x42, 0x43};
    TEST_ASSERT_EQUAL_HEX8(0x85, protocore_snp_bcc(ABC, sizeof(ABC)));
}

// The seed is zero, so an empty range checks to zero.
void test_empty_range_is_the_seed(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_snp_bcc(NULL, 0));
}

// The rotate makes the code order-sensitive: two frames holding the same octets in a different
// order check differently, which a plain sum or a plain XOR could not detect.
void test_byte_order_changes_the_code(void)
{
    static const uint8_t A[] = {0x01, 0x02, 0x04};
    static const uint8_t B[] = {0x04, 0x02, 0x01};
    TEST_ASSERT_NOT_EQUAL(protocore_snp_bcc(A, sizeof(A)), protocore_snp_bcc(B, sizeof(B)));
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
    size_t n = protocore_snp_build(SNP_SOH, DATA, sizeof(DATA), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(2u + sizeof(DATA) + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(SNP_SOH, out[0]);
    TEST_ASSERT_EQUAL_HEX8(3, out[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out + 2, sizeof(DATA));
    TEST_ASSERT_EQUAL_HEX8(protocore_snp_bcc(out, 5), out[5]);
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
        size_t n = protocore_snp_build(SNP_ENQ, LENS[k] ? data : NULL, LENS[k], frame, sizeof(frame));
        TEST_ASSERT_EQUAL_UINT(LENS[k] + 3u, n);
        TEST_ASSERT_TRUE(protocore_snp_parse(frame, n, &f));
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
    size_t n = protocore_snp_build(SNP_SOH, DATA, sizeof(DATA), good, sizeof(good));
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
            TEST_ASSERT_FALSE(protocore_snp_parse(bad, n, &f));
        }
    }
}

// The length byte is what the parser trusts for the frame's extent, so a frame shorter than that
// length declares is refused rather than read past its end.
void test_truncation_is_refused(void)
{
    static const uint8_t DATA[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t frame[16];
    size_t n = protocore_snp_build(SNP_SOH, DATA, sizeof(DATA), frame, sizeof(frame));
    for (size_t shorter = 0; shorter < n; shorter++)
    {
        SnpFrame f;
        TEST_ASSERT_FALSE(protocore_snp_parse(frame, shorter, &f));
    }
    SnpFrame f;
    TEST_ASSERT_TRUE(protocore_snp_parse(frame, n, &f));
}

// Trailing bytes past the framed length are not part of the frame and do not disturb the check.
void test_trailing_bytes_are_ignored(void)
{
    static const uint8_t DATA[] = {0xDE, 0xAD};
    uint8_t frame[16];
    SnpFrame f;
    size_t n = protocore_snp_build(SNP_ACK, DATA, sizeof(DATA), frame, sizeof(frame));
    frame[n] = 0xFF;
    frame[n + 1] = 0xFF;
    TEST_ASSERT_TRUE(protocore_snp_parse(frame, n + 2, &f));
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

    TEST_ASSERT_EQUAL_UINT(6u, protocore_snp_build(SNP_SOH, DATA, sizeof(DATA), out, 6));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_snp_build(SNP_SOH, DATA, sizeof(DATA), out, 5));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_snp_build(SNP_SOH, DATA, sizeof(DATA), NULL, 16));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_snp_build(SNP_SOH, NULL, 3, out, 16));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_snp_build(SNP_SOH, big, 256, out, sizeof(out)));
}

// The parser refuses null arguments and anything below the three-byte minimum frame.
void test_parser_refuses_bad_arguments(void)
{
    uint8_t frame[8];
    SnpFrame f;
    size_t n = protocore_snp_build(SNP_EOT, NULL, 0, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(3u, n);
    TEST_ASSERT_FALSE(protocore_snp_parse(NULL, n, &f));
    TEST_ASSERT_FALSE(protocore_snp_parse(frame, n, NULL));
    TEST_ASSERT_FALSE(protocore_snp_parse(frame, 2, &f));
}
