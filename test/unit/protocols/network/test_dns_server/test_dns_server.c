// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/network/dns/dns_server.h"
#include "network_drivers/transport/udp/server/server.h"
#include "network_drivers/transport/udp/udp.h"
#include "protocore_config.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

static size_t make_query(uint8_t *buf, uint16_t id, const char *name, uint16_t qtype, proto_bool rd)
{
    size_t n = 0;
    buf[n++] = (uint8_t)(id >> 8);
    buf[n++] = (uint8_t)id;
    buf[n++] = rd ? 0x01 : 0x00;
    buf[n++] = 0x00;
    buf[n++] = 0x00;
    buf[n++] = 0x01;
    for (int i = 0; i < 6; i++)
    {
        buf[n++] = 0x00;
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
    buf[n++] = 0x00;
    buf[n++] = (uint8_t)(qtype >> 8);
    buf[n++] = (uint8_t)qtype;
    buf[n++] = 0x00;
    buf[n++] = 0x01;
    return n;
}

static uint32_t resolve_foo(const char *name)
{
    return strcmp(name, "foo.lan") == 0 ? 0xC0A80105u : 0;
}
static uint32_t resolve_none(const char *name)
{
    (void)name;
    return 0;
}

void setUp()
{
    DnsServer.clear(protocore_dns_server_span());
}
void tearDown()
{
}

void test_a_record_answer()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 0x1234, "foo.lan", 1, PROTO_TRUE);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    size_t n = DnsServer.n;

    TEST_ASSERT_EQUAL_UINT(qlen + 16, n);
    TEST_ASSERT_EQUAL_UINT8(0x12, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34, out[1]);
    TEST_ASSERT_TRUE(out[2] & 0x80);
    TEST_ASSERT_TRUE(out[2] & 0x04);
    TEST_ASSERT_TRUE(out[2] & 0x01);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[3] & 0x0F);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[5]);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[7]);

    const uint8_t *a = out + qlen;
    TEST_ASSERT_EQUAL_UINT8(0xC0, a[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0C, a[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, a[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, a[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, a[4]);
    TEST_ASSERT_EQUAL_UINT8(0x01, a[5]);
    TEST_ASSERT_EQUAL_UINT8(60, a[9]);
    TEST_ASSERT_EQUAL_UINT8(0x04, a[11]);
    TEST_ASSERT_EQUAL_UINT8(192, a[12]);
    TEST_ASSERT_EQUAL_UINT8(168, a[13]);
    TEST_ASSERT_EQUAL_UINT8(1, a[14]);
    TEST_ASSERT_EQUAL_UINT8(5, a[15]);
}

void test_nxdomain()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "unknown.lan", 1, PROTO_FALSE);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_none;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    size_t n = DnsServer.n;
    TEST_ASSERT_EQUAL_UINT(qlen, n);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[7]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[3] & 0x0F);
}

void test_non_a_query_no_error()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "foo.lan", 28, PROTO_FALSE);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    size_t n = DnsServer.n;
    TEST_ASSERT_EQUAL_UINT(qlen, n);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[7]);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[3] & 0x0F);
}

static char g_seen[128];
static uint32_t capture_name(const char *name)
{
    strncpy(g_seen, name, sizeof(g_seen) - 1);
    g_seen[sizeof(g_seen) - 1] = 0;
    return 0;
}

void test_multilabel_name_reaches_resolver()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "a.b.c.example", 1, PROTO_FALSE);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = capture_name;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_STRING("a.b.c.example", g_seen);
}

void test_malformed_guards()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "foo.lan", 1, PROTO_FALSE);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = 11;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
    DnsServer.msg.query = NULL;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = NULL;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = NULL;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);

    uint8_t bad[16] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 0x0C, 0, 1};
    DnsServer.msg.query = bad;
    DnsServer.msg.qlen = sizeof(bad);
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);

    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = qlen + 8;
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
}

void test_table_add_lookup_case_insensitive()
{
    DnsServer.rec.name = "Printer.LAN";
    DnsServer.rec.a = 192;
    DnsServer.rec.b = 168;
    DnsServer.rec.c = 1;
    DnsServer.rec.d = 10;
    DnsServer.add(protocore_dns_server_span());
    TEST_ASSERT_TRUE(DnsServer.ok);
    DnsServer.rec.name = "clock.lan";
    DnsServer.rec.a = 192;
    DnsServer.rec.b = 168;
    DnsServer.rec.c = 1;
    DnsServer.rec.d = 11;
    DnsServer.add(protocore_dns_server_span());
    TEST_ASSERT_TRUE(DnsServer.ok);
    DnsServer.rec.name = "printer.lan";
    DnsServer.lookup(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, DnsServer.ip);
    DnsServer.rec.name = "PRINTER.LAN";
    DnsServer.lookup(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, DnsServer.ip);
    DnsServer.rec.name = "clock.lan";
    DnsServer.lookup(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_HEX32(0xC0A8010Bu, DnsServer.ip);
    DnsServer.rec.name = "absent.lan";
    DnsServer.lookup(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_HEX32(0u, DnsServer.ip);

    DnsServer.rec.name = "clock";
    DnsServer.lookup(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_HEX32(0u, DnsServer.ip);
    DnsServer.clear(protocore_dns_server_span());
    DnsServer.rec.name = "printer.lan";
    DnsServer.lookup(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_HEX32(0u, DnsServer.ip);
}

void test_end_to_end_with_table()
{
    DnsServer.rec.name = "gw.lan";
    DnsServer.rec.a = 10;
    DnsServer.rec.b = 0;
    DnsServer.rec.c = 0;
    DnsServer.rec.d = 1;
    DnsServer.add(protocore_dns_server_span());
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 0xABCD, "gw.lan", 1, PROTO_FALSE);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = protocore_dns_server_resolve; // the built-in table as a DnsResolveFn
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    size_t n = DnsServer.n;
    TEST_ASSERT_EQUAL_UINT(qlen + 16, n);
    const uint8_t *a = out + qlen;
    TEST_ASSERT_EQUAL_UINT8(10, a[12]);
    TEST_ASSERT_EQUAL_UINT8(0, a[13]);
    TEST_ASSERT_EQUAL_UINT8(0, a[14]);
    TEST_ASSERT_EQUAL_UINT8(1, a[15]);
}

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
    buf[n++] = 0x00;
    buf[n++] = 0x00;
    buf[n++] = 0x01;
    buf[n++] = 0x00;
    buf[n++] = 0x01;
    return n;
}

void test_dns_opcode_notimp()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 0x2222, "foo.lan", 1, PROTO_FALSE);
    q[2] = (uint8_t)(q[2] | (2u << 3));
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    size_t n = DnsServer.n;
    TEST_ASSERT_EQUAL_UINT(12, n);
    TEST_ASSERT_TRUE(out[2] & 0x80);
    TEST_ASSERT_EQUAL_UINT8(0x04, out[3] & 0x0F);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = 8;
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
}

void test_dns_truncated_questions()
{
    uint8_t out[64];
    uint8_t hdr_only[12] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    DnsServer.msg.query = hdr_only;
    DnsServer.msg.qlen = sizeof(hdr_only);
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
    uint8_t label_past[15] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x05, 'a', 'b'};
    DnsServer.msg.query = label_past;
    DnsServer.msg.qlen = sizeof(label_past);
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
    uint8_t no_qtype[17] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x03, 'a', 'b', 'c', 0x00};
    DnsServer.msg.query = no_qtype;
    DnsServer.msg.qlen = sizeof(no_qtype);
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
}

void test_dns_oversized_name()
{
    uint8_t q[320], out[64];
    const uint8_t dot_overflow[3] = {63, 63, 1};
    size_t qa = make_query_labels(q, dot_overflow, 3);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qa;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
    const uint8_t char_overflow[3] = {63, 62, 2};
    size_t qb = make_query_labels(q, char_overflow, 3);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qb;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = sizeof(out);
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
}

void test_dns_question_exceeds_out_cap()
{
    uint8_t q[128], out[256];
    size_t qlen = make_query(q, 1, "foo.lan", 1, PROTO_FALSE);
    DnsServer.msg.query = q;
    DnsServer.msg.qlen = qlen;
    DnsServer.ans.ttl = 60;
    DnsServer.ans.resolve = resolve_foo;
    DnsServer.msg.out = out;
    DnsServer.msg.out_cap = 20;
    DnsServer.build_response(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_UINT(0, DnsServer.n);
}

void test_dns_add_and_lookup_guards()
{
    DnsServer.rec.name = NULL;
    DnsServer.rec.a = 1;
    DnsServer.rec.b = 2;
    DnsServer.rec.c = 3;
    DnsServer.rec.d = 4;
    DnsServer.add(protocore_dns_server_span());
    TEST_ASSERT_FALSE(DnsServer.ok);
    DnsServer.rec.name = "";
    DnsServer.rec.a = 1;
    DnsServer.rec.b = 2;
    DnsServer.rec.c = 3;
    DnsServer.rec.d = 4;
    DnsServer.add(protocore_dns_server_span());
    TEST_ASSERT_FALSE(DnsServer.ok);
    char toolong[PROTOCORE_DNS_NAME_MAX + 4];
    memset(toolong, 'a', sizeof(toolong) - 1);
    toolong[sizeof(toolong) - 1] = '\0';
    DnsServer.rec.name = toolong;
    DnsServer.rec.a = 1;
    DnsServer.rec.b = 2;
    DnsServer.rec.c = 3;
    DnsServer.rec.d = 4;
    DnsServer.add(protocore_dns_server_span());
    TEST_ASSERT_FALSE(DnsServer.ok);

    char nm[16];
    for (int i = 0; i < PROTOCORE_DNS_SERVER_MAX_RECORDS; i++)
    {
        snprintf(nm, sizeof(nm), "h%d.lan", i);
        DnsServer.rec.name = nm;
        DnsServer.rec.a = 10;
        DnsServer.rec.b = 0;
        DnsServer.rec.c = 0;
        DnsServer.rec.d = (uint8_t)i;
        DnsServer.add(protocore_dns_server_span());
        TEST_ASSERT_TRUE(DnsServer.ok);
    }
    DnsServer.rec.name = "overflow.lan";
    DnsServer.rec.a = 10;
    DnsServer.rec.b = 0;
    DnsServer.rec.c = 0;
    DnsServer.rec.d = 99;
    DnsServer.add(protocore_dns_server_span());
    TEST_ASSERT_FALSE(DnsServer.ok);
    DnsServer.rec.name = NULL;
    DnsServer.lookup(protocore_dns_server_span());
    TEST_ASSERT_EQUAL_HEX32(0u, DnsServer.ip);
}

void test_dns_begin_answers_a_query_over_the_wire()
{
    UdpListener.port = 53;
    UdpListener.close(protocore_udp_listener_span());
    (void)UdpListener.ok;
    protocore_net_host_udp_reset();
    DnsServer.clear(protocore_dns_server_span());
    DnsServer.rec.name = "gw.lan";
    DnsServer.rec.a = 192;
    DnsServer.rec.b = 168;
    DnsServer.rec.c = 1;
    DnsServer.rec.d = 1;
    DnsServer.add(protocore_dns_server_span());
    TEST_ASSERT_TRUE(DnsServer.ok);
    DnsServer.begin(protocore_dns_server_span());
    TEST_ASSERT_TRUE(DnsServer.ok);

    uint8_t q[256];
    size_t qn = make_query(q, 0x1234, "gw.lan", 1, PROTO_TRUE);
    protocore_net_host_udp_deliver(53, "192.168.1.50", 40000, q, (uint16_t)qn);
    UdpListener.poll(protocore_udp_listener_span());

    TEST_ASSERT_EQUAL_size_t(1, protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_EQUAL_UINT16(40000, d->dst_port);
    TEST_ASSERT_EQUAL_HEX8(0x12, d->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, d->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x84, d->data[2] & 0xFC);
    TEST_ASSERT_EQUAL_HEX8(0x01, d->data[7]);

    TEST_ASSERT_EQUAL_UINT8(192, d->data[d->len - 4]);
    TEST_ASSERT_EQUAL_UINT8(168, d->data[d->len - 3]);
    TEST_ASSERT_EQUAL_UINT8(1, d->data[d->len - 2]);
    TEST_ASSERT_EQUAL_UINT8(1, d->data[d->len - 1]);

    protocore_net_host_udp_reset();
    qn = make_query(q, 0x5678, "absent.lan", 1, PROTO_TRUE);
    protocore_net_host_udp_deliver(53, "192.168.1.50", 40000, q, (uint16_t)qn);
    UdpListener.poll(protocore_udp_listener_span());
    TEST_ASSERT_EQUAL_size_t(1, protocore_net_host_udp_count());
    TEST_ASSERT_EQUAL_HEX8(0x03, protocore_net_host_udp_at(0)->data[3] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_net_host_udp_at(0)->data[7]);

    UdpListener.port = 53;
    UdpListener.close(protocore_udp_listener_span());
    (void)UdpListener.ok;
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
    RUN_TEST(test_dns_begin_answers_a_query_over_the_wire);
    return UNITY_END();
}
