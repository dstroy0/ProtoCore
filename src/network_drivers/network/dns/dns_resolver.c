// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_resolver.c
 * @brief IPv4 classifier / verifier, the query and answer codecs, and the resolve. See dns_resolver.h.
 */

#include "network_drivers/network/dns/dns_resolver.h"

#if PC_NEED_DNS_RESOLVER

#include "mmgr/secure.h"                          // pc_secure_persist_span: this module's storage
#include "network_drivers/network/dns/dns_wire.h" // the name codec both DNS halves share
#include "server/clock/clock.h"                   // pc_millis(): the deadline the resolve waits to

#if PC_HAS_VENDOR_DNS_RESOLVER
#include "lwip/def.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/priv/tcpip_priv.h"
#else
#include "mmgr/rawmemcpy.h"                // proto_raw_read: the server address moves whole
#include "network_drivers/transport/udp.h" // Udp.listener: the query port and the ask
#include "shared_primitives/ip.h"          // Ip.parse: the server, and the dotted-quad fast path
#include "shared_primitives/runops.h"      // proto_scan_nul: where the caller's address ends
#endif

// ---------------------------------------------------------------------------
// Pure: what an address is, and whether it is a plausible answer
// ---------------------------------------------------------------------------

static pc_ip_class classify(uint32_t ip)
{
    if (ip == 0u)
    {
        return PC_IP_UNSPECIFIED;
    }
    if (ip == 0xFFFFFFFFu)
    {
        return PC_IP_BROADCAST;
    }
    uint8_t a = (uint8_t)((ip >> 24) & 0xFF);
    uint8_t b = (uint8_t)((ip >> 16) & 0xFF);
    if (a == 127)
    {
        return PC_IP_LOOPBACK;
    }
    if (a == 10)
    {
        return PC_IP_PRIVATE;
    }
    if (a == 172 && b >= 16 && b <= 31)
    {
        return PC_IP_PRIVATE;
    }
    if (a == 192 && b == 168)
    {
        return PC_IP_PRIVATE;
    }
    if (a == 169 && b == 254)
    {
        return PC_IP_LINKLOCAL;
    }
    if (a >= 224 && a <= 239)
    {
        return PC_IP_MULTICAST;
    }
    return PC_IP_PUBLIC;
}

static proto_bool verify(uint32_t ip)
{
    switch (classify(ip))
    {
    case PC_IP_UNSPECIFIED: // 0.0.0.0 - blocked / no answer
    case PC_IP_BROADCAST:   // 255.255.255.255 - never a host
    case PC_IP_LOOPBACK:    // 127.x - DNS-rebinding to localhost
    case PC_IP_MULTICAST:   // 224-239 - never an A-record host
        return PROTO_FALSE;
    default:
        return PROTO_TRUE; // private / link-local / public are plausible
    }
}

// ---------------------------------------------------------------------------
// The wire: one question out, one address back
// ---------------------------------------------------------------------------

/** @brief Header words, and the record type / class an A query names (RFC 1035 sec 4.1.1). */
#define PC_DNS_HDR_LEN 12u
#define PC_DNS_T_A 1u
#define PC_DNS_C_IN 1u
#define PC_DNS_FLAG_RD 0x0100u ///< recursion desired
#define PC_DNS_FLAG_QR 0x8000u ///< this message is a response
#define PC_DNS_RCODE_MASK 0x000Fu

size_t pc_dns_query_build(uint8_t *out, size_t cap, uint16_t id, const char *host)
{
    if (out == NULL || host == NULL || cap < PC_DNS_HDR_LEN)
    {
        return 0;
    }
    for (size_t i = 0; i < PC_DNS_HDR_LEN; i++)
    {
        out[i] = 0;
    }
    out[0] = (uint8_t)(id >> 8);
    out[1] = (uint8_t)id;
    out[2] = (uint8_t)(PC_DNS_FLAG_RD >> 8);
    out[5] = 1; // QDCOUNT = 1
    size_t n = PC_DNS_HDR_LEN;
    size_t w = pc_dns_name_encode(out + n, cap - n, host);
    if (w == 0)
    {
        return 0;
    }
    n += w;
    if (n + 4 > cap)
    {
        return 0;
    }
    out[n] = 0;
    n++;
    out[n] = PC_DNS_T_A;
    n++;
    out[n] = 0;
    n++;
    out[n] = PC_DNS_C_IN;
    n++;
    return n;
}

// This module's storage, borrowed once and held for the life of the program. A DNS transaction is
// what authenticates its own reply - the id, and the query the answer has to echo - so it comes from
// the secure pool, whose release wipes and whose region is disjoint from plaintext. One named owner,
// unreachable cross-TU.
typedef struct
{
    pc_span name;   ///< where a record's owner name lands while the walk steps over it
    pc_span tx;     ///< the query in flight
    pc_span server; ///< the nameserver being asked
} DnsMemCtx;
static DnsMemCtx s_dns_mem;

// Take the borrows on first use. False when the pool cannot cover them, and every caller fails closed.
static proto_bool dns_mem_bind(void)
{
    if (pc_span_has_storage(s_dns_mem.name))
    {
        return PROTO_TRUE;
    }
    s_dns_mem.name = pc_secure_persist_span(PC_DNS_NAME_MAX);
    return pc_span_has_storage(s_dns_mem.name);
}

proto_bool pc_dns_answer_parse(const uint8_t *pkt, size_t len, uint16_t id, uint32_t *out_ip)
{
    if (pkt == NULL || out_ip == NULL || len < PC_DNS_HDR_LEN || !dns_mem_bind())
    {
        return PROTO_FALSE;
    }
    uint16_t got_id = (uint16_t)(((uint16_t)pkt[0] << 8) | pkt[1]);
    uint16_t flags = (uint16_t)(((uint16_t)pkt[2] << 8) | pkt[3]);
    if (got_id != id || (flags & PC_DNS_FLAG_QR) == 0 || (flags & PC_DNS_RCODE_MASK) != 0)
    {
        return PROTO_FALSE; // not our answer, not an answer, or the server said no
    }
    uint16_t qd = (uint16_t)(((uint16_t)pkt[4] << 8) | pkt[5]);
    uint16_t an = (uint16_t)(((uint16_t)pkt[6] << 8) | pkt[7]);
    if (an == 0)
    {
        return PROTO_FALSE;
    }

    // Step over the questions: each is a name then QTYPE + QCLASS. The name goes into the caller's
    // borrow and is discarded; what the walk wants from the decode is where the record's fields begin.
    size_t off = PC_DNS_HDR_LEN;
    char *name = (char *)s_dns_mem.name.buf;
    for (uint16_t q = 0; q < qd; q++)
    {
        if (!pc_dns_name_decode(pkt, len, off, name, s_dns_mem.name.cap, &off, PROTO_TRUE))
        {
            return PROTO_FALSE;
        }
        if (off + 4 > len)
        {
            return PROTO_FALSE;
        }
        off += 4;
    }

    // The first A record wins. A name that CNAMEs somewhere else answers with both records, and the
    // address is the one this resolver was asked for, so anything that is not an A is stepped over.
    for (uint16_t r = 0; r < an; r++)
    {
        if (!pc_dns_name_decode(pkt, len, off, name, s_dns_mem.name.cap, &off, PROTO_TRUE))
        {
            return PROTO_FALSE;
        }
        if (off + 10 > len)
        {
            return PROTO_FALSE;
        }
        uint16_t type = (uint16_t)(((uint16_t)pkt[off] << 8) | pkt[off + 1]);
        uint16_t cls = (uint16_t)(((uint16_t)pkt[off + 2] << 8) | pkt[off + 3]);
        uint16_t rdlen = (uint16_t)(((uint16_t)pkt[off + 8] << 8) | pkt[off + 9]);
        off += 10;
        if (off + rdlen > len)
        {
            return PROTO_FALSE;
        }
        if (type == PC_DNS_T_A && cls == PC_DNS_C_IN && rdlen == 4)
        {
            *out_ip = ((uint32_t)pkt[off] << 24) | ((uint32_t)pkt[off + 1] << 16) | ((uint32_t)pkt[off + 2] << 8) |
                      (uint32_t)pkt[off + 3];
            return PROTO_TRUE;
        }
        off += rdlen;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// The resolve
// ---------------------------------------------------------------------------

#if PC_HAS_VENDOR_DNS_RESOLVER

// All DNS-resolve binding state, owned by one instance (internal linkage): the resolved
// address plus the done/ok flags the lwIP callback sets, grouped so it is one named owner,
// unreachable cross-TU. The flags are volatile: the callback runs on tcpip_thread while the
// resolve loop polls them.
typedef struct
{
    ip_addr_t addr;
    volatile proto_bool done;
    volatile proto_bool ok;
} DnsResolverCtx;
static DnsResolverCtx s_dr;

typedef struct
{
    struct tcpip_api_call_data base;
    const char *host;
} DnsCall;

static void dns_cb(const char *name, const ip_addr_t *addr, void *arg)
{
    (void)name;
    (void)arg;
    if (addr)
    {
        s_dr.addr = *addr;
        s_dr.ok = PROTO_TRUE;
    }
    s_dr.done = PROTO_TRUE;
}

static err_t do_dns(struct tcpip_api_call_data *c)
{
    const char *host = ((DnsCall *)c)->host;
    err_t e = dns_gethostbyname(host, &s_dr.addr, dns_cb, NULL);
    if (e == ERR_OK) // already cached
    {
        s_dr.ok = PROTO_TRUE;
        s_dr.done = PROTO_TRUE;
    }
    else if (e != ERR_INPROGRESS) // hard failure
    {
        s_dr.done = PROTO_TRUE;
    }
    return ERR_OK;
}

static uint32_t to_host_order(const ip_addr_t *a)
{
    return lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(a)));
}

static proto_bool resolve(const char *host, uint32_t *out_ip)
{
    if (!host || !out_ip)
    {
        return PROTO_FALSE;
    }

    ip_addr_t literal;
    if (ipaddr_aton(host, &literal)) // dotted-quad fast path, no DNS
    {
        *out_ip = to_host_order(&literal);
        return PROTO_TRUE;
    }

    s_dr.done = PROTO_FALSE;
    s_dr.ok = PROTO_FALSE;
    DnsCall k;
    memset(&k, 0, sizeof(k));
    k.host = host;
    tcpip_api_call(do_dns, &k.base); // resolve in the lwIP thread

    uint32_t deadline = pc_millis() + PC_DNS_TIMEOUT_MS;
    while (!s_dr.done && (int32_t)(deadline - pc_millis()) > 0)
    {
        pcdelay(5);
    }

    if (!s_dr.ok)
    {
        return PROTO_FALSE;
    }
    *out_ip = to_host_order(&s_dr.addr);
    return PROTO_TRUE;
}

// The stack keeps its own nameserver list, learned from DHCP, so there is nothing here to point.
static proto_bool set_server(const char *ip)
{
    (void)ip;
    return PROTO_FALSE;
}

#else // the portable resolver

// All portable-resolve state, owned by one instance (internal linkage): the id and answer of the
// query in flight. The bytes those work on are the borrows in s_dns_mem. resolve() blocks until the
// answer or the deadline, so only one query is ever in flight and the borrows are its alone.
typedef struct
{
    uint16_t id;
    uint32_t answer; ///< host order, 0 until a reply parses
    proto_bool done;
} DnsResolverCtx;
static DnsResolverCtx s_dr = {0, 0, PROTO_FALSE};

// Take the query and nameserver borrows on first use, and seat the configured default in the latter.
static proto_bool dns_client_bind(void)
{
    if (pc_span_has_storage(s_dns_mem.tx))
    {
        return PROTO_TRUE;
    }
    if (!dns_mem_bind())
    {
        return PROTO_FALSE;
    }
    s_dns_mem.tx = pc_secure_persist_span(PC_DNS_NAME_MAX + 32);
    s_dns_mem.server = pc_secure_persist_span(PC_IP_STR_MAX);
    if (!pc_span_has_storage(s_dns_mem.tx) || !pc_span_has_storage(s_dns_mem.server))
    {
        return PROTO_FALSE;
    }
    size_t n = proto_scan_nul(PC_DNS_SERVER, s_dns_mem.server.cap);
    if (n >= s_dns_mem.server.cap)
    {
        return PROTO_FALSE;
    }
    proto_raw_read(s_dns_mem.server.buf, PC_DNS_SERVER, n);
    s_dns_mem.server.buf[n] = '\0';
    return PROTO_TRUE;
}

// Take a reply: anything that does not parse as an answer to the query in flight is ignored, and the
// resolve keeps waiting rather than failing on it.
static void dns_reply(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    uint32_t ip = 0;
    if (pc_dns_answer_parse(data, len, s_dr.id, &ip))
    {
        s_dr.answer = ip;
        s_dr.done = PROTO_TRUE;
    }
}

static proto_bool set_server(const char *ip)
{
    pc_ip probe = {PC_IP_NONE, {0}};
    if (ip == NULL || !dns_client_bind() || !Ip.parse(ip, &probe))
    {
        return PROTO_FALSE;
    }
    size_t n = proto_scan_nul(ip, s_dns_mem.server.cap);
    if (n >= s_dns_mem.server.cap)
    {
        return PROTO_FALSE;
    }
    proto_raw_read(s_dns_mem.server.buf, ip, n);
    s_dns_mem.server.buf[n] = '\0';
    return PROTO_TRUE;
}

static proto_bool resolve(const char *host, uint32_t *out_ip)
{
    if (host == NULL || out_ip == NULL)
    {
        return PROTO_FALSE;
    }
    pc_ip literal = {PC_IP_NONE, {0}};
    if (Ip.parse(host, &literal)) // a dotted quad answers itself, no query
    {
        *out_ip = ((uint32_t)literal.bytes[0] << 24) | ((uint32_t)literal.bytes[1] << 16) |
                  ((uint32_t)literal.bytes[2] << 8) | (uint32_t)literal.bytes[3];
        return PROTO_TRUE;
    }
    pc_ip server = {PC_IP_NONE, {0}};
    if (!dns_client_bind() || !Ip.parse((const char *)s_dns_mem.server.buf, &server))
    {
        return PROTO_FALSE;
    }
    if (!Udp.listener->listen(PC_DNS_CLIENT_PORT, dns_reply, NULL))
    {
        return PROTO_FALSE;
    }

    // The id ties the reply to this query. Ticks, so two resolves in a row do not share one.
    s_dr.id = (uint16_t)(pc_millis() | 1u);
    s_dr.answer = 0;
    s_dr.done = PROTO_FALSE;
    size_t n = pc_dns_query_build(s_dns_mem.tx.buf, s_dns_mem.tx.cap, s_dr.id, host);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    if (!Udp.listener->sendto(PC_DNS_CLIENT_PORT, &server, 53, s_dns_mem.tx.buf, n))
    {
        return PROTO_FALSE;
    }

    uint32_t deadline = pc_millis() + PC_DNS_TIMEOUT_MS;
    while (!s_dr.done && (int32_t)(deadline - pc_millis()) > 0)
    {
        Udp.listener->poll(); // the query leaves and the reply arrives on this pump
        pcdelay(5);
    }
    if (!s_dr.done)
    {
        return PROTO_FALSE;
    }
    *out_ip = s_dr.answer;
    return PROTO_TRUE;
}

#endif // PC_HAS_VENDOR_DNS_RESOLVER

static proto_bool resolve_verified(const char *host, uint32_t *out_ip)
{
    uint32_t ip = 0;
    if (!resolve(host, &ip))
    {
        return PROTO_FALSE;
    }
    if (!verify(ip))
    {
        return PROTO_FALSE;
    }
    if (out_ip)
    {
        *out_ip = ip;
    }
    return PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
const ResolverNs Resolver = {.classify = classify,
                             .verify = verify,
                             .resolve = resolve,
                             .resolve_verified = resolve_verified,
                             .set_server = set_server};

#endif // PC_NEED_DNS_RESOLVER
