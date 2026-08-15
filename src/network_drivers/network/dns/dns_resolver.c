// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_resolver.c
 * @brief The IPv4 classifier, the RFC 1035 query and answer codecs, and the resolve.
 *        See dns_resolver.h.
 */

#include "network_drivers/network/dns/dns_resolver.h"
#include "mmgr/protomem.h"

#if PROTOCORE_NEED_DNS_RESOLVER

#include "mmgr/secure.h"                          // protocore_secure_persist_span: this module's storage
#include "network_drivers/network/dns/dns_wire.h" // the name codec both DNS halves share
#include "server/clock/clock.h"                   // Clock.millis: the deadline the resolve waits to

#if PROTOCORE_HAS_VENDOR_DNS_RESOLVER
#include "core_setup/board_profiles/protocore_platform.h" // the platform's own resolver, under our names
#else
#include "mmgr/protostr.h"                               // str: the bounded-run walks
#include "mmgr/rawmemcpy.h"                              // raw.read: the server address moves whole
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the query port and the ask
#include "shared/ip/ip.h"                                // Ip.parse: the server, and the dotted-quad fast path
#endif

// ---------------------------------------------------------------------------
// The wire constants
// ---------------------------------------------------------------------------

/** @brief Header length and the QTYPE / QCLASS an A query names (RFC 1035 sec 4.1.1, sec 4.1.2). */
#define PROTOCORE_DNS_HDR_LEN 12u
#define PROTOCORE_DNS_T_A 1u
#define PROTOCORE_DNS_C_IN 1u
#define PROTOCORE_DNS_FLAG_RD 0x0100u    ///< RD: recursion desired (RFC 1035 sec 4.1.1)
#define PROTOCORE_DNS_FLAG_QR 0x8000u    ///< QR: this message is a response (RFC 1035 sec 4.1.1)
#define PROTOCORE_DNS_RCODE_MASK 0x000Fu ///< RCODE: 0 is no error (RFC 1035 sec 4.1.1)
#define PROTOCORE_DNS_RR_FIXED 10u       ///< TYPE, CLASS, TTL, RDLENGTH ahead of RDATA (RFC 1035 sec 4.1.3)
#define PROTOCORE_DNS_A_RDLEN 4u         ///< the A RDATA is four octets (RFC 1035 sec 3.4.1)
#define PROTOCORE_DNS_PORT 53u           ///< the server port a query is sent to (RFC 1035 sec 4.2.1)

// ---------------------------------------------------------------------------
// The resolver's state, and the handle that reaches it
// ---------------------------------------------------------------------------

/**
 * @brief The resolver's compile-time storage: the borrowed spans, the query in flight, its timer.
 *
 * The spans are borrowed once from the secure pool, whose release wipes and whose region is disjoint
 * from plaintext, and held for the life of the program.
 */
struct ResolverStorage
{
    protocore_span name; ///< where a record's owner name lands while the walk steps over it
#if PROTOCORE_HAS_VENDOR_DNS_RESOLVER
    protocore_net_ip addr;   ///< the address the stack's callback reported
    _Atomic proto_bool done; ///< the callback ran
    _Atomic proto_bool ok;   ///< and it carried an address
#else
    protocore_span tx;     ///< the query message in flight
    protocore_span server; ///< the nameserver being asked, as text
    uint16_t id;           ///< the ID of the query in flight (RFC 1035 sec 4.1.1)
    uint32_t answer;       ///< host order, 0 until a response parses
    proto_bool done;       ///< a response parsed
#endif
    uint32_t timer;  ///< the millisecond the query in flight left at
    proto_bool busy; ///< a query is out; a second host waits for it
};

/**
 * @brief The resolver's state and the calls that reach it - what ResolverNs points at.
 *
 * @var ResolverInternal::store  the borrowed spans, the query in flight, and its timer
 * @var ResolverInternal::ns     the handle a caller sets a call's members on
 */
struct ResolverInternal
{
    struct ResolverStorage *store;
    ResolverNs *ns;
};

static struct ResolverStorage s_store;

static struct ResolverInternal s_resolver = {.store = &s_store, .ns = &Resolver};

// ---------------------------------------------------------------------------
// Pure: what an address is, and whether it is a plausible answer
// ---------------------------------------------------------------------------

// The registry entry a host-order word falls in (RFC 6890 sec 2.2.2, RFC 1112 sec 4).
static protocore_ip_class ip_class(uint32_t ip)
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

// Whether that word is a plausible A record for a remote host.
static proto_bool ip_plausible(uint32_t ip)
{
    switch (ip_class(ip))
    {
    case PROTOCORE_IP_UNSPECIFIED: // 0.0.0.0 - blocked / no answer
    case PROTOCORE_IP_BROADCAST:   // 255.255.255.255 - never a host
    case PROTOCORE_IP_LOOPBACK:    // 127.x - a name resolving back to this host
    case PROTOCORE_IP_MULTICAST:   // 224-239 - never an A record host
        return PROTO_FALSE;
    default:
        return PROTO_TRUE; // private / link-local / public are plausible
    }
}

// ---------------------------------------------------------------------------
// The wire: one question out, one address back
// ---------------------------------------------------------------------------

// Write a header carrying @p id with RD set and QDCOUNT 1, then the name, QTYPE A, QCLASS IN.
// Returns the octets written, or 0 when the name does not encode or does not fit @p cap.
static size_t question_build(uint8_t *out, size_t cap, uint16_t id, const char *host)
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
    DnsWire.text.dotted = host;
    DnsWire.text.out = out + n;
    DnsWire.text.out_cap = cap - n;
    DnsWire.encode(DnsWire.internal);
    size_t w = DnsWire.n;
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

// Take the name borrow on first use. False when the pool cannot cover it, and every caller fails
// closed.
static proto_bool name_bind(struct ResolverInternal *restrict ctx)
{
    if (span.has_storage(ctx->store->name))
    {
        return PROTO_TRUE;
    }
    ctx->store->name = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX);
    return span.has_storage(ctx->store->name);
}

// Read the first A record out of @p pkt into @p out_ip, host order. Accepts the message only when
// its ID is @p id, QR is set, and RCODE is 0 (RFC 1035 sec 4.1.1, RFC 5452 sec 9.1).
static proto_bool answer_read(struct ResolverInternal *restrict ctx, const uint8_t *pkt, size_t len, uint16_t id,
                              uint32_t *out_ip)
{
    if (pkt == NULL || out_ip == NULL || len < PROTOCORE_DNS_HDR_LEN || !name_bind(ctx))
    {
        return PROTO_FALSE;
    }
    uint16_t got_id = (uint16_t)(((uint16_t)pkt[0] << 8) | pkt[1]);
    uint16_t flags = (uint16_t)(((uint16_t)pkt[2] << 8) | pkt[3]);
    if (got_id != id || (flags & PROTOCORE_DNS_FLAG_QR) == 0 || (flags & PROTOCORE_DNS_RCODE_MASK) != 0)
    {
        return PROTO_FALSE; // not our answer, not an answer, or the server said no
    }
    uint16_t qd = (uint16_t)(((uint16_t)pkt[4] << 8) | pkt[5]); // QDCOUNT
    uint16_t an = (uint16_t)(((uint16_t)pkt[6] << 8) | pkt[7]); // ANCOUNT
    if (an == 0)
    {
        return PROTO_FALSE;
    }

    // Step over the question section: each entry is a QNAME then QTYPE + QCLASS. The name goes into
    // the borrow and is discarded; what the walk wants from the decode is where the fields begin.
    size_t off = PROTOCORE_DNS_HDR_LEN;
    char *name = (char *)ctx->store->name.buf;
    for (uint16_t q = 0; q < qd; q++)
    {
        DnsWire.msg.pkt = pkt;
        DnsWire.msg.len = len;
        DnsWire.msg.off = off;
        DnsWire.msg.out = name;
        DnsWire.msg.out_cap = ctx->store->name.cap;
        DnsWire.msg.allow_ptr = PROTO_TRUE;
        DnsWire.decode(DnsWire.internal);
        if (!DnsWire.ok)
        {
            return PROTO_FALSE;
        }
        off = DnsWire.next;
        if (off + 4 > len)
        {
            return PROTO_FALSE;
        }
        off += 4;
    }

    // The first A record wins. A name that CNAMEs somewhere else answers with both records, and the
    // address is the one this resolver was asked for, so any other TYPE is stepped over.
    for (uint16_t r = 0; r < an; r++)
    {
        DnsWire.msg.pkt = pkt;
        DnsWire.msg.len = len;
        DnsWire.msg.off = off;
        DnsWire.msg.out = name;
        DnsWire.msg.out_cap = ctx->store->name.cap;
        DnsWire.msg.allow_ptr = PROTO_TRUE;
        DnsWire.decode(DnsWire.internal);
        if (!DnsWire.ok)
        {
            return PROTO_FALSE;
        }
        off = DnsWire.next;
        if (off + PROTOCORE_DNS_RR_FIXED > len)
        {
            return PROTO_FALSE;
        }
        uint16_t type = (uint16_t)(((uint16_t)pkt[off] << 8) | pkt[off + 1]);
        uint16_t cls = (uint16_t)(((uint16_t)pkt[off + 2] << 8) | pkt[off + 3]);
        uint16_t rdlen = (uint16_t)(((uint16_t)pkt[off + 8] << 8) | pkt[off + 9]);
        off += PROTOCORE_DNS_RR_FIXED;
        if (off + rdlen > len)
        {
            return PROTO_FALSE;
        }
        if (type == PROTOCORE_DNS_T_A && cls == PROTOCORE_DNS_C_IN && rdlen == PROTOCORE_DNS_A_RDLEN)
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

// The marshal record: the name to resolve and the handle its outcome lands on, carried onto the
// stack's own thread.
typedef struct
{
    protocore_net_call base;
    struct ResolverInternal *ctx;
    const char *host;
} DnsMarshal;

// The four network-order octets, read in wire order into a host-order word.
static uint32_t addr_host_order(const protocore_net_ip *a)
{
    const uint32_t net = protocore_net_ip4_u32(protocore_net_ip_as_v4(a));
    const uint8_t *b = (const uint8_t *)&net;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

// The stack's completion, on its own thread: lands the address, then the flags the caller's tick
// reads. The flags are atomic, so the address write is ordered ahead of them across that seam.
static void dns_found(const char *name, const protocore_net_ip *addr, void *arg)
{
    (void)name;
    struct ResolverInternal *ctx = (struct ResolverInternal *)arg;
    if (ctx == NULL)
    {
        return;
    }
    if (addr != NULL)
    {
        ctx->store->addr = *addr;
        ctx->store->ok = PROTO_TRUE;
    }
    ctx->store->done = PROTO_TRUE;
}

// The ask, on the stack's own thread. A name the stack already holds completes inside this call.
static protocore_net_err dns_ask(protocore_net_call *c)
{
    DnsMarshal *m = (DnsMarshal *)c;
    protocore_net_err e = protocore_net_dns_resolve(m->host, &m->ctx->store->addr, dns_found, m->ctx);
    if (e == PROTOCORE_NET_OK) // already held
    {
        m->ctx->store->ok = PROTO_TRUE;
        m->ctx->store->done = PROTO_TRUE;
    }
    else if (e != PROTOCORE_NET_ERR_INPROGRESS) // hard failure
    {
        m->ctx->store->done = PROTO_TRUE;
    }
    return PROTOCORE_NET_OK;
}

static void resolver_resolve(struct ResolverInternal *restrict ctx)
{
    ctx->ns->u32 = 0;
    ctx->ns->state = PROTOCORE_DNS_FAILED;
    const char *host = ctx->ns->query.host;
    if (host == NULL)
    {
        return;
    }

    protocore_net_ip literal;
    if (protocore_net_ip_parse(host, &literal)) // dotted-quad fast path, no query
    {
        ctx->ns->u32 = addr_host_order(&literal);
        ctx->ns->state = PROTOCORE_DNS_READY;
        return;
    }

    // The query already out: the stack's callback ran, or the timer ran out.
    if (ctx->store->busy)
    {
        if (ctx->store->done)
        {
            ctx->store->busy = PROTO_FALSE;
            if (!ctx->store->ok)
            {
                return;
            }
            ctx->ns->u32 = addr_host_order(&ctx->store->addr);
            ctx->ns->state = PROTOCORE_DNS_READY;
            return;
        }
        if ((uint32_t)(Clock.ms - ctx->store->timer) >= PROTOCORE_DNS_TIMEOUT_MS)
        {
            ctx->store->busy = PROTO_FALSE;
            return;
        }
        ctx->ns->state = PROTOCORE_DNS_BUSY;
        return;
    }

    ctx->store->done = PROTO_FALSE;
    ctx->store->ok = PROTO_FALSE;
    DnsMarshal m;
    mem.set(&m, 0, sizeof(m));
    m.ctx = ctx;
    m.host = host;
    (void)protocore_net_call_marshal(dns_ask, &m.base);

    // Already held, so the callback ran inside the marshal and there is nothing to wait for.
    if (ctx->store->done)
    {
        if (!ctx->store->ok)
        {
            return;
        }
        ctx->ns->u32 = addr_host_order(&ctx->store->addr);
        ctx->ns->state = PROTOCORE_DNS_READY;
        return;
    }
    ctx->store->busy = PROTO_TRUE;
    ctx->store->timer = Clock.ms;
    ctx->ns->state = PROTOCORE_DNS_BUSY;
}

// Reports false and changes nothing. The nameserver list is the stack's, learned from DHCP.
static void resolver_set_server(struct ResolverInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}

#else // the portable resolver

// Take the query and nameserver borrows on first use, and seat the configured default in the latter.
static proto_bool client_bind(struct ResolverInternal *restrict ctx)
{
    if (span.has_storage(ctx->store->tx))
    {
        return PROTO_TRUE;
    }
    if (!name_bind(ctx))
    {
        return PROTO_FALSE;
    }
    ctx->store->tx = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX + 32);
    ctx->store->server = protocore_secure_persist_span(PROTOCORE_IP_STR_MAX);
    if (!span.has_storage(ctx->store->tx) || !span.has_storage(ctx->store->server))
    {
        return PROTO_FALSE;
    }
    size_t n = str.len(PROTOCORE_DNS_SERVER, ctx->store->server.cap);
    if (n >= ctx->store->server.cap)
    {
        return PROTO_FALSE;
    }
    raw.read(ctx->store->server.buf, PROTOCORE_DNS_SERVER, n);
    ctx->store->server.buf[n] = '\0';
    return PROTO_TRUE;
}

// Take a response, on the listener's drain. Always armed: busy is the sending side, so this runs
// whether or not a query is out. Anything that does not parse as an answer to the query in flight is
// ignored, and that query keeps waiting rather than failing on it.
static void dns_reply(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *arg)
{
    (void)peer;
    struct ResolverInternal *ctx = (struct ResolverInternal *)arg;
    if (ctx == NULL)
    {
        return;
    }
    uint32_t ip = 0;
    if (answer_read(ctx, data, len, ctx->store->id, &ip))
    {
        ctx->store->answer = ip;
        ctx->store->done = PROTO_TRUE;
    }
}

static void resolver_set_server(struct ResolverInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const char *ip = ctx->ns->server.ip;
    if (ip == NULL || !client_bind(ctx))
    {
        return;
    }
    protocore_ip probe = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = ip;
    Ip.args.out = &probe;
    Ip.parse(Ip.internal);
    if (!Ip.ok)
    {
        return;
    }
    size_t n = str.len(ip, ctx->store->server.cap);
    if (n >= ctx->store->server.cap)
    {
        return;
    }
    raw.read(ctx->store->server.buf, ip, n);
    ctx->store->server.buf[n] = '\0';
    ctx->ns->ok = PROTO_TRUE;
}

static void resolver_resolve(struct ResolverInternal *restrict ctx)
{
    ctx->ns->u32 = 0;
    ctx->ns->state = PROTOCORE_DNS_FAILED;
    const char *host = ctx->ns->query.host;
    if (host == NULL)
    {
        return;
    }

    protocore_ip literal = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = host;
    Ip.args.out = &literal;
    Ip.parse(Ip.internal);
    if (Ip.ok) // a dotted quad answers itself, no query
    {
        ctx->ns->u32 = ((uint32_t)literal.bytes[0] << 24) | ((uint32_t)literal.bytes[1] << 16) |
                       ((uint32_t)literal.bytes[2] << 8) | (uint32_t)literal.bytes[3];
        ctx->ns->state = PROTOCORE_DNS_READY;
        return;
    }

    // The query already out: the response landed on the listener's drain, or the timer ran out.
    if (ctx->store->busy)
    {
        if (ctx->store->done)
        {
            ctx->store->busy = PROTO_FALSE;
            ctx->ns->u32 = ctx->store->answer;
            ctx->ns->state = PROTOCORE_DNS_READY;
            return;
        }
        if ((uint32_t)(Clock.ms - ctx->store->timer) >= PROTOCORE_DNS_TIMEOUT_MS)
        {
            ctx->store->busy = PROTO_FALSE;
            return;
        }
        ctx->ns->state = PROTOCORE_DNS_BUSY;
        return;
    }

    protocore_ip server = {PROTOCORE_IP_NONE, {0}};
    if (!client_bind(ctx))
    {
        return;
    }
    Ip.args.text = (const char *)ctx->store->server.buf;
    Ip.args.out = &server;
    Ip.parse(Ip.internal);
    if (!Ip.ok)
    {
        return;
    }

    // The handler is given this handle back, so the drain reaches the query it is answering.
    UdpListener.port = PROTOCORE_DNS_CLIENT_PORT;
    UdpListener.bind.handler = dns_reply;
    UdpListener.bind.handler_ctx = ctx;
    UdpListener.listen(UdpListener.internal);
    if (!UdpListener.ok)
    {
        return;
    }

    // The ID ties the response to this query. It ticks, so two resolves in a row do not share one.
    ctx->store->id = (uint16_t)(Clock.ms | 1u);
    ctx->store->answer = 0;
    ctx->store->done = PROTO_FALSE;
    size_t n = question_build(ctx->store->tx.buf, ctx->store->tx.cap, ctx->store->id, host);
    if (n == 0)
    {
        return;
    }

    UdpListener.port = PROTOCORE_DNS_CLIENT_PORT;
    UdpListener.send_args.dst = &server;
    UdpListener.send_args.dst_port = PROTOCORE_DNS_PORT;
    UdpListener.send_args.data = ctx->store->tx.buf;
    UdpListener.send_args.len = n;
    UdpListener.sendto(UdpListener.internal);
    if (!UdpListener.ok)
    {
        return;
    }
    ctx->store->busy = PROTO_TRUE;
    ctx->store->timer = Clock.ms;
    ctx->ns->state = PROTOCORE_DNS_BUSY;
}

#endif // PROTOCORE_HAS_VENDOR_DNS_RESOLVER

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

static void resolver_classify(struct ResolverInternal *restrict ctx)
{
    ctx->ns->cls = ip_class(ctx->ns->addr.ip);
}

static void resolver_verify(struct ResolverInternal *restrict ctx)
{
    ctx->ns->ok = ip_plausible(ctx->ns->addr.ip);
}

static void resolver_query_build(struct ResolverInternal *restrict ctx)
{
    ctx->ns->n = question_build(ctx->ns->query.out, ctx->ns->query.cap, ctx->ns->query.id, ctx->ns->query.host);
}

static void resolver_answer_parse(struct ResolverInternal *restrict ctx)
{
    ctx->ns->u32 = 0;
    ctx->ns->ok = answer_read(ctx, ctx->ns->answer.pkt, ctx->ns->answer.len, ctx->ns->query.id, &ctx->ns->u32);
}

static void resolver_resolve_verified(struct ResolverInternal *restrict ctx)
{
    resolver_resolve(ctx);
    if (ctx->ns->state != PROTOCORE_DNS_READY)
    {
        return;
    }
    if (!ip_plausible(ctx->ns->u32))
    {
        ctx->ns->u32 = 0;
        ctx->ns->state = PROTOCORE_DNS_FAILED;
    }
}

static void resolver_busy(struct ResolverInternal *restrict ctx)
{
    ctx->ns->ok = ctx->store->busy;
}

// Designated, so a member's position in the struct does not decide what it binds to.
ResolverNs Resolver = {.classify = resolver_classify,
                       .verify = resolver_verify,
                       .query_build = resolver_query_build,
                       .answer_parse = resolver_answer_parse,
                       .resolve = resolver_resolve,
                       .resolve_verified = resolver_resolve_verified,
                       .busy = resolver_busy,
                       .set_server = resolver_set_server,
                       .internal = &s_resolver};

#endif // PROTOCORE_NEED_DNS_RESOLVER
