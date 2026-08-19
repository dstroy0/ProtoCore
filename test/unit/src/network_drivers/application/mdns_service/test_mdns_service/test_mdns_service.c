// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the portable mDNS / DNS-SD responder
// (network_drivers/application/mdns_service/mdns_service.h).
//
// RFC 6762 governs the transport and the header bits: sec 3 puts mDNS on 224.0.0.251 UDP 5353,
// sec 18.2 and 18.4 require QR = 1 and AA = 1 in a response, sec 18.1 requires the Query Identifier
// zero, sec 6 forbids questions in a response and forbids answering a name this host does not own.
// RFC 6763 governs the record set: sec 4.1 the <Instance>.<Service>.<Domain> name, sec 9 the
// _services._dns-sd._udp enumeration, sec 6.1 that a TXT record is never zero-length. RR type
// numbers (A 1, PTR 12, TXT 16, SRV 33, ANY 255) are IANA DNS RR TYPE registry assignments.
//
// test_a_response_carries_the_rfc6762_header_bits is the load-bearing case. Those four header bits
// are what makes a datagram a Multicast DNS response rather than a query, and a responder that gets
// them wrong is either ignored by every querier or feeds its own answers back into itself. Every
// record-content assertion below would still pass with the header wrong.
//
// The queries here are built and the responses decoded by this file's own label codec, so nothing
// under test is its own oracle.

#include "network_drivers/application/mdns_service/mdns_service.h"
#include "network_drivers/transport/udp/server/server.h"
#include "protocore_net_host.h"
#include <string.h>

#include <unity.h>

#define MDNS_PORT 5353

// IANA DNS RR TYPE registry assignments.
#define T_A 1
#define T_PTR 12
#define T_TXT 16
#define T_SRV 33
#define T_ANY 255

void setUp(void)
{
    protocore_net_host_reset();
}

void tearDown(void)
{
    UdpListener.port = MDNS_PORT;
    UdpListener.close(protocore_udp_listener_span());
}

// --- this file's own label codec, independent of the one under test -----------------------------

// Write "a.b.c" as length-prefixed labels ending in a root label (RFC 1035 sec 3.1).
static size_t put_name(uint8_t *o, size_t n, const char *dotted)
{
    size_t i = 0;
    while (dotted[i] != '\0')
    {
        const size_t s = i;
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

// Read the name at @p off into @p out, following compression pointers (RFC 1035 sec 4.1.4). Returns
// the offset just past the name as it appeared in the message.
static size_t get_name(const uint8_t *p, size_t off, char *out)
{
    size_t n = 0;
    size_t after = 0;
    for (;;)
    {
        const uint8_t b = p[off];
        if ((b & 0xC0u) == 0xC0u)
        {
            if (after == 0)
            {
                after = off + 2;
            }
            off = (size_t)(((b & 0x3Fu) << 8) | p[off + 1]);
            continue;
        }
        off++;
        if (b == 0)
        {
            if (after == 0)
            {
                after = off;
            }
            break;
        }
        if (n != 0)
        {
            out[n++] = '.';
        }
        for (uint8_t k = 0; k < b; k++)
        {
            out[n++] = (char)p[off++];
        }
    }
    out[n] = '\0';
    return after;
}

// A one-question query: header, QNAME, QTYPE, QCLASS IN.
static size_t make_query(uint8_t *o, const char *name, uint16_t qtype)
{
    memset(o, 0, 12);
    o[5] = 1; // QDCOUNT
    size_t n = put_name(o, 12, name);
    o[n++] = (uint8_t)(qtype >> 8);
    o[n++] = (uint8_t)qtype;
    o[n++] = 0;
    o[n++] = 1; // CLASS IN
    return n;
}

// Put one datagram on the group and let the listener carry any answer to the wire.
static void deliver(const void *p, uint16_t n)
{
    protocore_net_host_udp_reset();
    TEST_ASSERT_TRUE(protocore_net_host_udp_deliver(MDNS_PORT, "192.0.2.7", MDNS_PORT, (void *)p, n));
    UdpListener.poll(protocore_udp_listener_span());
}

static void ask(const char *name, uint16_t qtype)
{
    uint8_t q[256];
    const size_t n = make_query(q, name, qtype);
    deliver(q, (uint16_t)n);
}

// One answer record, walked out of the response.
typedef struct
{
    char owner[256];
    uint16_t type;
    uint16_t cls;
    uint32_t ttl;
    const uint8_t *rdata;
    uint16_t rdlen;
} Rec;

static uint16_t answer_count(const protocore_net_host_dgram *d)
{
    return (uint16_t)(((uint16_t)d->data[6] << 8) | d->data[7]);
}

// Decode answer @p want of the captured response; false when there is no such record.
static proto_bool answer_at(const protocore_net_host_dgram *d, int want, Rec *r)
{
    if (want >= (int)answer_count(d))
    {
        return PROTO_FALSE;
    }
    size_t off = 12;
    for (int i = 0;; i++)
    {
        off = get_name(d->data, off, r->owner);
        r->type = (uint16_t)(((uint16_t)d->data[off] << 8) | d->data[off + 1]);
        r->cls = (uint16_t)(((uint16_t)d->data[off + 2] << 8) | d->data[off + 3]);
        r->ttl = ((uint32_t)d->data[off + 4] << 24) | ((uint32_t)d->data[off + 5] << 16) |
                 ((uint32_t)d->data[off + 6] << 8) | (uint32_t)d->data[off + 7];
        r->rdlen = (uint16_t)(((uint16_t)d->data[off + 8] << 8) | d->data[off + 9]);
        r->rdata = &d->data[off + 10];
        if (i == want)
        {
            return PROTO_TRUE;
        }
        off += (size_t)10 + r->rdlen;
    }
}

// The one datagram the responder sent.
static const protocore_net_host_dgram *response(void)
{
    TEST_ASSERT_EQUAL_size_t(1u, protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_NOT_NULL(d);
    return d;
}

// --- tests --------------------------------------------------------------------------------------

// RFC 6762 sec 3: mDNS runs on the IPv4 link-local multicast address 224.0.0.251, UDP port 5353.
void test_begin_joins_the_rfc6762_group(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    TEST_ASSERT_NOT_NULL(protocore_net_host_udp_pcb(MDNS_PORT));

    UdpListener.port = MDNS_PORT;
    UdpListener.joined_group(protocore_udp_listener_span());
    TEST_ASSERT_EQUAL_STRING("224.0.0.251", UdpListener.text);
}

// RFC 6762 sec 18: ID zero (18.1), QR one (18.2), AA one (18.4), and no questions in the Question
// Section (sec 6). The answer goes to the group, not back to the asker (sec 6).
void test_a_response_carries_the_rfc6762_header_bits(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    ask("_http._tcp.local", T_PTR);

    const protocore_net_host_dgram *d = response();
    TEST_ASSERT_EQUAL_UINT8(0, d->data[0]); // Query Identifier, both octets
    TEST_ASSERT_EQUAL_UINT8(0, d->data[1]);
    TEST_ASSERT_EQUAL_UINT8(0x80, d->data[2] & 0x80u); // QR = 1
    TEST_ASSERT_EQUAL_UINT8(0x04, d->data[2] & 0x04u); // AA = 1
    TEST_ASSERT_EQUAL_UINT8(0x00, d->data[2] & 0x78u); // OPCODE 0 (sec 18.3)
    TEST_ASSERT_EQUAL_UINT8(0x00, d->data[2] & 0x02u); // TC = 0 (sec 18.5)
    TEST_ASSERT_EQUAL_UINT8(0, d->data[4]);            // QDCOUNT, both octets
    TEST_ASSERT_EQUAL_UINT8(0, d->data[5]);
    TEST_ASSERT_TRUE(answer_count(d) >= 1u);

    TEST_ASSERT_EQUAL_UINT8(224, d->addr[0]);
    TEST_ASSERT_EQUAL_UINT8(0, d->addr[1]);
    TEST_ASSERT_EQUAL_UINT8(0, d->addr[2]);
    TEST_ASSERT_EQUAL_UINT8(251, d->addr[3]);
    TEST_ASSERT_EQUAL_UINT16(MDNS_PORT, d->dst_port);
    TEST_ASSERT_EQUAL_UINT16(MDNS_PORT, d->src_port);
}

// RFC 6762 sec 10.2: a record whose name this host owns uniquely carries the cache-flush bit, the
// most significant bit of the rrclass. A shared record (a DNS-SD PTR) does not.
void test_the_cache_flush_bit_separates_unique_records_from_shared_ones(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);

    Rec r;
    ask("myhost._http._tcp.local", T_SRV);
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_UINT16(T_SRV, r.type);
    TEST_ASSERT_EQUAL_HEX16(0x8001u, r.cls); // unique: cache-flush set, class IN

    ask("myhost._http._tcp.local", T_TXT);
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_UINT16(T_TXT, r.type);
    TEST_ASSERT_EQUAL_HEX16(0x8001u, r.cls); // an instance's TXT is unique too

    ask("_http._tcp.local", T_PTR);
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_UINT16(T_PTR, r.type);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, r.cls); // shared: no cache-flush bit
}

// RFC 6763 sec 9: a PTR query for _services._dns-sd._udp.<Domain> lists the service types this host
// offers. begin() registered _http._tcp, so that is what comes back.
void test_service_enumeration_lists_the_registered_type(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    ask("_services._dns-sd._udp.local", T_PTR);

    Rec r;
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_STRING("_services._dns-sd._udp.local", r.owner);
    TEST_ASSERT_EQUAL_UINT16(T_PTR, r.type);
    char target[256];
    get_name(r.rdata, 0, target);
    TEST_ASSERT_EQUAL_STRING("_http._tcp.local", target);
}

// RFC 6763 sec 4.1: the service instance name is <Instance>.<Service>.<Domain>, and the service
// type's PTR names it.
void test_the_service_type_points_at_the_instance(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    ask("_http._tcp.local", T_PTR);

    Rec r;
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_STRING("_http._tcp.local", r.owner);
    char target[256];
    get_name(r.rdata, 0, target);
    TEST_ASSERT_EQUAL_STRING("myhost._http._tcp.local", target);
}

// RFC 6763 sec 4.1 / RFC 2782: the instance's SRV carries priority, weight, port and target, in that
// order, and the target is the host's own name.
void test_the_instance_srv_carries_the_port_and_the_target(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 8080;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    ask("myhost._http._tcp.local", T_SRV);

    Rec r;
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_UINT16(T_SRV, r.type);
    TEST_ASSERT_EQUAL_HEX16(0x8001u, r.cls); // an instance's SRV is a unique record
    TEST_ASSERT_EQUAL_UINT16(0u, (uint16_t)((r.rdata[0] << 8) | r.rdata[1]));
    TEST_ASSERT_EQUAL_UINT16(0u, (uint16_t)((r.rdata[2] << 8) | r.rdata[3]));
    TEST_ASSERT_EQUAL_UINT16(8080u, (uint16_t)((r.rdata[4] << 8) | r.rdata[5]));
    char target[256];
    get_name(r.rdata + 6, 0, target);
    TEST_ASSERT_EQUAL_STRING("myhost.local", target);
}

// RFC 6763 sec 6.1: "DNS-SD implementations MUST NOT emit empty TXT records"; a service with nothing
// to say sends a TXT record containing a single zero byte, which is one empty string.
void test_a_txt_record_is_never_zero_length(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    ask("myhost._http._tcp.local", T_TXT);

    Rec r;
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_UINT16(T_TXT, r.type);
    TEST_ASSERT_EQUAL_UINT16(1u, r.rdlen);
    TEST_ASSERT_EQUAL_UINT8(0u, r.rdata[0]);
}

// RFC 6763 sec 6.3: each key/value pair is one length-prefixed string of the form "key=value".
//   "path=/"    is 6 octets, so the string costs 1 + 6 = 7
//   "fw=1.2.3"  is 8 octets, so the string costs 1 + 8 = 9
//   the rdata is therefore 16 octets
void test_txt_pairs_are_length_prefixed_key_equals_value(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    MdnsService.txt_args.key = "path";
    MdnsService.txt_args.value = "/";
    MdnsService.txt(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    MdnsService.txt_args.key = "fw";
    MdnsService.txt_args.value = "1.2.3";
    MdnsService.txt(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    ask("myhost._http._tcp.local", T_TXT);

    Rec r;
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_UINT16(16u, r.rdlen);
    TEST_ASSERT_EQUAL_UINT8(6u, r.rdata[0]);
    TEST_ASSERT_EQUAL_MEMORY("path=/", &r.rdata[1], 6);
    TEST_ASSERT_EQUAL_UINT8(8u, r.rdata[7]);
    TEST_ASSERT_EQUAL_MEMORY("fw=1.2.3", &r.rdata[8], 8);
}

// QTYPE 255 (ANY) asks for every record at a name, so an instance answers with both the records it
// owns rather than picking one.
void test_qtype_any_on_an_instance_answers_srv_and_txt(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    ask("myhost._http._tcp.local", T_ANY);

    const protocore_net_host_dgram *d = response();
    TEST_ASSERT_EQUAL_UINT16(2u, answer_count(d));
    Rec a, b;
    TEST_ASSERT_TRUE(answer_at(d, 0, &a));
    TEST_ASSERT_TRUE(answer_at(d, 1, &b));
    TEST_ASSERT_EQUAL_UINT16(T_SRV, a.type);
    TEST_ASSERT_EQUAL_UINT16(T_TXT, b.type);
}

// A second service type is advertised alongside the _http._tcp one begin() registers, with its own
// instance name and its own port.
void test_an_added_service_is_advertised_beside_the_first(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    MdnsService.add_service_args.service_type = "_https";
    MdnsService.add_service_args.proto = "_tcp";
    MdnsService.add_service_args.port = 443;
    MdnsService.add_service(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);

    Rec r;
    ask("_https._tcp.local", T_PTR);
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_STRING("_https._tcp.local", r.owner);
    char target[256];
    get_name(r.rdata, 0, target);
    TEST_ASSERT_EQUAL_STRING("myhost._https._tcp.local", target);

    ask("myhost._https._tcp.local", T_SRV);
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_UINT16(443u, (uint16_t)((r.rdata[4] << 8) | r.rdata[5]));

    // The first type still answers.
    ask("_http._tcp.local", T_PTR);
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_STRING("_http._tcp.local", r.owner);
}

// RFC 6762 sec 6: "For shared records, NXDOMAIN and other error responses MUST NOT be sent" - a name
// this host does not own draws silence, not an error. A query for a type this host has no record of
// at a name it does own is the same silence.
void test_a_name_this_host_does_not_own_draws_silence(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);

    ask("someoneelse.local", T_A);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());

    ask("myhost.local", T_SRV); // the host name owns an A record, not an SRV
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());

    // The A record is the egress address, and there is none on a host with no link up, so the name
    // this responder does own is still answered with silence rather than with 0.0.0.0.
    ask("myhost.local", T_A);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
}

// RFC 6762 sec 18.2: a datagram with QR set is somebody's response, not a query. Answering one would
// make every responder on the link answer every other one.
void test_a_response_on_the_group_is_not_answered(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);

    uint8_t q[256];
    const size_t n = make_query(q, "_http._tcp.local", T_PTR);
    q[2] = 0x84; // QR + AA
    deliver(q, (uint16_t)n);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
}

// A question whose labels run past the end of the datagram is dropped whole rather than half
// answered from whatever followed it in the buffer.
void test_a_malformed_query_is_dropped(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);

    // QDCOUNT 1, then a label claiming five octets with three present.
    uint8_t q[16] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 5, 'l', 'o', 'c'};
    deliver(q, (uint16_t)sizeof q);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());

    // A header with a question count and no question at all.
    uint8_t hdr[12] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    deliver(hdr, (uint16_t)sizeof hdr);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
}

// The service table is a fixed array, so the request past the last slot is refused rather than
// overwriting a neighbour's entry.
void test_the_service_table_fills_and_then_refuses(void)
{
    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok); // takes the first slot
    for (size_t i = 1; i < PROTOCORE_MDNS_MAX_SERVICES; i++)
    {
        MdnsService.add_service_args.service_type = "_svc";
        MdnsService.add_service_args.proto = "_tcp";
        MdnsService.add_service_args.port = (uint16_t)(1000u + i);
        MdnsService.add_service(protocore_mdns_service_span());
        TEST_ASSERT_TRUE(MdnsService.ok);
    }
    MdnsService.add_service_args.service_type = "_over";
    MdnsService.add_service_args.proto = "_tcp";
    MdnsService.add_service_args.port = 9999;
    MdnsService.add_service(protocore_mdns_service_span());
    TEST_ASSERT_FALSE(MdnsService.ok);

    // The first service still answers, so the refusal did not disturb the table.
    Rec r;
    ask("_http._tcp.local", T_PTR);
    TEST_ASSERT_TRUE(answer_at(response(), 0, &r));
    TEST_ASSERT_EQUAL_STRING("_http._tcp.local", r.owner);
}

// A name that cannot be advertised is refused at the door: a responder that started on an empty or
// missing host name would answer for ".local" itself.
void test_a_missing_name_is_refused(void)
{
    MdnsService.begin_args.hostname = NULL;
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_FALSE(MdnsService.ok);
    MdnsService.begin_args.hostname = "";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_FALSE(MdnsService.ok);

    MdnsService.begin_args.hostname = "myhost";
    MdnsService.begin_args.http_port = 80;
    MdnsService.begin(protocore_mdns_service_span());
    TEST_ASSERT_TRUE(MdnsService.ok);
    MdnsService.txt_args.key = NULL;
    MdnsService.txt_args.value = "v";
    MdnsService.txt(protocore_mdns_service_span());
    TEST_ASSERT_FALSE(MdnsService.ok);
    MdnsService.txt_args.key = "k";
    MdnsService.txt_args.value = NULL;
    MdnsService.txt(protocore_mdns_service_span());
    TEST_ASSERT_FALSE(MdnsService.ok);
    MdnsService.add_service_args.service_type = NULL;
    MdnsService.add_service_args.proto = "_tcp";
    MdnsService.add_service_args.port = 1;
    MdnsService.add_service(protocore_mdns_service_span());
    TEST_ASSERT_FALSE(MdnsService.ok);
    MdnsService.add_service_args.service_type = "_x";
    MdnsService.add_service_args.proto = NULL;
    MdnsService.add_service_args.port = 1;
    MdnsService.add_service(protocore_mdns_service_span());
    TEST_ASSERT_FALSE(MdnsService.ok);
}
