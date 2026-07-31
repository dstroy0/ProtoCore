// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the TPKT + COTP (X.224 class 0) frame codec (services/fieldbus/cotp): the TPKT
// envelope, the COTP Data TPDU, the Connection Request, and the COTP parser. Byte vectors
// per RFC 1006 / ISO 8073. Pure host tests.

#include "services/fieldbus/cotp/cotp.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_tpkt_bytes()
{
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[16];
    size_t n = pc_tpkt_build(buf, sizeof(buf), payload, sizeof(payload));
    const uint8_t expect[] = {0x03, 0x00, 0x00, 0x07, 0xAA, 0xBB, 0xCC}; // version 3, len 7
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    const uint8_t *p;
    size_t plen, consumed;
    TEST_ASSERT_TRUE(pc_tpkt_parse(buf, n, &p, &plen, &consumed));
    TEST_ASSERT_EQUAL_size_t(3, plen);
    TEST_ASSERT_EQUAL_size_t(7, consumed);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, p, 3);
}

void test_cotp_dt_bytes()
{
    const uint8_t data[] = {0x41, 0x42, 0x43}; // "ABC"
    uint8_t buf[16];
    size_t n = pc_cotp_build_dt(buf, sizeof(buf), data, sizeof(data), true);
    const uint8_t expect[] = {0x02, 0xF0, 0x80, 0x41, 0x42, 0x43}; // LI=2, DT, EOT
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    CotpHeader h;
    TEST_ASSERT_TRUE(pc_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_TRUE(h.eot);
    TEST_ASSERT_EQUAL_size_t(3, h.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, h.data, 3);
}

void test_cotp_cr_bytes()
{
    uint8_t buf[32];
    size_t n = pc_cotp_build_cr(buf, sizeof(buf), 0x0001, 0x0A, nullptr, 0);
    const uint8_t expect[] = {0x09, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00, 0xC0, 0x01, 0x0A};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    CotpHeader h;
    TEST_ASSERT_TRUE(pc_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_CR, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0000, h.dst_ref);
    TEST_ASSERT_EQUAL_HEX16(0x0001, h.src_ref);
}

// A CR with S7-style src/dst TSAP parameters appended.
void test_cotp_cr_with_tsaps()
{
    const uint8_t tsaps[] = {0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x02}; // src-tsap, dst-tsap
    uint8_t buf[32];
    size_t n = pc_cotp_build_cr(buf, sizeof(buf), 0x0002, 0x0A, tsaps, sizeof(tsaps));
    TEST_ASSERT_EQUAL_HEX8(0x11, buf[0]); // LI = 9 + 8 = 17
    CotpHeader h;
    TEST_ASSERT_TRUE(pc_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_CR, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0002, h.src_ref);
}

// A Connection Confirm (the server's response to a CR), and the CR -> CC handshake.
void test_cotp_cc_bytes()
{
    uint8_t buf[32];
    // CC echoing a client src-ref 0x0001 as the destination reference, this end's src-ref 0x0042.
    size_t n = pc_cotp_build_cc(buf, sizeof(buf), 0x0001, 0x0042, 0x0A, nullptr, 0);
    const uint8_t expect[] = {0x09, 0xD0, 0x00, 0x01, 0x00, 0x42, 0x00, 0xC0, 0x01, 0x0A};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    CotpHeader h;
    TEST_ASSERT_TRUE(pc_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_CC, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0001, h.dst_ref);
    TEST_ASSERT_EQUAL_HEX16(0x0042, h.src_ref);

    // The CR -> CC handshake: build a CR, parse it, then confirm by echoing its src-ref as the dst-ref.
    uint8_t crbuf[32];
    size_t crn = pc_cotp_build_cr(crbuf, sizeof(crbuf), 0x1234, 0x0A, nullptr, 0);
    CotpHeader cr;
    TEST_ASSERT_TRUE(pc_cotp_parse(crbuf, crn, &cr));
    n = pc_cotp_build_cc(buf, sizeof(buf), cr.src_ref, 0x0055, 0x0A, nullptr, 0);
    TEST_ASSERT_TRUE(pc_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX16(0x1234, h.dst_ref); // the CR's src-ref echoed back

    // TSAP params append after the header, and the guards fail closed.
    const uint8_t tsaps[] = {0xC2, 0x02, 0x01, 0x02};
    n = pc_cotp_build_cc(buf, sizeof(buf), 0x0001, 0x0042, 0x0A, tsaps, sizeof(tsaps));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(9 + sizeof(tsaps)), buf[0]); // LI grows by the extras
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_cc(nullptr, sizeof(buf), 1, 2, 0x0A, nullptr, 0)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_cc(buf, sizeof(buf), 1, 2, 0x0A, nullptr, 5));     // len but null params
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_cc(buf, 8, 1, 2, 0x0A, nullptr, 0));               // total > cap
}

// The full stack: a TPKT carrying a COTP Data TPDU carrying an S7-ish payload.
void test_full_stack()
{
    const uint8_t s7[] = {0x32, 0x01, 0x00, 0x00}; // S7 header start, say
    uint8_t cotp[32];
    size_t clen = pc_cotp_build_dt(cotp, sizeof(cotp), s7, sizeof(s7), true);
    uint8_t buf[48];
    size_t n = pc_tpkt_build(buf, sizeof(buf), cotp, clen);
    // total = 4 (tpkt) + 3 (cotp dt) + 4 (s7) = 11
    TEST_ASSERT_EQUAL_size_t(11, n);

    const uint8_t *p;
    size_t plen, consumed;
    TEST_ASSERT_TRUE(pc_tpkt_parse(buf, n, &p, &plen, &consumed));
    CotpHeader h;
    TEST_ASSERT_TRUE(pc_cotp_parse(p, plen, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_EQUAL_size_t(sizeof(s7), h.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(s7, h.data, sizeof(s7));
}

void test_parse_rejects_bad()
{
    const uint8_t *p;
    size_t plen, consumed;
    const uint8_t bad_ver[] = {0x04, 0x00, 0x00, 0x04}; // version != 3
    TEST_ASSERT_FALSE(pc_tpkt_parse(bad_ver, sizeof(bad_ver), &p, &plen, &consumed));
    const uint8_t short_tpkt[] = {0x03, 0x00, 0x00, 0x08, 0xAA}; // declares 8, only 5 buffered
    TEST_ASSERT_FALSE(pc_tpkt_parse(short_tpkt, sizeof(short_tpkt), &p, &plen, &consumed));

    CotpHeader h;
    const uint8_t bad_li[] = {0x05, 0xF0, 0x80}; // LI 5 but only 2 octets follow
    TEST_ASSERT_FALSE(pc_cotp_parse(bad_li, sizeof(bad_li), &h));

    uint8_t small[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_dt(small, sizeof(small), (const uint8_t *)"abcd", 4, true));
}

// Builder guards (null / oversize) and the parser's short-buffer + per-TPDU-type branches.
void test_guards_and_types()
{
    uint8_t buf[32];
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};

    TEST_ASSERT_EQUAL_size_t(0, pc_tpkt_build(nullptr, sizeof(buf), data, 3)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_tpkt_build(buf, sizeof(buf), nullptr, 3));  // len but null payload
    TEST_ASSERT_EQUAL_size_t(0, pc_tpkt_build(buf, 5, data, 3));               // total > cap

    const uint8_t *p;
    size_t plen, consumed;
    TEST_ASSERT_FALSE(pc_tpkt_parse(nullptr, 4, &p, &plen, &consumed)); // null buf
    uint8_t two[2] = {0x03, 0x00};
    TEST_ASSERT_FALSE(pc_tpkt_parse(two, 2, &p, &plen, &consumed)); // len < TPKT header

    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_dt(nullptr, sizeof(buf), data, 3, true)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_dt(buf, sizeof(buf), nullptr, 3, true));  // len but null data

    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_cr(nullptr, sizeof(buf), 1, 0x0A, nullptr, 0)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_cr(buf, sizeof(buf), 1, 0x0A, nullptr, 5));     // len but null params
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_cr(buf, 8, 1, 0x0A, nullptr, 0));               // total > cap

    CotpHeader h;
    TEST_ASSERT_FALSE(pc_cotp_parse(buf, 1, &h)); // len < 2
    uint8_t dt_short[2] = {0x01, COTP_DT};
    TEST_ASSERT_FALSE(pc_cotp_parse(dt_short, sizeof(dt_short), &h)); // DT with LI < 2 (no NR/EOT octet)
    uint8_t cr_short[6] = {0x03, COTP_CR, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_cotp_parse(cr_short, sizeof(cr_short), &h)); // CR with LI < 6
    uint8_t other[3] = {0x02, 0x80, 0x00}; // a non-DT/CR/CC type code (e.g. DR): reported, no body
    TEST_ASSERT_TRUE(pc_cotp_parse(other, sizeof(other), &h));
    TEST_ASSERT_EQUAL_HEX8(0x80, h.code);
}

// pc_tpkt_build: the zero-length-payload path (data_len==0, no payload copy) and the
// total-exceeds-0xFFFF overflow branch (distinct from the total > cap branch already covered
// above). The oversize payload pointer is never dereferenced (the function returns 0 before
// any memcpy), so it need not point at real backing memory of that size.
void test_tpkt_build_edge_cases()
{
    uint8_t buf[16];
    size_t n =
        pc_tpkt_build(buf, sizeof(buf), nullptr, 0); // payload_len==0: skip the null-payload check and the memcpy
    TEST_ASSERT_EQUAL_size_t(TPKT_HEADER_SIZE, n);
    TEST_ASSERT_EQUAL_HEX8(TPKT_VERSION, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(TPKT_HEADER_SIZE, buf[3]);

    uint8_t dummy[1];
    TEST_ASSERT_EQUAL_size_t(0, pc_tpkt_build(buf, sizeof(buf), dummy, 0x10000)); // total > 0xFFFF
}

// pc_tpkt_parse: a declared total shorter than the TPKT header itself (distinct from the
// total > len truncation branch already covered above), and all three optional out-params
// left null.
void test_tpkt_parse_edge_cases()
{
    const uint8_t too_short_total[] = {0x03, 0x00, 0x00, 0x02, 0xAA}; // declares total=2 < header(4)
    const uint8_t *p;
    size_t plen, consumed;
    TEST_ASSERT_FALSE(pc_tpkt_parse(too_short_total, sizeof(too_short_total), &p, &plen, &consumed));

    const uint8_t payload[] = {0x11, 0x22};
    uint8_t buf[16];
    size_t n = pc_tpkt_build(buf, sizeof(buf), payload, sizeof(payload));
    TEST_ASSERT_TRUE(pc_tpkt_parse(buf, n, nullptr, nullptr, nullptr)); // every out-param optional
}

// pc_cotp_build_dt: the zero-length-data path (skips the null-data check and the memcpy),
// and eot==false (the ternary's other arm).
void test_cotp_dt_edge_cases()
{
    uint8_t buf[16];
    size_t n = pc_cotp_build_dt(buf, sizeof(buf), nullptr, 0, true); // data_len==0
    TEST_ASSERT_EQUAL_size_t(COTP_DT_HEADER_LEN, n);

    const uint8_t data[] = {0x01, 0x02};
    n = pc_cotp_build_dt(buf, sizeof(buf), data, sizeof(data), false); // eot==false
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    CotpHeader h;
    TEST_ASSERT_TRUE(pc_cotp_parse(buf, n, &h));
    TEST_ASSERT_FALSE(h.eot);
}

// pc_cotp_build_cr: after_li > 0xFF (distinct from the total > cap branch already covered
// above). The check happens before any byte is written, so extra_params need not be read.
void test_cotp_cr_after_li_overflow()
{
    uint8_t buf[300];
    uint8_t extra[250]; // 9 fixed header octets + 250 > 0xFF
    TEST_ASSERT_EQUAL_size_t(0, pc_cotp_build_cr(buf, sizeof(buf), 1, 0x0A, extra, sizeof(extra)));
}

// pc_cotp_parse guard clause: null buf, null out, and li==0 (li<1), each in isolation from
// the len<2 / header>len branches already covered above.
void test_cotp_parse_guard_edge_cases()
{
    uint8_t buf[4] = {0x02, COTP_DT, 0x00, 0x00};
    CotpHeader h;
    TEST_ASSERT_FALSE(pc_cotp_parse(nullptr, sizeof(buf), &h));  // null buf
    TEST_ASSERT_FALSE(pc_cotp_parse(buf, sizeof(buf), nullptr)); // null out

    uint8_t li_zero[2] = {0x00, 0x00}; // LI==0: header==1 <= len, but li<1
    TEST_ASSERT_FALSE(pc_cotp_parse(li_zero, sizeof(li_zero), &h));
}

// pc_cotp_parse: a Connection Confirm (COTP_CC), the sibling branch of COTP_CR that the
// existing tests never exercise.
void test_cotp_parse_cc()
{
    uint8_t cc[] = {0x06, COTP_CC, 0x00, 0x03, 0x00, 0x04, 0x00}; // LI=6, dst_ref=3, src_ref=4
    CotpHeader h;
    TEST_ASSERT_TRUE(pc_cotp_parse(cc, sizeof(cc), &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_CC, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0003, h.dst_ref);
    TEST_ASSERT_EQUAL_HEX16(0x0004, h.src_ref);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_tpkt_bytes);
    RUN_TEST(test_cotp_dt_bytes);
    RUN_TEST(test_cotp_cr_bytes);
    RUN_TEST(test_cotp_cr_with_tsaps);
    RUN_TEST(test_cotp_cc_bytes);
    RUN_TEST(test_full_stack);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_guards_and_types);
    RUN_TEST(test_tpkt_build_edge_cases);
    RUN_TEST(test_tpkt_parse_edge_cases);
    RUN_TEST(test_cotp_dt_edge_cases);
    RUN_TEST(test_cotp_cr_after_li_overflow);
    RUN_TEST(test_cotp_parse_guard_edge_cases);
    RUN_TEST(test_cotp_parse_cc);
    return UNITY_END();
}
