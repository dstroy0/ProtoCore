// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the authoritative DNS server (services/net/dns_server): the pure response
// builder (A-record answer, NXDOMAIN, non-A query, malformed guards, header flags) and the
// built-in name->IP table (add / case-insensitive lookup / clear).

#include "protocore_config.h" // PC_DNS_NAME_MAX / PC_DNS_SERVER_MAX_RECORDS
#include "services/net/dns_server/dns_server.h"
#include <stdint.h>
#include <stdio.h> // snprintf
#include <string.h>
#include <unity.h>

// Build a DNS query: header + one question (QNAME / QTYPE / QCLASS=IN).
static size_t make_query(uint8_t *buf, uint16_t id, const char *name, uint16_t qtype, bool rd)
{
    size_t n = 0;
    buf[n++] = (uint8_t)(id >> 8);
    buf[n++] = (uint8_t)id;
    buf[n++] = rd ? 0x01 : 0x00; // flags hi: standard query, RD bit
    buf[n++] = 0x00;             // flags lo
    buf[n++] = 0x00;
    buf[n++] = 0x01; // QDCOUNT = 1
    for (int i = 0; i < 6; i++)
    {
        buf[n++] = 0x00; // AN/NS/AR = 0
    }
    const char *p = name;
    while (*p)
    {
        const char *dot = strchr(p, '.');
        size_t label = dot ? (size_t)(dot - p) : strlen(p);
        buf[n++] = (uint8_t)label;
        memcpy(buf + n, p, label);
        n += label;
        p += label;
        if (*p == '.')
        {
            p++;
        }
    }
    buf[n++] = 0x00; // end of QNAME
    buf[n++] = (uint8_t)(qtype >> 8);
    buf[n++] = (uint8_t)qtype;
    buf[n++] = 0x00;
    buf[n++] = 0x01; // QCLASS = IN
    return n;
}

static uint32_t resolve_foo(const char *name)
{
    return strcmp(name, "foo.lan") == 0 ? 0xC0A80105u : 0; // 192.168.1.5
}
static uint32_t resolve_none(const char *name)
{
    (void)name;
    return 0;
}

void setUp()
{
    pc_dns_server_clear();
}
void tearDown()
{
}

void test_a_record_answer()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 0x1234, "foo.lan", 1, true);
    size_t n = pc_dns_server_build_response(q, qlen, 60, resolve_foo, out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT(qlen + 16, n);  // header+question copied + 16-byte A answer
    TEST_ASSERT_EQUAL_UINT8(0x12, out[0]); // id preserved
    TEST_ASSERT_EQUAL_UINT8(0x34, out[1]);
    TEST_ASSERT_TRUE(out[2] & 0x80);              // QR = 1 (response)
    TEST_ASSERT_TRUE(out[2] & 0x04);              // AA = 1
    TEST_ASSERT_TRUE(out[2] & 0x01);              // RD copied from the query
    TEST_ASSERT_EQUAL_UINT8(0x00, out[3] & 0x0F); // RCODE = 0
    TEST_ASSERT_EQUAL_UINT8(0x01, out[5]);        // QDCOUNT = 1
    TEST_ASSERT_EQUAL_UINT8(0x01, out[7]);        // ANCOUNT = 1
    // Answer record (appended at qlen): 0xC00C, A, IN, TTL, rdlen 4, 192.168.1.5
    const uint8_t *a = out + qlen;
    TEST_ASSERT_EQUAL_UINT8(0xC0, a[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0C, a[1]); // name pointer to the question
    TEST_ASSERT_EQUAL_UINT8(0x00, a[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, a[3]); // TYPE A
    TEST_ASSERT_EQUAL_UINT8(0x00, a[4]);
    TEST_ASSERT_EQUAL_UINT8(0x01, a[5]);  // CLASS IN
    TEST_ASSERT_EQUAL_UINT8(60, a[9]);    // TTL low byte
    TEST_ASSERT_EQUAL_UINT8(0x04, a[11]); // RDLENGTH 4
    TEST_ASSERT_EQUAL_UINT8(192, a[12]);
    TEST_ASSERT_EQUAL_UINT8(168, a[13]);
    TEST_ASSERT_EQUAL_UINT8(1, a[14]);
    TEST_ASSERT_EQUAL_UINT8(5, a[15]);
}

void test_nxdomain()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "unknown.lan", 1, false);
    size_t n = pc_dns_server_build_response(q, qlen, 60, resolve_none, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(qlen, n);              // no answer appended
    TEST_ASSERT_EQUAL_UINT8(0x00, out[7]);        // ANCOUNT = 0
    TEST_ASSERT_EQUAL_UINT8(0x03, out[3] & 0x0F); // RCODE = 3 (NXDOMAIN)
}

void test_non_a_query_no_error()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "foo.lan", 28, false); // AAAA
    size_t n = pc_dns_server_build_response(q, qlen, 60, resolve_foo, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(qlen, n);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[7]);        // ANCOUNT = 0
    TEST_ASSERT_EQUAL_UINT8(0x00, out[3] & 0x0F); // RCODE 0 (not NXDOMAIN - we just don't serve AAAA)
}

void test_multilabel_name_reaches_resolver()
{
    static char seen[128];
    struct L
    {
        static uint32_t cap(const char *name)
        {
            strncpy(seen, name, sizeof(seen) - 1);
            seen[sizeof(seen) - 1] = 0;
            return 0;
        }
    };
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "a.b.c.example", 1, false);
    pc_dns_server_build_response(q, qlen, 60, L::cap, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a.b.c.example", seen);
}

void test_malformed_guards()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "foo.lan", 1, false);
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, 11, 60, resolve_foo, out, sizeof(out))); // < header
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(NULL, qlen, 60, resolve_foo, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, qlen, 60, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, qlen, 60, resolve_foo, NULL, sizeof(out)));
    // A compression pointer inside the question is illegal.
    uint8_t bad[16] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 0x0C, 0, 1};
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(bad, sizeof(bad), 60, resolve_foo, out, sizeof(out)));
    // Output too small for the answer.
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, qlen, 60, resolve_foo, out, qlen + 8));
}

void test_table_add_lookup_case_insensitive()
{
    TEST_ASSERT_TRUE(pc_dns_server_add("Printer.LAN", 192, 168, 1, 10));
    TEST_ASSERT_TRUE(pc_dns_server_add("clock.lan", 192, 168, 1, 11));
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, pc_dns_server_lookup("printer.lan")); // case-insensitive hit
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, pc_dns_server_lookup("PRINTER.LAN"));
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Bu, pc_dns_server_lookup("clock.lan"));
    TEST_ASSERT_EQUAL_HEX32(0u, pc_dns_server_lookup("absent.lan"));
    // "clock" is a strict prefix of the stored "clock.lan": the compare must run past the
    // shorter probe's terminator while the stored name still has characters left.
    TEST_ASSERT_EQUAL_HEX32(0u, pc_dns_server_lookup("clock"));
    pc_dns_server_clear();
    TEST_ASSERT_EQUAL_HEX32(0u, pc_dns_server_lookup("printer.lan"));
}

void test_end_to_end_with_table()
{
    pc_dns_server_add("gw.lan", 10, 0, 0, 1);
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 0xABCD, "gw.lan", 1, false);
    size_t n = pc_dns_server_build_response(q, qlen, 60, pc_dns_server_lookup, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(qlen + 16, n);
    const uint8_t *a = out + qlen;
    TEST_ASSERT_EQUAL_UINT8(10, a[12]);
    TEST_ASSERT_EQUAL_UINT8(0, a[13]);
    TEST_ASSERT_EQUAL_UINT8(0, a[14]);
    TEST_ASSERT_EQUAL_UINT8(1, a[15]);
}

// Build a DNS query with explicit label lengths (labels of 'a'), for the oversized-name guards.
static size_t make_query_labels(uint8_t *buf, const uint8_t *label_lens, int nlabels)
{
    static const uint8_t hdr[12] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    memcpy(buf, hdr, 12);
    size_t n = 12;
    for (int i = 0; i < nlabels; i++)
    {
        buf[n++] = label_lens[i];
        for (int k = 0; k < label_lens[i]; k++)
        {
            buf[n++] = 'a';
        }
    }
    buf[n++] = 0x00; // end of QNAME
    buf[n++] = 0x00;
    buf[n++] = 0x01; // QTYPE A
    buf[n++] = 0x00;
    buf[n++] = 0x01; // QCLASS IN
    return n;
}

// A non-standard opcode (e.g. IQUERY) is answered NOTIMP (RCODE 4); too small an output is dropped.
void test_dns_opcode_notimp()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 0x2222, "foo.lan", 1, false);
    q[2] = (uint8_t)(q[2] | (2u << 3)); // opcode = 2 (STATUS)
    size_t n = pc_dns_server_build_response(q, qlen, 60, resolve_foo, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(12, n);
    TEST_ASSERT_TRUE(out[2] & 0x80);                                                           // QR = 1
    TEST_ASSERT_EQUAL_UINT8(0x04, out[3] & 0x0F);                                              // NOTIMP
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, qlen, 60, resolve_foo, out, 8)); // out_cap < 12
}

// Truncated questions are rejected: a header with no question, a label running past the datagram,
// and a name with no room for QTYPE/QCLASS.
void test_dns_truncated_questions()
{
    uint8_t out[64];
    uint8_t hdr_only[12] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(0,
                           pc_dns_server_build_response(hdr_only, sizeof(hdr_only), 60, resolve_foo, out, sizeof(out)));
    uint8_t label_past[15] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x05, 'a', 'b'}; // label len 5, only 2 bytes
    TEST_ASSERT_EQUAL_UINT(
        0, pc_dns_server_build_response(label_past, sizeof(label_past), 60, resolve_foo, out, sizeof(out)));
    uint8_t no_qtype[17] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x03, 'a', 'b', 'c', 0x00}; // name ok, no QTYPE/QCLASS
    TEST_ASSERT_EQUAL_UINT(0,
                           pc_dns_server_build_response(no_qtype, sizeof(no_qtype), 60, resolve_foo, out, sizeof(out)));
}

// A name that overflows the reassembly buffer is rejected (both the dot-insert and the label-char
// bounds guards).
void test_dns_oversized_name()
{
    uint8_t q[320], out[64];
    const uint8_t dot_overflow[3] = {63, 63, 1}; // 63 + '.' + 63 -> the dot before the 3rd label overflows
    size_t qa = make_query_labels(q, dot_overflow, 3);
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, qa, 60, resolve_foo, out, sizeof(out)));
    const uint8_t char_overflow[3] = {63, 62, 2}; // the first char of the 3rd label overflows
    size_t qb = make_query_labels(q, char_overflow, 3);
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, qb, 60, resolve_foo, out, sizeof(out)));
}

// A valid question that does not fit the output buffer (before the answer) is dropped.
void test_dns_question_exceeds_out_cap()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "foo.lan", 1, false); // qend ~ 24
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_server_build_response(q, qlen, 60, resolve_foo, out, 20));
}

// pc_dns_server_add rejects an empty/null/over-long name and a full table; lookup(NULL) is 0.
void test_dns_add_and_lookup_guards()
{
    TEST_ASSERT_FALSE(pc_dns_server_add(NULL, 1, 2, 3, 4));
    TEST_ASSERT_FALSE(pc_dns_server_add("", 1, 2, 3, 4));
    char toolong[PC_DNS_NAME_MAX + 4];
    memset(toolong, 'a', sizeof(toolong) - 1);
    toolong[sizeof(toolong) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_dns_server_add(toolong, 1, 2, 3, 4));

    char nm[16];
    for (int i = 0; i < PC_DNS_SERVER_MAX_RECORDS; i++)
    {
        snprintf(nm, sizeof(nm), "h%d.lan", i);
        TEST_ASSERT_TRUE(pc_dns_server_add(nm, 10, 0, 0, (uint8_t)i));
    }
    TEST_ASSERT_FALSE(pc_dns_server_add("overflow.lan", 10, 0, 0, 99)); // table full
    TEST_ASSERT_EQUAL_HEX32(0u, pc_dns_server_lookup(NULL));
}

// The host build's pc_dns_server_begin() is a stub (no lwIP) and reports failure.
void test_dns_begin_host_stub()
{
    TEST_ASSERT_FALSE(pc_dns_server_begin());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_a_record_answer);
    RUN_TEST(test_nxdomain);
    RUN_TEST(test_non_a_query_no_error);
    RUN_TEST(test_multilabel_name_reaches_resolver);
    RUN_TEST(test_malformed_guards);
    RUN_TEST(test_table_add_lookup_case_insensitive);
    RUN_TEST(test_end_to_end_with_table);
    RUN_TEST(test_dns_opcode_notimp);
    RUN_TEST(test_dns_truncated_questions);
    RUN_TEST(test_dns_oversized_name);
    RUN_TEST(test_dns_question_exceeds_out_cap);
    RUN_TEST(test_dns_add_and_lookup_guards);
    RUN_TEST(test_dns_begin_host_stub);
    return UNITY_END();
}
