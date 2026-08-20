// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DNS resolver (network_drivers/network/dns/dns_resolver.h).
//
// RFC 1035 governs the wire: sec 4.1.1 the header, sec 4.1.2 the question, sec 4.1.3 a resource
// record, sec 4.1.4 the name compression pointer, sec 3.4.1 the four-octet A RDATA, sec 4.2.1 the
// server port. RFC 5452 sec 9.1 makes the ID one of the attributes a response has to match before
// its data is used. The answer classifier is judged against the IPv4 Special-Purpose Address
// Registry (RFC 6890 sec 2.2.2) and the host group range (RFC 1112 sec 4).
//
// test_rfc1035_published_qname is the load-bearing case: sec 4.1.4 prints the octets of F.ISI.ARPA
// on the wire, so the builder's QNAME is checked against a name the standard itself lays out rather
// than against this encoder's own idea of one.

#include "network_drivers/network/dns/dns_resolver/dns_resolver.h"
#include "network_drivers/transport/udp/server/server.h"
#include "protocore_net_host.h"
#include <string.h>

#include <unity.h>

#define IPV4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void setUp(void)
{
    protocore_net_host_reset();
    set_millis(0);
    ResolverV.server.ip = "9.9.9.9";
    Resolver.set_server(protocore_dns_resolver_span());
}

// The module holds one query across calls, so a case that left one in flight releases it here by
// walking past its deadline; otherwise the next case inherits it.
void tearDown(void)
{
    Resolver.busy(protocore_dns_resolver_span());
    if (ResolverV.ok)
    {
        set_millis(millis() + PROTOCORE_DNS_TIMEOUT_MS);
        ResolverV.query.host = "release.example";
        Resolver.resolve(protocore_dns_resolver_span());
    }
    UdpListenerV.port = PROTOCORE_DNS_CLIENT_PORT;
    UdpListener.close(protocore_udp_listener_span());
}

// --- the calls, each as one helper -----------------------------------------------------------

static size_t query_build(uint8_t *out, size_t cap, uint16_t id, const char *host)
{
    ResolverV.query.host = host;
    ResolverV.query.id = id;
    ResolverV.query.out = out;
    ResolverV.query.cap = cap;
    Resolver.query_build(protocore_dns_resolver_span());
    return ResolverV.n;
}

static proto_bool answer_parse(const uint8_t *pkt, size_t len, uint16_t id, uint32_t *out)
{
    ResolverV.query.id = id;
    ResolverV.answer.pkt = pkt;
    ResolverV.answer.len = len;
    Resolver.answer_parse(protocore_dns_resolver_span());
    *out = ResolverV.u32;
    return ResolverV.ok;
}

static protocore_ip_class classify(uint32_t ip)
{
    ResolverV.addr.ip = ip;
    Resolver.classify(protocore_dns_resolver_span());
    return ResolverV.cls;
}

static proto_bool verify(uint32_t ip)
{
    ResolverV.addr.ip = ip;
    Resolver.verify(protocore_dns_resolver_span());
    return ResolverV.ok;
}

static protocore_dns_state resolve(const char *host)
{
    ResolverV.query.host = host;
    Resolver.resolve(protocore_dns_resolver_span());
    return ResolverV.state;
}

static protocore_dns_state resolve_verified(const char *host)
{
    ResolverV.query.host = host;
    Resolver.resolve_verified(protocore_dns_resolver_span());
    return ResolverV.state;
}

static proto_bool busy(void)
{
    Resolver.busy(protocore_dns_resolver_span());
    return ResolverV.ok;
}

static proto_bool set_server(const char *ip)
{
    ResolverV.server.ip = ip;
    Resolver.set_server(protocore_dns_resolver_span());
    return ResolverV.ok;
}

// --- a response, assembled the way a server writes one ---------------------------------------

// RFC 1035 sec 4.1.2: a QNAME is a sequence of length-prefixed labels ending in a zero octet.
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
        o[n++] = (uint8_t)(i - s);
        for (size_t k = s; k < i; k++)
        {
            o[n++] = (uint8_t)dotted[k];
        }
        if (dotted[i] == '.')
        {
            i++;
        }
    }
    o[n++] = 0;
    return n;
}

// RFC 1035 sec 4.1.3: NAME, TYPE, CLASS, TTL, RDLENGTH, RDATA. The owner is written as the sec
// 4.1.4 pointer back to the question at offset 12, which is what a real server emits.
static size_t put_rr(uint8_t *o, size_t n, uint16_t type, uint16_t cls, const uint8_t *rd, uint16_t rdlen)
{
    o[n++] = 0xC0;
    o[n++] = 0x0C;
    o[n++] = (uint8_t)(type >> 8);
    o[n++] = (uint8_t)type;
    o[n++] = (uint8_t)(cls >> 8);
    o[n++] = (uint8_t)cls;
    o[n++] = 0;
    o[n++] = 0;
    o[n++] = 0;
    o[n++] = 60; // TTL
    o[n++] = (uint8_t)(rdlen >> 8);
    o[n++] = (uint8_t)rdlen;
    memcpy(o + n, rd, rdlen);
    return n + rdlen;
}

// RFC 1035 sec 4.1.1 header, then the echoed question (sec 4.1.2), QTYPE A, QCLASS IN.
static size_t put_response_head(uint8_t *o, uint16_t id, uint16_t flags, uint16_t ancount)
{
    memset(o, 0, 12);
    o[0] = (uint8_t)(id >> 8);
    o[1] = (uint8_t)id;
    o[2] = (uint8_t)(flags >> 8);
    o[3] = (uint8_t)flags;
    o[5] = 1;
    o[6] = (uint8_t)(ancount >> 8);
    o[7] = (uint8_t)ancount;
    size_t n = put_name(o, 12, "example.com");
    o[n++] = 0;
    o[n++] = 1; // QTYPE A
    o[n++] = 0;
    o[n++] = 1; // QCLASS IN
    return n;
}

// --- the question ------------------------------------------------------------------------------

// RFC 1035 sec 4.1.4 shows the domain name F.ISI.ARPA on the wire as
//     1 'F'  3 'I' 'S' 'I'  4 'A' 'R' 'P' 'A'  0
// and sec 4.1.1 fixes the header: the ID the asker chose, RD set, QDCOUNT 1 and nothing else
// counted. sec 4.1.2 puts QTYPE and QCLASS after the name, and A / IN are 1 and 1
// (sec 3.2.2, sec 3.2.4).
void test_rfc1035_published_qname(void)
{
    uint8_t q[128];
    size_t n = query_build(q, sizeof(q), 0xBEEF, "F.ISI.ARPA");

    static const uint8_t QNAME[12] = {1, 'F', 3, 'I', 'S', 'I', 4, 'A', 'R', 'P', 'A', 0};
    TEST_ASSERT_EQUAL_size_t(12u + sizeof(QNAME) + 4u, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(QNAME, q + 12, sizeof(QNAME));

    TEST_ASSERT_EQUAL_HEX8(0xBE, q[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, q[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, q[2]); // RD, and QR/Opcode/AA/TC clear
    TEST_ASSERT_EQUAL_HEX8(0x00, q[3]); // RA, Z and RCODE clear
    TEST_ASSERT_EQUAL_HEX8(0x00, q[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, q[5]); // QDCOUNT 1
    TEST_ASSERT_EQUAL_HEX8(0x00, q[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, q[7]); // ANCOUNT 0
    TEST_ASSERT_EQUAL_HEX8(0x00, q[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00, q[9]); // NSCOUNT 0
    TEST_ASSERT_EQUAL_HEX8(0x00, q[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, q[11]); // ARCOUNT 0

    TEST_ASSERT_EQUAL_HEX8(0x00, q[n - 4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, q[n - 3]); // QTYPE A
    TEST_ASSERT_EQUAL_HEX8(0x00, q[n - 2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, q[n - 1]); // QCLASS IN
}

// A question that does not fit is not half-written: the caller gets nothing to send.
void test_query_build_refuses_what_does_not_fit(void)
{
    uint8_t q[128];
    TEST_ASSERT_EQUAL_size_t(0u, query_build(NULL, sizeof(q), 1, "a.com"));
    TEST_ASSERT_EQUAL_size_t(0u, query_build(q, sizeof(q), 1, NULL));
    TEST_ASSERT_EQUAL_size_t(0u, query_build(q, 8, 1, "a.com"));  // shorter than the header
    TEST_ASSERT_EQUAL_size_t(0u, query_build(q, 16, 1, "a.com")); // header fits, the name does not
    TEST_ASSERT_EQUAL_size_t(0u, query_build(q, 19, 1, "a.com")); // name fits, QTYPE + QCLASS do not
}

// --- the answer --------------------------------------------------------------------------------

// RFC 1035 sec 3.4.1: "ADDRESS  A 32 bit Internet address", carried in a four-octet RDATA.
void test_answer_parse_reads_the_a_record(void)
{
    uint8_t p[256];
    size_t n = put_response_head(p, 0x1234, 0x8180, 1);
    const uint8_t addr[4] = {192, 0, 2, 42}; // RFC 5737 documentation block
    n = put_rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 0;
    TEST_ASSERT_TRUE(answer_parse(p, n, 0x1234, &ip));
    TEST_ASSERT_EQUAL_HEX32(IPV4(192, 0, 2, 42), ip);
}

// An alias answers with the CNAME first and the address behind it, so the first record is not the
// one to read.
void test_answer_parse_walks_past_other_types(void)
{
    uint8_t p[256];
    size_t n = put_response_head(p, 0x1234, 0x8180, 2);
    uint8_t cname[32];
    size_t cl = put_name(cname, 0, "cdn.example.net");
    n = put_rr(p, n, 5, 1, cname, (uint16_t)cl); // TYPE CNAME
    const uint8_t addr[4] = {192, 0, 2, 7};
    n = put_rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 0;
    TEST_ASSERT_TRUE(answer_parse(p, n, 0x1234, &ip));
    TEST_ASSERT_EQUAL_HEX32(IPV4(192, 0, 2, 7), ip);
}

// RFC 5452 sec 9.1 names the ID among the attributes a response must match; a message answering a
// question this resolver did not ask is not its answer.
void test_answer_parse_requires_the_id_to_match(void)
{
    uint8_t p[256];
    size_t n = put_response_head(p, 0x1234, 0x8180, 1);
    const uint8_t addr[4] = {192, 0, 2, 1};
    n = put_rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 1;
    TEST_ASSERT_FALSE(answer_parse(p, n, 0x9999, &ip));
    TEST_ASSERT_EQUAL_HEX32(0u, ip); // and no address is reported
    TEST_ASSERT_TRUE(answer_parse(p, n, 0x1234, &ip));
}

// RFC 1035 sec 4.1.1: QR "specifies whether this message is a query (0), or a response (1)", and
// RCODE 0 is "No error condition"; 3 is "Name Error".
void test_answer_parse_requires_a_response_with_rcode_zero(void)
{
    uint8_t p[256];
    const uint8_t addr[4] = {192, 0, 2, 1};
    uint32_t ip = 0;

    size_t n = put_response_head(p, 0x1234, 0x0100, 1); // QR clear: still a query
    n = put_rr(p, n, 1, 1, addr, 4);
    TEST_ASSERT_FALSE(answer_parse(p, n, 0x1234, &ip));

    n = put_response_head(p, 0x1234, 0x8183, 1); // RCODE 3, Name Error
    n = put_rr(p, n, 1, 1, addr, 4);
    TEST_ASSERT_FALSE(answer_parse(p, n, 0x1234, &ip));
}

// The address has to come from a record that is TYPE A, CLASS IN and four octets long; nothing else
// carries one.
void test_answer_parse_requires_an_a_record_in_class_in(void)
{
    uint8_t p[256];
    uint32_t ip = 0;

    size_t n = put_response_head(p, 0x1234, 0x8180, 0); // ANCOUNT 0
    TEST_ASSERT_FALSE(answer_parse(p, n, 0x1234, &ip));

    uint8_t cname[32];
    size_t cl = put_name(cname, 0, "cdn.example.net");
    n = put_response_head(p, 0x1234, 0x8180, 1);
    n = put_rr(p, n, 5, 1, cname, (uint16_t)cl); // a CNAME and nothing else
    TEST_ASSERT_FALSE(answer_parse(p, n, 0x1234, &ip));

    const uint8_t addr[4] = {192, 0, 2, 1};
    n = put_response_head(p, 0x1234, 0x8180, 1);
    n = put_rr(p, n, 1, 3, addr, 4); // CLASS CH, not IN
    TEST_ASSERT_FALSE(answer_parse(p, n, 0x1234, &ip));

    const uint8_t wide[16] = {0};
    n = put_response_head(p, 0x1234, 0x8180, 1);
    n = put_rr(p, n, 1, 1, wide, 16); // TYPE A with a AAAA-sized RDATA
    TEST_ASSERT_FALSE(answer_parse(p, n, 0x1234, &ip));
}

// A record whose RDATA runs off the end of the datagram is not read past the buffer.
void test_answer_parse_refuses_a_truncated_message(void)
{
    uint8_t p[256];
    size_t n = put_response_head(p, 0x1234, 0x8180, 1);
    const uint8_t addr[4] = {192, 0, 2, 1};
    n = put_rr(p, n, 1, 1, addr, 4);

    uint32_t ip = 0;
    TEST_ASSERT_FALSE(answer_parse(p, n - 2, 0x1234, &ip));
    TEST_ASSERT_FALSE(answer_parse(p, 8, 0x1234, &ip)); // shorter than the header
    TEST_ASSERT_FALSE(answer_parse(NULL, n, 0x1234, &ip));
}

// --- the classifier ----------------------------------------------------------------------------

// RFC 6890 sec 2.2.2 registry entries, and RFC 1112 sec 4: "host group addresses range from
// 224.0.0.0 to 239.255.255.255".
void test_classify_matches_the_registry(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_UNSPECIFIED, classify(0u));                     // 0.0.0.0/8
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_BROADCAST, classify(0xFFFFFFFFu));              // 255.255.255.255/32
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_LOOPBACK, classify(IPV4(127, 0, 0, 1)));        // 127.0.0.0/8
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PRIVATE, classify(IPV4(10, 0, 0, 1)));          // 10.0.0.0/8
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PRIVATE, classify(IPV4(172, 16, 0, 0)));        // 172.16.0.0/12, first
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PRIVATE, classify(IPV4(172, 31, 255, 255)));    // 172.16.0.0/12, last
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PUBLIC, classify(IPV4(172, 15, 255, 255)));     // one below the /12
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PUBLIC, classify(IPV4(172, 32, 0, 0)));         // one above it
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PRIVATE, classify(IPV4(192, 168, 0, 1)));       // 192.168.0.0/16
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_LINKLOCAL, classify(IPV4(169, 254, 1, 1)));     // 169.254.0.0/16
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_MULTICAST, classify(IPV4(224, 0, 0, 0)));       // host group, first
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_MULTICAST, classify(IPV4(239, 255, 255, 255))); // host group, last
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PUBLIC, classify(IPV4(223, 255, 255, 255)));    // one below the range
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_PUBLIC, classify(IPV4(240, 0, 0, 1)));          // one above it
}

// An A record naming this host, naming nothing, or naming a group is not an answer about a remote
// name; the private and link-local blocks are, because a name may legitimately point into them.
void test_verify_refuses_what_cannot_be_a_remote_host(void)
{
    TEST_ASSERT_FALSE(verify(0u));
    TEST_ASSERT_FALSE(verify(0xFFFFFFFFu));
    TEST_ASSERT_FALSE(verify(IPV4(127, 0, 0, 1)));
    TEST_ASSERT_FALSE(verify(IPV4(224, 0, 0, 251)));
    TEST_ASSERT_TRUE(verify(IPV4(10, 0, 0, 1)));
    TEST_ASSERT_TRUE(verify(IPV4(169, 254, 1, 1)));
    TEST_ASSERT_TRUE(verify(IPV4(192, 0, 2, 42)));
}

// --- the resolve -------------------------------------------------------------------------------

// A dotted quad is already an address, so nothing is asked and no port is bound.
void test_a_literal_answers_itself(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_READY, resolve("192.168.4.7"));
    TEST_ASSERT_EQUAL_HEX32(IPV4(192, 168, 4, 7), ResolverV.u32);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
    TEST_ASSERT_NULL(protocore_net_host_udp_pcb(PROTOCORE_DNS_CLIENT_PORT));
    TEST_ASSERT_FALSE(busy());
}

void test_resolve_refuses_a_null_host(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_FAILED, resolve(NULL));
}

// RFC 1035 sec 4.2.1: the query goes to port 53 at the configured server, and it is the question
// this module builds.
void test_a_name_puts_one_question_on_the_wire(void)
{
    TEST_ASSERT_TRUE(set_server("192.0.2.53"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("example.com"));
    TEST_ASSERT_TRUE(busy());

    TEST_ASSERT_EQUAL_size_t(1u, protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_EQUAL_UINT16(53, d->dst_port);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_DNS_CLIENT_PORT, d->src_port);
    TEST_ASSERT_EQUAL_UINT8(192, d->addr[0]);
    TEST_ASSERT_EQUAL_UINT8(0, d->addr[1]);
    TEST_ASSERT_EQUAL_UINT8(2, d->addr[2]);
    TEST_ASSERT_EQUAL_UINT8(53, d->addr[3]);
    TEST_ASSERT_EQUAL_HEX8(0x01, d->data[2]); // RD set
    TEST_ASSERT_EQUAL_HEX8(0x01, d->data[5]); // one question

    // A second name asked while that query is out does not put a second question on the wire.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("elsewhere.example"));
    TEST_ASSERT_EQUAL_size_t(1u, protocore_net_host_udp_count());
}

// The answer arrives on the listener's own drain, and the next ask reports it.
void test_the_answer_completes_the_resolve(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("example.com"));
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    const uint16_t id = (uint16_t)(((uint16_t)d->data[0] << 8) | d->data[1]);

    uint8_t p[256];
    size_t n = put_response_head(p, id, 0x8180, 1);
    const uint8_t addr[4] = {192, 0, 2, 42};
    n = put_rr(p, n, 1, 1, addr, 4);
    TEST_ASSERT_EQUAL_INT(1, protocore_net_host_udp_deliver(PROTOCORE_DNS_CLIENT_PORT, "9.9.9.9", 53, p, (uint16_t)n));
    UdpListenerV.port = PROTOCORE_DNS_CLIENT_PORT;
    UdpListener.poll(protocore_udp_listener_span());

    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_READY, resolve("example.com"));
    TEST_ASSERT_EQUAL_HEX32(IPV4(192, 0, 2, 42), ResolverV.u32);
    TEST_ASSERT_FALSE(busy());
}

// A response carrying somebody else's ID does not end the query in flight; it keeps waiting.
void test_a_foreign_response_does_not_end_the_query(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("example.com"));
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    const uint16_t id = (uint16_t)(((uint16_t)d->data[0] << 8) | d->data[1]);

    uint8_t p[256];
    size_t n = put_response_head(p, (uint16_t)(id ^ 0xFFFFu), 0x8180, 1);
    const uint8_t addr[4] = {203, 0, 113, 9};
    n = put_rr(p, n, 1, 1, addr, 4);
    (void)protocore_net_host_udp_deliver(PROTOCORE_DNS_CLIENT_PORT, "203.0.113.9", 53, p, (uint16_t)n);
    UdpListenerV.port = PROTOCORE_DNS_CLIENT_PORT;
    UdpListener.poll(protocore_udp_listener_span());

    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("example.com"));
    TEST_ASSERT_TRUE(busy());
}

// Nothing answers, so the query ends at its own deadline and the module is free again.
void test_the_query_ends_at_its_deadline(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("example.com"));
    set_millis(millis() + PROTOCORE_DNS_TIMEOUT_MS - 1u);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("example.com")); // one millisecond short
    set_millis(millis() + 1u);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_FAILED, resolve("example.com"));
    TEST_ASSERT_FALSE(busy());
}

// A nameserver is an address literal, and one that is not leaves the previous server standing.
void test_set_server_takes_only_an_address(void)
{
    TEST_ASSERT_FALSE(set_server(NULL));
    TEST_ASSERT_FALSE(set_server("not-an-address"));
    TEST_ASSERT_FALSE(set_server("999.1.1.1"));
    TEST_ASSERT_TRUE(set_server("192.0.2.53"));

    TEST_ASSERT_FALSE(set_server("256.0.0.1")); // refused, so the last good one is still asked
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_BUSY, resolve("example.com"));
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_EQUAL_UINT8(192, d->addr[0]);
    TEST_ASSERT_EQUAL_UINT8(53, d->addr[3]);
}

// The verified resolve applies the classifier to whatever came back, so an answer that points at
// this host or at a group is refused rather than reported.
void test_resolve_verified_refuses_an_implausible_answer(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_FAILED, resolve_verified("127.0.0.1"));
    TEST_ASSERT_EQUAL_HEX32(0u, ResolverV.u32);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_FAILED, resolve_verified("224.0.0.251"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DNS_READY, resolve_verified("192.0.2.42"));
    TEST_ASSERT_EQUAL_HEX32(IPV4(192, 0, 2, 42), ResolverV.u32);
}
