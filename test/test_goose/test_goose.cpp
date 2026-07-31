// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/goose: the IEC 61850 GOOSE BER PDU + Ethernet frame codec.

#include "services/energy/goose/goose.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Find needle in hay; returns index or -1.
static int find(const uint8_t *hay, size_t hlen, const uint8_t *needle, size_t nlen)
{
    if (nlen > hlen)
    {
        return -1;
    }
    for (size_t i = 0; i + nlen <= hlen; i++)
    {
        if (memcmp(hay + i, needle, nlen) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

static pc_goose base(void)
{
    pc_goose g = {};
    g.gocb_ref = "X";
    g.time_allowed_to_live = 1;
    g.dat_set = "D";
    g.go_id = "G";
    g.t = nullptr; // -> 8 zero octets
    g.st_num = 1;
    g.sq_num = 2;
    g.simulation = false;
    g.conf_rev = 1;
    g.nds_com = false;
    g.num_entries = 0;
    g.all_data = nullptr;
    g.all_data_len = 0;
    return g;
}

void test_pdu_structure(void)
{
    pc_goose g = base();
    uint8_t out[128];
    size_t n = pc_goose_pdu(&g, out, sizeof(out));
    // Content is 42 octets (see goose.cpp field sizes); PDU = 61 2A <42> = 44.
    TEST_ASSERT_EQUAL_size_t(44, n);
    TEST_ASSERT_EQUAL_HEX8(0x61, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2A, out[1]); // 42
    // First field: gocbRef "X" -> 80 01 58.
    const uint8_t gocb[] = {0x80, 0x01, 'X'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(gocb, out + 2, 3);
    // sqNum = 2 -> 86 01 02 present.
    const uint8_t sq[] = {0x86, 0x01, 0x02};
    TEST_ASSERT_TRUE(find(out, n, sq, 3) >= 0);
    // simulation false -> 87 01 00; allData empty -> AB 00.
    const uint8_t sim[] = {0x87, 0x01, 0x00};
    TEST_ASSERT_TRUE(find(out, n, sim, 3) >= 0);
    const uint8_t ad[] = {0xAB, 0x00};
    TEST_ASSERT_TRUE(find(out, n, ad, 2) >= 0);
}

void test_integer_leading_zero(void)
{
    pc_goose g = base();
    g.st_num = 200; // 0xC8, MSB set -> BER positive INTEGER needs a leading 0x00: 85 02 00 C8.
    uint8_t out[128];
    size_t n = pc_goose_pdu(&g, out, sizeof(out));
    const uint8_t st[] = {0x85, 0x02, 0x00, 0xC8};
    TEST_ASSERT_TRUE(find(out, n, st, 4) >= 0);
    // A value < 0x80 stays one octet.
    g.st_num = 0x7F;
    n = pc_goose_pdu(&g, out, sizeof(out));
    const uint8_t st2[] = {0x85, 0x01, 0x7F};
    TEST_ASSERT_TRUE(find(out, n, st2, 3) >= 0);
}

void test_frame(void)
{
    pc_goose g = base();
    uint8_t dst[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t out[128];
    size_t n = pc_goose_frame(dst, src, 0x1234, &g, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 22);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(dst, out, 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(src, out + 6, 6);
    TEST_ASSERT_EQUAL_HEX8(0x88, out[12]); // ethertype GOOSE
    TEST_ASSERT_EQUAL_HEX8(0xB8, out[13]);
    TEST_ASSERT_EQUAL_HEX8(0x12, out[14]); // APPID
    TEST_ASSERT_EQUAL_HEX8(0x34, out[15]);
    uint16_t goose_len = (uint16_t)((out[16] << 8) | out[17]);
    TEST_ASSERT_EQUAL_UINT16(8 + 44, goose_len); // header(8) + PDU(44)
    TEST_ASSERT_EQUAL_HEX8(0x61, out[22]);       // APDU starts here
}

// The PDU/frame guards, the tlv-overflow fail path, and the multi-octet BER length
// encoding (long-form), driven by a >=128-octet allData.
void test_goose_error_and_longform(void)
{
    pc_goose g = base();
    uint8_t out[512];

    TEST_ASSERT_EQUAL_size_t(0, pc_goose_pdu(nullptr, out, sizeof(out))); // null g
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_pdu(&g, nullptr, sizeof(out)));  // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_pdu(&g, out, 3));                // cap < reserved header
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_pdu(&g, out, 10)); // fits the header but overflows on a field -> !ok

    // A >=128-octet allData forces multi-octet BER lengths (len_octets + write_len long-form).
    static uint8_t big[200];
    memset(big, 0x5A, sizeof(big));
    g.all_data = big;
    g.all_data_len = sizeof(big);
    size_t n = pc_goose_pdu(&g, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 200);
    TEST_ASSERT_EQUAL_HEX8(0x61, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x81, out[1]); // content length >= 128 -> long-form (one following octet)

    uint8_t dst[6] = {0};
    uint8_t src[6] = {0};
    pc_goose g2 = base();
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_frame(nullptr, src, 0x1234, &g2, out, sizeof(out))); // null dst
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_frame(dst, src, 0x1234, &g2, out, 20));              // cap < 22
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_frame(dst, src, 0x1234, &g2, out, 30)); // >=22 but the PDU cannot fit
}

// pc_ber_str's `s ? s : ""` / `s ? strnlen(...) : 0` ternaries (null gocbRef), pc_ber_bool's
// `v ? 0xFF : 0x00` ternary (simulation true), and the `g->t ? g->t : ZT` ternary (a caller-supplied
// UtcTime) all only ever took their "false"/default operand in the tests above; drive each one's
// other side here.
void test_goose_null_string_true_bool_and_time(void)
{
    pc_goose g = base();
    g.gocb_ref = nullptr; // pc_ber_str: s is null -> value "" / length 0.
    g.simulation = true;  // pc_ber_bool: v is true -> 0xFF.
    uint8_t tbuf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    g.t = tbuf; // g->t is non-null -> use it instead of ZT.
    uint8_t out[128];
    size_t n = pc_goose_pdu(&g, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    const uint8_t gocb[] = {0x80, 0x00}; // empty gocbRef
    TEST_ASSERT_TRUE(find(out, n, gocb, 2) >= 0);
    const uint8_t sim[] = {0x87, 0x01, 0xFF}; // simulation true
    TEST_ASSERT_TRUE(find(out, n, sim, 3) >= 0);
    const uint8_t t[] = {0x84, 0x08, 1, 2, 3, 4, 5, 6, 7, 8}; // caller-supplied t
    TEST_ASSERT_TRUE(find(out, n, t, sizeof(t)) >= 0);
}

// pc_goose_pdu's field chain is a run of `&&`-joined encoders; each field's success (already
// exercised above) and failure (only exercised for a couple of fields above) both need to be seen
// for full branch coverage of that field's `&&`. Drive cap to the exact cumulative size needed
// just before each successive field, so that field's tlv() always overflows while every prior
// field in the chain fit exactly. The last entry (44) fails on the final 0xAB allData tlv() itself
// -- every prior field (through 0x8A numDatSetEntries) fits exactly, so that field's `&&` is seen
// taking its "succeeded, keep going" branch right before the chain finally fails.
void test_goose_pdu_field_boundary_failures(void)
{
    pc_goose g = base();
    uint8_t out[64];
    static const size_t fail_cap[] = {
        4,  // 0x80 gocbRef
        7,  // 0x81 timeAllowedToLive
        10, // 0x82 datSet
        13, // 0x83 goID
        16, // 0x84 t
        26, // 0x85 stNum
        29, // 0x86 sqNum
        32, // 0x87 simulation
        35, // 0x88 confRev
        38, // 0x89 ndsCom
        41, // 0x8A numDatSetEntries
        44, // 0xAB allData
    };
    for (size_t i = 0; i < sizeof(fail_cap) / sizeof(fail_cap[0]); i++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_goose_pdu(&g, out, fail_cap[i]));
    }
}

// pc_goose_frame has its own null-pointer guard (dst, src, g, out) ahead of the size check;
// only the dst-null and cap<22 sides were exercised above.
void test_goose_frame_null_guards(void)
{
    pc_goose g = base();
    uint8_t dst[6] = {0};
    uint8_t src[6] = {0};
    uint8_t out[128];
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_frame(dst, nullptr, 0x1234, &g, out, sizeof(out)));  // null src
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_frame(dst, src, 0x1234, nullptr, out, sizeof(out))); // null g
    TEST_ASSERT_EQUAL_size_t(0, pc_goose_frame(dst, src, 0x1234, &g, nullptr, sizeof(out)));  // null out
}

// Build a frame with the publisher, then subscribe to it and confirm every field round-trips
// (multi-octet integers, a caller-supplied UtcTime, the true booleans, and the allData blob).
void test_parse_roundtrip(void)
{
    pc_goose g = base();
    g.gocb_ref = "GE1/LLN0$GO$gcb";
    g.time_allowed_to_live = 1000;
    g.dat_set = "GE1/LLN0$DS";
    g.go_id = "TRIP1";
    uint8_t tbuf[8] = {0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD};
    g.t = tbuf;
    g.st_num = 200;      // forces a BER leading-zero integer (85 02 00 C8)
    g.sq_num = 0x012345; // a 3-octet integer
    g.simulation = true;
    g.conf_rev = 7;
    g.nds_com = true;
    g.num_entries = 2;
    uint8_t adata[] = {0x83, 0x01, 0x01, 0x83, 0x01, 0x00}; // two boolean Data elements
    g.all_data = adata;
    g.all_data_len = sizeof(adata);

    uint8_t dst[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t frame[256];
    size_t n = pc_goose_frame(dst, src, 0x3001, &g, frame, sizeof(frame));
    TEST_ASSERT_TRUE(n > 22);

    pc_goose_rx rx;
    TEST_ASSERT_TRUE(pc_goose_parse_frame(frame, n, &rx));
    TEST_ASSERT_EQUAL_HEX16(0x3001, rx.appid);
    TEST_ASSERT_EQUAL_size_t(strlen("GE1/LLN0$GO$gcb"), rx.gocb_ref_len);
    TEST_ASSERT_EQUAL_MEMORY("GE1/LLN0$GO$gcb", rx.gocb_ref, rx.gocb_ref_len);
    TEST_ASSERT_EQUAL_UINT32(1000, rx.time_allowed_to_live);
    TEST_ASSERT_EQUAL_MEMORY("GE1/LLN0$DS", rx.dat_set, rx.dat_set_len);
    TEST_ASSERT_EQUAL_size_t(5, rx.go_id_len);
    TEST_ASSERT_EQUAL_MEMORY("TRIP1", rx.go_id, rx.go_id_len);
    TEST_ASSERT_NOT_NULL(rx.t);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tbuf, rx.t, 8);
    TEST_ASSERT_EQUAL_UINT32(200, rx.st_num);
    TEST_ASSERT_EQUAL_UINT32(0x012345, rx.sq_num);
    TEST_ASSERT_TRUE(rx.simulation);
    TEST_ASSERT_EQUAL_UINT32(7, rx.conf_rev);
    TEST_ASSERT_TRUE(rx.nds_com);
    TEST_ASSERT_EQUAL_UINT32(2, rx.num_entries);
    TEST_ASSERT_EQUAL_size_t(sizeof(adata), rx.all_data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(adata, rx.all_data, sizeof(adata));
}

void test_parse_rejects(void)
{
    pc_goose g = base();
    uint8_t dst[6] = {0};
    uint8_t src[6] = {0};
    uint8_t frame[256];
    size_t n = pc_goose_frame(dst, src, 0x1234, &g, frame, sizeof(frame));

    pc_goose_rx rx;
    // A non-GOOSE ethertype is rejected.
    uint8_t bad[256];
    memcpy(bad, frame, n);
    bad[13] = 0xB9;
    TEST_ASSERT_FALSE(pc_goose_parse_frame(bad, n, &rx));
    // A frame truncated into the PDU fails the BER definite-length bound check.
    TEST_ASSERT_FALSE(pc_goose_parse_frame(frame, 25, &rx));
    // Too short (< 24) and null guards.
    TEST_ASSERT_FALSE(pc_goose_parse_frame(frame, 23, &rx));
    TEST_ASSERT_FALSE(pc_goose_parse_frame(nullptr, n, &rx));
    TEST_ASSERT_FALSE(pc_goose_parse_frame(frame, n, nullptr));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pdu_structure);
    RUN_TEST(test_integer_leading_zero);
    RUN_TEST(test_frame);
    RUN_TEST(test_goose_error_and_longform);
    RUN_TEST(test_goose_null_string_true_bool_and_time);
    RUN_TEST(test_goose_pdu_field_boundary_failures);
    RUN_TEST(test_goose_frame_null_guards);
    RUN_TEST(test_parse_roundtrip);
    RUN_TEST(test_parse_rejects);
    return UNITY_END();
}
