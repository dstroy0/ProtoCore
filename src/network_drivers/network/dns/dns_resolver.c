// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_resolver.c
 * @brief IPv4 classifier / verifier, the query and answer codecs, and the resolve. See dns_resolver.h.
 */

#include "network_drivers/network/dns/dns_resolver.h"
#include "mmgr/protomem.h"

#if PROTOCORE_NEED_DNS_RESOLVER

#include "mmgr/secure.h"                          // protocore_secure_persist_span: this module's storage
#include "network_drivers/network/dns/dns_wire.h" // the name codec both DNS halves share
#include "server/clock/clock.h"                   // protocore_millis(): the deadline the resolve waits to

#if PROTOCORE_HAS_VENDOR_DNS_RESOLVER
#include "lwip/def.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/priv/tcpip_priv.h"
#else
#include "mmgr/protostr.h"                 // str: the bounded-run walks
#include "mmgr/rawmemcpy.h"                // proto_raw_read: the server address moves whole
#include "network_drivers/transport/udp.h" // Udp.listener: the query port and the ask
#include "shared_primitives/ip.h"          // Ip.parse: the server, and the dotted-quad fast path
#endif

// ---------------------------------------------------------------------------
// Pure: what an address is, and whether it is a plausible answer
// ---------------------------------------------------------------------------

static protocore_ip_class classify(uint32_t ip)
{
    if (ip == 0u)
    {
        return PROTOCORE_IP_UNSPECIFIED;
    }
    if (ip == 0xFFFFFFFFu)
    {
        return PROTOCORE_IP_BROADCAST;
    }
    uint8_t a = (uint8_t)((ip >> 24) & 0xFF);
    uint8_t b = (uint8_t)((ip >> 16) & 0xFF);
    if (a == 127)
    {
        return PROTOCORE_IP_LOOPBACK;
    }
    if (a == 10)
    {
        return PROTOCORE_IP_PRIVATE;
    }
    if (a == 172 && b >= 16 && b <= 31)
    {
        return PROTOCORE_IP_PRIVATE;
    }
    if (a == 192 && b == 168)
    {
        return PROTOCORE_IP_PRIVATE;
    }
    if (a == 169 && b == 254)
    {
        return PROTOCORE_IP_LINKLOCAL;
    }
    if (a >= 224 && a <= 239)
    {
        return PROTOCORE_IP_MULTICAST;
    }
    return PROTOCORE_IP_PUBLIC;
}

static proto_bool verify(uint32_t ip)
{
    switch (classify(ip))
    {
    case PROTOCORE_IP_UNSPECIFIED: // 0.0.0.0 - blocked / no answer
    case PROTOCORE_IP_BROADCAST:   // 255.255.255.255 - never a host
    case PROTOCORE_IP_LOOPBACK:    // 127.x - DNS-rebinding to localhost
    case PROTOCORE_IP_MULTICAST:   // 224-239 - never an A-record host
        return PROTO_FALSE;
    default:
        return PROTO_TRUE; // private / link-local / public are plausible
    }
}

// ---------------------------------------------------------------------------
// The wire: one question out, one address back
// ---------------------------------------------------------------------------

/** @brief Header words, and the record type / class an A query names (RFC 1035 sec 4.1.1). */
#define PROTOCORE_DNS_HDR_LEN 12u
#define PROTOCORE_DNS_T_A 1u
#define PROTOCORE_DNS_C_IN 1u
#define PROTOCORE_DNS_FLAG_RD 0x0100u ///< recursion desired
#define PROTOCORE_DNS_FLAG_QR 0x8000u ///< this message is a response
#define PROTOCORE_DNS_RCODE_MASK 0x000Fu

size_t protocore_dns_query_build(uint8_t *out, size_t cap, uint16_t id, const char *host)
{
    if (out == NULL || host == NULL || cap < PROTOCORE_DNS_HDR_LEN)
    {
        return 0;
    }
    for (size_t i = 0; i < PROTOCORE_DNS_HDR_LEN; i++)
    {
        out[i] = 0;
    }
    out[0] = (uint8_t)(id >> 8);
    out[1] = (uint8_t)id;
    out[2] = (uint8_t)(PROTOCORE_DNS_FLAG_RD >> 8);
    out[5] = 1; // QDCOUNT = 1
    size_t n = PROTOCORE_DNS_HDR_LEN;
    size_t w = protocore_dns_name_encode(out + n, cap - n, host);
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
    out[n] = PROTOCORE_DNS_T_A;
    n++;
    out[n] = 0;
    n++;
    out[n] = PROTOCORE_DNS_C_IN;
    n++;
    return n;
}

// This module's storage, borrowed once and held for the life of the program. A DNS transaction is
// what authenticates its own reply - the id, and the query the answer has to echo - so it comes from
// the secure pool, whose release wipes and whose region is disjoint from plaintext. One named owner,
// unreachable cross-TU.
typedef struct
{
    protocore_span name;   ///< where a record's owner name lands while the walk steps over it
    protocore_span tx;     ///< the query in flight
    protocore_span server; ///< the nameserver being asked
} DnsMemCtx;
static DnsMemCtx s_dns_mem;

// Take the borrows on first use. False when the pool cannot cover them, and every caller fails closed.
static proto_bool dns_mem_bind(void)
{
    if (protocore_span_has_storage(s_dns_mem.name))
    {
        return PROTO_TRUE;
    }
    s_dns_mem.name = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX);
    return protocore_span_has_storage(s_dns_mem.name);
}

proto_bool protocore_dns_answer_parse(const uint8_t *pkt, size_t len, uint16_t id, uint32_t *out_ip)
{
    if (pkt == NULL || out_ip == NULL || len < PROTOCORE_DNS_HDR_LEN || !dns_mem_bind())
    {
        return PROTO_FALSE;
    }
    uint16_t got_id = (uint16_t)(((uint16_t)pkt[0] << 8) | pkt[1]);
    uint16_t flags = (uint16_t)(((uint16_t)pkt[2] << 8) | pkt[3]);
    if (got_id != id || (flags & PROTOCORE_DNS_FLAG_QR) == 0 || (flags & PROTOCORE_DNS_RCODE_MASK) != 0)
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
    size_t off = PROTOCORE_DNS_HDR_LEN;
    char *name = (char *)s_dns_mem.name.buf;
    for (uint16_t q = 0; q < qd; q++)
    {
        if (!protocore_dns_name_decode(pkt, len, off, name, s_dns_mem.name.cap, &off, PROTO_TRUE))
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
        if (!protocore_dns_name_decode(pkt, len, off, name, s_dns_mem.name.cap, &off, PROTO_TRUE))
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
        if (type == PROTOCORE_DNS_T_A && cls == PROTOCORE_DNS_C_IN && rdlen == 4)
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

#if PROTOCORE_HAS_VENDOR_DNS_RESOLVER

// All DNS-resolve binding state, owned by one instance (internal linkage): the resolved address,
// the done/ok flags the lwIP callback sets, and the module's one timer, grouped so it is one named
// owner, unreachable cross-TU. The flags are atomic: the callback writes them on tcpip_thread while
// the caller reads them from its own tick, and volatile orders nothing across that seam.
typedef struct
{
    ip_addr_t addr;
    uint32_t timer;  ///< millis the query in flight left at
    proto_bool busy; ///< a query is out; a second host waits for it
    _Atomic proto_bool done;
    _Atomic proto_bool ok;
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

// The query in flight, and the timer it is measured against. The stack's callback sets done from its
// own thread, so the caller only ever reads it.
static proto_bool dns_busy(void)
{
    return s_dr.busy;
}

static protocore_dns_state resolve(const char *host, uint32_t *out_ip)
{
    if (!host || !out_ip)
    {
        return PROTOCORE_DNS_FAILED;
    }

    ip_addr_t literal;
    if (ipaddr_aton(host, &literal)) // dotted-quad fast path, no DNS
    {
        *out_ip = to_host_order(&literal);
        return PROTOCORE_DNS_READY;
    }

    if (s_dr.busy)
    {
        if (s_dr.done)
        {
            s_dr.busy = PROTO_FALSE;
            if (!s_dr.ok)
            {
                return PROTOCORE_DNS_FAILED;
            }
            *out_ip = to_host_order(&s_dr.addr);
            return PROTOCORE_DNS_READY;
        }
        if ((uint32_t)(protocore_millis() - s_dr.timer) >= PROTOCORE_DNS_TIMEOUT_MS)
        {
            s_dr.busy = PROTO_FALSE;
            return PROTOCORE_DNS_FAILED;
        }
        return PROTOCORE_DNS_BUSY;
    }

    s_dr.done = PROTO_FALSE;
    s_dr.ok = PROTO_FALSE;
    DnsCall k;
    mem.set(&k, 0, sizeof(k));
    k.host = host;
    tcpip_api_call(do_dns, &k.base); // resolve in the lwIP thread

    // Already cached, so the callback ran inside the marshal and there is nothing to wait for.
    if (s_dr.done)
    {
        if (!s_dr.ok)
        {
            return PROTOCORE_DNS_FAILED;
        }
        *out_ip = to_host_order(&s_dr.addr);
        return PROTOCORE_DNS_READY;
    }
    s_dr.busy = PROTO_TRUE;
    s_dr.timer = protocore_millis();
    return PROTOCORE_DNS_BUSY;
}

// The stack keeps its own nameserver list, learned from DHCP, so there is nothing here to point.
static proto_bool set_server(const char *ip)
{
    (void)ip;
    return PROTO_FALSE;
}

#else // the portable resolver

// All portable-resolve state, owned by one instance (internal linkage): the id and answer of the
// query in flight, and the module's one timer. The bytes those work on are the borrows in s_dns_mem.
// One query is in flight at a time, so the borrows are its alone.
typedef struct
{
    uint16_t id;
    uint32_t answer; ///< host order, 0 until a reply parses
    uint32_t timer;  ///< millis the query in flight left at
    proto_bool busy; ///< a query is out; a second host waits for it
    proto_bool done;
} DnsResolverCtx;
static DnsResolverCtx s_dr = {0, 0, 0, PROTO_FALSE, PROTO_FALSE};

// Take the query and nameserver borrows on first use, and seat the configured default in the latter.
static proto_bool dns_client_bind(void)
{
    if (protocore_span_has_storage(s_dns_mem.tx))
    {
        return PROTO_TRUE;
    }
    if (!dns_mem_bind())
    {
        return PROTO_FALSE;
    }
    s_dns_mem.tx = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX + 32);
    s_dns_mem.server = protocore_secure_persist_span(PROTOCORE_IP_STR_MAX);
    if (!protocore_span_has_storage(s_dns_mem.tx) || !protocore_span_has_storage(s_dns_mem.server))
    {
        return PROTO_FALSE;
    }
    size_t n = str.len(PROTOCORE_DNS_SERVER, s_dns_mem.server.cap);
    if (n >= s_dns_mem.server.cap)
    {
        return PROTO_FALSE;
    }
    proto_raw_read(s_dns_mem.server.buf, PROTOCORE_DNS_SERVER, n);
    s_dns_mem.server.buf[n] = '\0';
    return PROTO_TRUE;
}

// Take a reply, on the listener's drain. Always armed: busy is the sending side, so this runs
// whether or not a query is out. Anything that does not parse as an answer to the query in flight is
// ignored, and that query keeps waiting rather than failing on it.
static void dns_reply(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    uint32_t ip = 0;
    if (protocore_dns_answer_parse(data, len, s_dr.id, &ip))
    {
        s_dr.answer = ip;
        s_dr.done = PROTO_TRUE;
    }
}

static proto_bool set_server(const char *ip)
{
    protocore_ip probe = {PROTOCORE_IP_NONE, {0}};
    if (ip == NULL || !dns_client_bind() || !Ip.parse(ip, &probe))
    {
        return PROTO_FALSE;
    }
    size_t n = str.len(ip, s_dns_mem.server.cap);
    if (n >= s_dns_mem.server.cap)
    {
        return PROTO_FALSE;
    }
    proto_raw_read(s_dns_mem.server.buf, ip, n);
    s_dns_mem.server.buf[n] = '\0';
    return PROTO_TRUE;
}

static proto_bool dns_busy(void)
{
    return s_dr.busy;
}

static protocore_dns_state resolve(const char *host, uint32_t *out_ip)
{
    if (host == NULL || out_ip == NULL)
    {
        return PROTOCORE_DNS_FAILED;
    }
    protocore_ip literal = {PROTOCORE_IP_NONE, {0}};
    if (Ip.parse(host, &literal)) // a dotted quad answers itself, no query
    {
        *out_ip = ((uint32_t)literal.bytes[0] << 24) | ((uint32_t)literal.bytes[1] << 16) |
                  ((uint32_t)literal.bytes[2] << 8) | (uint32_t)literal.bytes[3];
        return PROTOCORE_DNS_READY;
    }

    // The query already out: the reply landed on the listener's drain, or the timer ran out.
    if (s_dr.busy)
    {
        if (s_dr.done)
        {
            s_dr.busy = PROTO_FALSE;
            *out_ip = s_dr.answer;
            return PROTOCORE_DNS_READY;
        }
        if ((uint32_t)(protocore_millis() - s_dr.timer) >= PROTOCORE_DNS_TIMEOUT_MS)
        {
            s_dr.busy = PROTO_FALSE;
            return PROTOCORE_DNS_FAILED;
        }
        return PROTOCORE_DNS_BUSY;
    }

    protocore_ip server = {PROTOCORE_IP_NONE, {0}};
    if (!dns_client_bind() || !Ip.parse((const char *)s_dns_mem.server.buf, &server))
    {
        return PROTOCORE_DNS_FAILED;
    }
    if (!Udp.listener->listen(PROTOCORE_DNS_CLIENT_PORT, dns_reply, NULL))
    {
        return PROTOCORE_DNS_FAILED;
    }

    // The id ties the reply to this query. Ticks, so two resolves in a row do not share one.
    s_dr.id = (uint16_t)(protocore_millis() | 1u);
    s_dr.answer = 0;
    s_dr.done = PROTO_FALSE;
    size_t n = protocore_dns_query_build(s_dns_mem.tx.buf, s_dns_mem.tx.cap, s_dr.id, host);
    if (n == 0)
    {
        return PROTOCORE_DNS_FAILED;
    }
    if (!Udp.listener->sendto(PROTOCORE_DNS_CLIENT_PORT, &server, 53, s_dns_mem.tx.buf, n))
    {
        return PROTOCORE_DNS_FAILED;
    }
    s_dr.busy = PROTO_TRUE;
    s_dr.timer = protocore_millis();
    return PROTOCORE_DNS_BUSY;
}

#endif // PROTOCORE_HAS_VENDOR_DNS_RESOLVER

static protocore_dns_state resolve_verified(const char *host, uint32_t *out_ip)
{
    uint32_t ip = 0;
    protocore_dns_state s = resolve(host, &ip);
    if (s != PROTOCORE_DNS_READY)
    {
        return s;
    }
    if (!verify(ip))
    {
        return PROTOCORE_DNS_FAILED;
    }
    if (out_ip)
    {
        *out_ip = ip;
    }
    return PROTOCORE_DNS_READY;
}

// Designated, so a member's position in the struct does not decide what it binds to.
const ResolverNs Resolver = {.classify = classify,
                             .verify = verify,
                             .resolve = resolve,
                             .resolve_verified = resolve_verified,
                             .busy = dns_busy,
                             .set_server = set_server};

#endif // PROTOCORE_NEED_DNS_RESOLVER
