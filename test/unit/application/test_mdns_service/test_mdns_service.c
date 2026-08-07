// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The portable mDNS / DNS-SD responder (network_drivers/application/mdns_service, RFC 6762 /
// RFC 6763): the 224.0.0.251:5353 join, and what it answers for the service-enumeration name, a
// service type, and an instance's SRV / TXT. Queries are built and responses decoded by this file's
// own label reader, so nothing under test is its own oracle.

#include "network_drivers/application/mdns_service/mdns_service.h"
#include "network_drivers/transport/udp.h"
#include "pc_net_host.h"
#include <string.h>

#include <unity.h>

#define MDNS_PORT 5353

void setUp()
{
    pc_net_host_reset();
}
void tearDown()
{
    Udp.listener->close(MDNS_PORT);
}

// --- the test's own label codec (independent of the one under test) -----------------------------

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

// Read the name at off into out, following one level of pointer. Returns the offset just past it.
static size_t get_name(const uint8_t *p, size_t off, char *out)
{
    size_t n = 0;
    size_t after = 0;
    for (;;)
    {
        uint8_t b = p[off];
        if ((b & 0xC0) == 0xC0)
        {
            if (after == 0)
            {
                after = off + 2;
            }
            off = (size_t)(((b & 0x3F) << 8) | p[off + 1]);
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
            out[n] = '.';
            n++;
        }
        for (uint8_t k = 0; k < b; k++)
        {
            out[n] = (char)p[off];
            n++;
            off++;
        }
    }
    out[n] = '\0';
    return after;
}

static size_t make_query(uint8_t *o, const char *name, uint16_t qtype)
{
    for (size_t i = 0; i < 12; i++)
    {
        o[i] = 0;
    }
    o[5] = 1; // QDCOUNT = 1
    size_t n = put_name(o, 12, name);
    o[n] = (uint8_t)(qtype >> 8);
    n++;
    o[n] = (uint8_t)qtype;
    n++;
    o[n] = 0;
    n++;
    o[n] = 1; // CLASS IN
    n++;
    return n;
}

// Put one query on the group and let the listener carry any answer to the wire.
static void ask(const char *name, uint16_t qtype)
{
    uint8_t q[256];
    size_t n = make_query(q, name, qtype);
    pc_net_host_udp_reset();
    TEST_ASSERT_TRUE(pc_net_host_udp_deliver(MDNS_PORT, "192.0.2.7", 5353, q, (uint16_t)n));
    Udp.listener->poll();
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

// Decode answer i of the captured response. Returns false when there is no such record.
static proto_bool answer_at(const pc_net_host_dgram *d, int want, Rec *r)
{
    uint16_t an = (uint16_t)((d->data[6] << 8) | d->data[7]);
    if (want >= (int)an)
    {
        return PROTO_FALSE;
    }
    size_t off = 12;
    for (int i = 0;; i++)
    {
        off = get_name(d->data, off, r->owner);
        r->type = (uint16_t)((d->data[off] << 8) | d->data[off + 1]);
        r->cls = (uint16_t)((d->data[off + 2] << 8) | d->data[off + 3]);
        r->ttl = ((uint32_t)d->data[off + 4] << 24) | ((uint32_t)d->data[off + 5] << 16) |
                 ((uint32_t)d->data[off + 6] << 8) | (uint32_t)d->data[off + 7];
        r->rdlen = (uint16_t)((d->data[off + 8] << 8) | d->data[off + 9]);
        r->rdata = &d->data[off + 10];
        if (i == want)
        {
            return PROTO_TRUE;
        }
        off += 10 + r->rdlen;
    }
}

// The one datagram the responder sent, checked as an mDNS response to the group.
static const pc_net_host_dgram *response(void)
{
    TEST_ASSERT_EQUAL_INT(1, (int)pc_net_host_udp_count());
    const pc_net_host_dgram *d = pc_net_host_udp_at(0);
    TEST_ASSERT_EQUAL_UINT8(224, d->addr[0]); // answered to 224.0.0.251, not to the asker
    TEST_ASSERT_EQUAL_UINT8(251, d->addr[3]);
    TEST_ASSERT_EQUAL_UINT16(MDNS_PORT, d->dst_port);
    TEST_ASSERT_EQUAL_UINT8(0x84, d->data[2]); // QR + AA
    TEST_ASSERT_EQUAL_UINT8(0, d->data[5]);    // no questions repeated back
    return d;
}

// --- tests --------------------------------------------------------------------------------------

void test_begin_joins_the_group()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    TEST_ASSERT_NOT_NULL(pc_net_host_udp_pcb(MDNS_PORT));
    TEST_ASSERT_EQUAL_STRING("224.0.0.251", Udp.listener->joined_group(MDNS_PORT));
}

void test_begin_rejects_a_bad_hostname()
{
    TEST_ASSERT_FALSE(pc_mdns_begin(NULL, 80));
    TEST_ASSERT_FALSE(pc_mdns_begin("", 80));
}

// A browser walks _services._dns-sd._udp.local to find what types the host offers; begin()
// registered _http._tcp, so that is what comes back.
void test_service_enumeration_lists_the_http_type()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    ask("_services._dns-sd._udp.local", 12); // PTR

    const pc_net_host_dgram *d = response();
    Rec r;
    TEST_ASSERT_TRUE(answer_at(d, 0, &r));
    TEST_ASSERT_EQUAL_STRING("_services._dns-sd._udp.local", r.owner);
    TEST_ASSERT_EQUAL_UINT16(12, r.type);
    TEST_ASSERT_EQUAL_UINT16(0x0001, r.cls); // shared record: no cache-flush bit
    char target[256];
    get_name(r.rdata, 0, target);
    TEST_ASSERT_EQUAL_STRING("_http._tcp.local", target);
}

// Asking the service type yields the instance that offers it.
void test_service_type_points_at_the_instance()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    ask("_http._tcp.local", 12);

    const pc_net_host_dgram *d = response();
    Rec r;
    TEST_ASSERT_TRUE(answer_at(d, 0, &r));
    TEST_ASSERT_EQUAL_STRING("_http._tcp.local", r.owner);
    char target[256];
    get_name(r.rdata, 0, target);
    TEST_ASSERT_EQUAL_STRING("myhost._http._tcp.local", target);
}

// SRV carries the port begin() was given and targets the host's own name.
void test_instance_srv_carries_port_and_target()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 8080));
    ask("myhost._http._tcp.local", 33); // SRV

    const pc_net_host_dgram *d = response();
    Rec r;
    TEST_ASSERT_TRUE(answer_at(d, 0, &r));
    TEST_ASSERT_EQUAL_UINT16(33, r.type);
    TEST_ASSERT_EQUAL_UINT16(0x8001, r.cls);                                 // unique record: cache-flush set
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)((r.rdata[0] << 8) | r.rdata[1])); // priority
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)((r.rdata[2] << 8) | r.rdata[3])); // weight
    TEST_ASSERT_EQUAL_UINT16(8080, (uint16_t)((r.rdata[4] << 8) | r.rdata[5]));
    char target[256];
    get_name(r.rdata + 6, 0, target);
    TEST_ASSERT_EQUAL_STRING("myhost.local", target);
}

// TXT with nothing added is one empty string, never zero-length (RFC 6763 sec 6.1).
void test_instance_txt_is_never_empty()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    ask("myhost._http._tcp.local", 16); // TXT

    const pc_net_host_dgram *d = response();
    Rec r;
    TEST_ASSERT_TRUE(answer_at(d, 0, &r));
    TEST_ASSERT_EQUAL_UINT16(16, r.type);
    TEST_ASSERT_EQUAL_UINT16(1, r.rdlen);
    TEST_ASSERT_EQUAL_UINT8(0, r.rdata[0]);
}

// Each key=value is one length-prefixed string in the TXT rdata.
void test_txt_records_are_length_prefixed()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    TEST_ASSERT_TRUE(pc_mdns_txt("path", "/"));
    TEST_ASSERT_TRUE(pc_mdns_txt("fw", "1.2.3"));
    ask("myhost._http._tcp.local", 16);

    const pc_net_host_dgram *d = response();
    Rec r;
    TEST_ASSERT_TRUE(answer_at(d, 0, &r));
    TEST_ASSERT_EQUAL_UINT8(6, r.rdata[0]);
    TEST_ASSERT_EQUAL_MEMORY("path=/", &r.rdata[1], 6);
    TEST_ASSERT_EQUAL_UINT8(8, r.rdata[7]);
    TEST_ASSERT_EQUAL_MEMORY("fw=1.2.3", &r.rdata[8], 8);
    TEST_ASSERT_EQUAL_UINT16(16, r.rdlen); // (1 + 6) + (1 + 8)
}

// QTYPE ANY on an instance answers with everything the instance owns.
void test_any_on_an_instance_answers_srv_and_txt()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    ask("myhost._http._tcp.local", 255);

    const pc_net_host_dgram *d = response();
    TEST_ASSERT_EQUAL_UINT16(2, (uint16_t)((d->data[6] << 8) | d->data[7]));
    Rec a, b;
    TEST_ASSERT_TRUE(answer_at(d, 0, &a));
    TEST_ASSERT_TRUE(answer_at(d, 1, &b));
    TEST_ASSERT_EQUAL_UINT16(33, a.type);
    TEST_ASSERT_EQUAL_UINT16(16, b.type);
}

// An added service is advertised alongside the _http._tcp one begin() registers.
void test_added_service_is_advertised()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    TEST_ASSERT_TRUE(pc_mdns_add_service("_https", "_tcp", 443));
    ask("_https._tcp.local", 12);

    const pc_net_host_dgram *d = response();
    Rec r;
    TEST_ASSERT_TRUE(answer_at(d, 0, &r));
    TEST_ASSERT_EQUAL_STRING("_https._tcp.local", r.owner);
    char target[256];
    get_name(r.rdata, 0, target);
    TEST_ASSERT_EQUAL_STRING("myhost._https._tcp.local", target);
}

// A name this responder does not own draws nothing at all: mDNS is silent on a miss rather than
// answering NXDOMAIN the way unicast DNS does (RFC 6762 sec 6).
void test_unknown_name_is_answered_with_silence()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    ask("someoneelse.local", 1);
    TEST_ASSERT_EQUAL_INT(0, (int)pc_net_host_udp_sent());
}

// A response arriving on the group is not a query, so it draws no reply and cannot start a storm.
void test_a_response_is_not_answered()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    uint8_t q[256];
    size_t n = make_query(q, "_http._tcp.local", 12);
    q[2] = 0x84; // QR + AA: this is somebody's answer
    pc_net_host_udp_reset();
    TEST_ASSERT_TRUE(pc_net_host_udp_deliver(MDNS_PORT, "192.0.2.7", 5353, q, (uint16_t)n));
    Udp.listener->poll();
    TEST_ASSERT_EQUAL_INT(0, (int)pc_net_host_udp_sent());
}

// A query whose question is truncated is dropped rather than half-answered.
void test_malformed_query_is_dropped()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    uint8_t q[16] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 5, 'l', 'o', 'c'}; // label runs off the end
    pc_net_host_udp_reset();
    TEST_ASSERT_TRUE(pc_net_host_udp_deliver(MDNS_PORT, "192.0.2.7", 5353, q, sizeof(q)));
    Udp.listener->poll();
    TEST_ASSERT_EQUAL_INT(0, (int)pc_net_host_udp_sent());
}

// The service table is fixed, so the one past it is refused rather than overwriting a neighbour.
void test_service_table_fills_and_refuses()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80)); // takes the first slot
    for (int i = 1; i < PC_MDNS_MAX_SERVICES; i++)
    {
        TEST_ASSERT_TRUE(pc_mdns_add_service("_svc", "_tcp", (uint16_t)(1000 + i)));
    }
    TEST_ASSERT_FALSE(pc_mdns_add_service("_over", "_tcp", 9999));
}

void test_txt_and_add_service_reject_null()
{
    TEST_ASSERT_TRUE(pc_mdns_begin("myhost", 80));
    TEST_ASSERT_FALSE(pc_mdns_txt(NULL, "v"));
    TEST_ASSERT_FALSE(pc_mdns_txt("k", NULL));
    TEST_ASSERT_FALSE(pc_mdns_add_service(NULL, "_tcp", 1));
    TEST_ASSERT_FALSE(pc_mdns_add_service("_x", NULL, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_joins_the_group);
    RUN_TEST(test_begin_rejects_a_bad_hostname);
    RUN_TEST(test_service_enumeration_lists_the_http_type);
    RUN_TEST(test_service_type_points_at_the_instance);
    RUN_TEST(test_instance_srv_carries_port_and_target);
    RUN_TEST(test_instance_txt_is_never_empty);
    RUN_TEST(test_txt_records_are_length_prefixed);
    RUN_TEST(test_any_on_an_instance_answers_srv_and_txt);
    RUN_TEST(test_added_service_is_advertised);
    RUN_TEST(test_unknown_name_is_answered_with_silence);
    RUN_TEST(test_a_response_is_not_answered);
    RUN_TEST(test_malformed_query_is_dropped);
    RUN_TEST(test_service_table_fills_and_refuses);
    RUN_TEST(test_txt_and_add_service_reject_null);
    return UNITY_END();
}
