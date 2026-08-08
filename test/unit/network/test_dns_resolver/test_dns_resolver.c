// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The DNS resolver (network_drivers/network/dns/dns_resolver): the answer classifier and verifier,
// the question it writes and the answer it reads, and the resolve paths that do not block - the
// dotted-quad shortcut, a nameserver that will not parse, and the deadline.
//
// resolve() blocks until an answer or the deadline, so a reply cannot be handed to it from here.
// That is why the two wire halves are API in their own right: they carry the parsing this module
// would otherwise hide behind a socket, and they are what these cases drive.

#include "network_drivers/network/dns/dns_resolver.h"
#include "network_drivers/transport/udp.h"
#include "pc_net_host.h"
#include <string.h>

#include <unity.h>

#define IPV4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void setUp()
{
    pc_net_host_reset();
    set_millis(0);
    Resolver.set_server("9.9.9.9");
}
void tearDown()
{
    Udp.listener->close(PC_DNS_CLIENT_PORT);
}

// --- the classifier ------------------------------------------------------------------------------

void test_classify()
{
    TEST_ASSERT_EQUAL_INT(PC_IP_UNSPECIFIED, Resolver.classify(0u));
    TEST_ASSERT_EQUAL_INT(PC_IP_BROADCAST, Resolver.classify(0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_INT(PC_IP_LOOPBACK, Resolver.classify(IPV4(127, 0, 0, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PRIVATE, Resolver.classify(IPV4(10, 0, 0, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PRIVATE, Resolver.classify(IPV4(172, 16, 0, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PRIVATE, Resolver.classify(IPV4(172, 31, 255, 254)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(172, 15, 0, 1))); // just below the /12
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(172, 32, 0, 1))); // just above it
    TEST_ASSERT_EQUAL_INT(PC_IP_PRIVATE, Resolver.classify(IPV4(192, 168, 1, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_LINKLOCAL, Resolver.classify(IPV4(169, 254, 1, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_MULTICAST, Resolver.classify(IPV4(224, 0, 0, 251)));
    TEST_ASSERT_EQUAL_INT(PC_IP_MULTICAST, Resolver.classify(IPV4(239, 255, 255, 255)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(93, 184, 216, 34)));
}

void test_verify_rejects_suspicious()
{
    TEST_ASSERT_FALSE(Resolver.verify(0u));
    TEST_ASSERT_FALSE(Resolver.verify(0xFFFFFFFFu));
    TEST_ASSERT_FALSE(Resolver.verify(IPV4(127, 0, 0, 1)));
    TEST_ASSERT_FALSE(Resolver.verify(IPV4(224, 0, 0, 1)));
}

void test_verify_accepts_plausible()
{
    TEST_ASSERT_TRUE(Resolver.verify(IPV4(10, 0, 0, 1)));
    TEST_ASSERT_TRUE(Resolver.verify(IPV4(169, 254, 1, 1)));
    TEST_ASSERT_TRUE(Resolver.verify(IPV4(93, 184, 216, 34)));
}

// --- the question --------------------------------------------------------------------------------

void test_query_build_writes_one_a_question()
{
    uint8_t q[128];
    size_t n = pc_dns_query_build(q, sizeof(q), 0xBEEF, "example.com");
    TEST_ASSERT_TRUE(n > 12);

    TEST_ASSERT_EQUAL_UINT8(0xBE, q[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEF, q[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, q[2]); // recursion desired
    TEST_ASSERT_EQUAL_UINT8(0x00, q[3]);
    TEST_ASSERT_EQUAL_UINT8(1, q[5]); // QDCOUNT
    TEST_ASSERT_EQUAL_UINT8(0, q[7]); // ANCOUNT

    const uint8_t want[] = {7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, q + 12, sizeof(want));
    TEST_ASSERT_EQUAL_UINT8(1, q[n - 3]); // QTYPE A
    TEST_ASSERT_EQUAL_UINT8(1, q[n - 1]); // QCLASS IN
    TEST_ASSERT_EQUAL_UINT(12 + sizeof(want) + 4, n);
}

void test_query_build_guards()
{
    uint8_t q[128];
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_query_build(NULL, sizeof(q), 1, "a.com"));
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_query_build(q, sizeof(q), 1, NULL));
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_query_build(q, 8, 1, "a.com"));  // no room for a header
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_query_build(q, 16, 1, "a.com")); // header fits, the name does not
}

// --- the answer ----------------------------------------------------------------------------------

// Build a response: header, the echoed question, then the answers the caller stages.
static size_t put_name(uint8_t *o, size_t n, const char *dotted)
{
    size_t i = 0;
    while (dotted[i] != '\0')
    {
        size_t s = i;
        while (dotted[i] != '\0' && dotted[i] != '.')
        {
            i++;
        }
        o[n] = (uint8_t)(i - s);
        n++;
        for (size_t k = s; k < i; k++)
        {
            o[n] = (uint8_t)dotted[k];
            n++;
        }
        if (dotted[i] == '.')
        {
            i++;
        }
    }
    o[n] = 0;
    n++;
    return n;
}

static size_t rr(uint8_t *o, size_t n, uint16_t type, uint16_t cls, const uint8_t *rd, uint16_t rdlen)
{
    o[n] = 0xC0; // owner: a pointer back to the question, which is what a real server writes
    n++;
    o[n] = 0x0C;
    n++;
    o[n] = (uint8_t)(type >> 8);
    n++;
    o[n] = (uint8_t)type;
    n++;
    o[n] = (uint8_t)(cls >> 8);
    n++;
    o[n] = (uint8_t)cls;
    n++;
    o[n] = 0;
    n++;
    o[n] = 0;
    n++;
    o[n] = 0;
    n++;
    o[n] = 60;
    n++; // TTL
    o[n] = (uint8_t)(rdlen >> 8);
    n++;
    o[n] = (uint8_t)rdlen;
    n++;
    memcpy(o + n, rd, rdlen);
    return n + rdlen;
}

static size_t response_head(uint8_t *o, uint16_t id, uint16_t flags, uint16_t an)
{
    memset(o, 0, 12);
    o[0] = (uint8_t)(id >> 8);
    o[1] = (uint8_t)id;
    o[2] = (uint8_t)(flags >> 8);
    o[3] = (uint8_t)flags;
    o[5] = 1; // QDCOUNT
    o[6] = (uint8_t)(an >> 8);
    o[7] = (uint8_t)an;
    size_t n = put_name(o, 12, "example.com");
    o[n] = 0;
    n++;
    o[n] = 1;
    n++;
    o[n] = 0;
    n++;
    o[n] = 1;
    n++;
    return n;
}

void test_answer_parse_takes_the_a_record()
{
    uint8_t p[256];
    size_t n = response_head(p, 0x1234, 0x8180, 1);
    const uint8_t addr[4] = {93, 184, 216, 34};
    n = rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 0;
    TEST_ASSERT_TRUE(pc_dns_answer_parse(p, n, 0x1234, &ip));
    TEST_ASSERT_EQUAL_UINT32(IPV4(93, 184, 216, 34), ip);
}

// A name that is an alias answers with the CNAME first and the address after it, so the walk cannot
// stop at the first record.
void test_answer_parse_walks_past_a_cname()
{
    uint8_t p[256];
    size_t n = response_head(p, 0x1234, 0x8180, 2);
    uint8_t cname[32];
    size_t cl = put_name(cname, 0, "cdn.example.net");
    n = rr(p, n, 5, 1, cname, (uint16_t)cl); // CNAME
    const uint8_t addr[4] = {10, 1, 2, 3};
    n = rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 0;
    TEST_ASSERT_TRUE(pc_dns_answer_parse(p, n, 0x1234, &ip));
    TEST_ASSERT_EQUAL_UINT32(IPV4(10, 1, 2, 3), ip);
}

void test_answer_parse_refuses_a_foreign_id()
{
    uint8_t p[256];
    size_t n = response_head(p, 0x1234, 0x8180, 1);
    const uint8_t addr[4] = {1, 2, 3, 4};
    n = rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 0;
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n, 0x9999, &ip)); // answers a question nobody asked
}

void test_answer_parse_refuses_a_question_and_an_rcode()
{
    uint8_t p[256];
    const uint8_t addr[4] = {1, 2, 3, 4};
    uint32_t ip = 0;

    size_t n = response_head(p, 0x1234, 0x0100, 1); // QR clear: this is a query
    n = rr(p, n, 1, 1, addr, 4);
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n, 0x1234, &ip));

    n = response_head(p, 0x1234, 0x8183, 1); // NXDOMAIN
    n = rr(p, n, 1, 1, addr, 4);
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n, 0x1234, &ip));
}

void test_answer_parse_refuses_when_no_a_record_is_present()
{
    uint8_t p[256];
    uint32_t ip = 0;

    size_t n = response_head(p, 0x1234, 0x8180, 0); // ANCOUNT 0
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n, 0x1234, &ip));

    n = response_head(p, 0x1234, 0x8180, 1);
    uint8_t cname[32];
    size_t cl = put_name(cname, 0, "cdn.example.net");
    n = rr(p, n, 5, 1, cname, (uint16_t)cl); // a CNAME and nothing else
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n, 0x1234, &ip));

    const uint8_t addr[4] = {1, 2, 3, 4};
    n = response_head(p, 0x1234, 0x8180, 1);
    n = rr(p, n, 1, 3, addr, 4); // an A record in the wrong class
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n, 0x1234, &ip));
}

void test_answer_parse_refuses_a_truncated_record()
{
    uint8_t p[256];
    size_t n = response_head(p, 0x1234, 0x8180, 1);
    const uint8_t addr[4] = {1, 2, 3, 4};
    n = rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 0;
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n - 2, 0x1234, &ip)); // rdata runs off the end
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, 8, 0x1234, &ip));     // shorter than a header
    TEST_ASSERT_FALSE(pc_dns_answer_parse(NULL, n, 0x1234, &ip));
    TEST_ASSERT_FALSE(pc_dns_answer_parse(p, n, 0x1234, NULL));
}

// --- resolve -------------------------------------------------------------------------------------

// A dotted quad is its own answer, so no port is bound and nothing is asked.
void test_resolve_literal_skips_dns()
{
    uint32_t ip = 0;
    TEST_ASSERT_TRUE(Resolver.resolve("192.168.4.7", &ip));
    TEST_ASSERT_EQUAL_UINT32(IPV4(192, 168, 4, 7), ip);
    TEST_ASSERT_EQUAL_INT(0, (int)pc_net_host_udp_sent());
    TEST_ASSERT_NULL(pc_net_host_udp_pcb(PC_DNS_CLIENT_PORT));
}

void test_resolve_refuses_null_arguments()
{
    uint32_t ip = 0;
    TEST_ASSERT_FALSE(Resolver.resolve(NULL, &ip));
    TEST_ASSERT_FALSE(Resolver.resolve("example.com", NULL));
}

// A name puts a query on the wire, addressed to the server that was set.
void test_resolve_asks_the_configured_server()
{
    TEST_ASSERT_TRUE(Resolver.set_server("192.168.1.1"));
    uint32_t ip = 0;
    TEST_ASSERT_FALSE(Resolver.resolve("example.com", &ip)); // nothing answers, so it times out

    TEST_ASSERT_TRUE(pc_net_host_udp_count() > 0);
    const pc_net_host_dgram *d = pc_net_host_udp_at(0);
    TEST_ASSERT_EQUAL_UINT16(53, d->dst_port);
    TEST_ASSERT_EQUAL_UINT16(PC_DNS_CLIENT_PORT, d->src_port);
    TEST_ASSERT_EQUAL_UINT8(192, d->addr[0]);
    TEST_ASSERT_EQUAL_UINT8(1, d->addr[3]);
    TEST_ASSERT_EQUAL_UINT8(1, d->data[5]); // one question
}

// The deadline is what ends a resolve nothing answers.
void test_resolve_gives_up_at_the_deadline()
{
    uint32_t before = millis();
    uint32_t ip = 0;
    TEST_ASSERT_FALSE(Resolver.resolve("example.com", &ip));
    TEST_ASSERT_TRUE((millis() - before) >= PC_DNS_TIMEOUT_MS);
}

void test_set_server_refuses_what_does_not_parse()
{
    TEST_ASSERT_FALSE(Resolver.set_server(NULL));
    TEST_ASSERT_FALSE(Resolver.set_server("not-an-address"));
    TEST_ASSERT_FALSE(Resolver.set_server("999.1.1.1"));
    // The one that was already set stands, so the resolver still asks somebody.
    TEST_ASSERT_TRUE(Resolver.set_server("8.8.4.4"));
}

// resolve_verified refuses an answer the classifier calls implausible, and the literal path is the
// one that reaches it without a server.
void test_resolve_verified_rejects_a_rebinding_answer()
{
    uint32_t ip = 0;
    TEST_ASSERT_FALSE(Resolver.resolve_verified("127.0.0.1", &ip)); // loopback
    TEST_ASSERT_FALSE(Resolver.resolve_verified("224.0.0.251", &ip));
    TEST_ASSERT_TRUE(Resolver.resolve_verified("93.184.216.34", &ip));
    TEST_ASSERT_EQUAL_UINT32(IPV4(93, 184, 216, 34), ip);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_classify);
    RUN_TEST(test_verify_rejects_suspicious);
    RUN_TEST(test_verify_accepts_plausible);
    RUN_TEST(test_query_build_writes_one_a_question);
    RUN_TEST(test_query_build_guards);
    RUN_TEST(test_answer_parse_takes_the_a_record);
    RUN_TEST(test_answer_parse_walks_past_a_cname);
    RUN_TEST(test_answer_parse_refuses_a_foreign_id);
    RUN_TEST(test_answer_parse_refuses_a_question_and_an_rcode);
    RUN_TEST(test_answer_parse_refuses_when_no_a_record_is_present);
    RUN_TEST(test_answer_parse_refuses_a_truncated_record);
    RUN_TEST(test_resolve_literal_skips_dns);
    RUN_TEST(test_resolve_refuses_null_arguments);
    RUN_TEST(test_resolve_asks_the_configured_server);
    RUN_TEST(test_resolve_gives_up_at_the_deadline);
    RUN_TEST(test_set_server_refuses_what_does_not_parse);
    RUN_TEST(test_resolve_verified_rejects_a_rebinding_answer);
    return UNITY_END();
}
