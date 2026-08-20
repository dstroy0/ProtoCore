// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_resolver.c
 * @brief The IPv4 classifier, the RFC 1035 query and answer codecs, and the resolve.
 *        See dns_resolver.h.
 */

#include "network_drivers/network/dns/dns_resolver/dns_resolver.h"
#include "mmgr/protomem/protomem.h"

#if PROTOCORE_ENABLE_DNS_RESOLVER

#include "mmgr/secure/secure.h"                            // protocore_secure_persist_span: this module's storage
#include "network_drivers/network/dns/dns_wire/dns_wire.h" // the name codec both DNS halves share
#include "server/clock/clock.h"                            // Clock.millis: the deadline the resolve waits to

// --- the program's shared state, beside the namespace not on it -------------

PROTOCORE_BEGIN_DECLS

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_DNS_RESOLVER_BORROW persistent bytes
} ResolverOwnCtx;
static ResolverOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_dns_resolver_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_DNS_RESOLVER_BORROW).buf;
    }
    return s_own.span;
}

#if PROTOCORE_HAS_VENDOR_DNS_RESOLVER
#include "config/platform/platform.h" // the platform's own resolver, under our names
#else
#include "mmgr/protostr/protostr.h"                      // str: the bounded-run walks
#include "mmgr/rawmemcpy/rawmemcpy.h"                    // raw.read: the server address moves whole
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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define DNS_RESOLVER_OFF_CTX 0u
static_assert(DNS_RESOLVER_OFF_CTX + sizeof(struct ResolverStorage) <= PROTOCORE_DNS_RESOLVER_BORROW,
              "PROTOCORE_DNS_RESOLVER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define DNS_RESOLVER_CTX(w) ((struct ResolverStorage *)(void *)((w) + DNS_RESOLVER_OFF_CTX))

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
    DnsWireV.text.dotted = host;
    DnsWireV.text.out = out + n;
    DnsWireV.text.out_cap = cap - n;
    DnsWire.encode(protocore_dns_resolver_span());
    size_t w = DnsWireV.n;
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
static proto_bool name_bind(uint8_t *restrict work)
{
    if (span.has_storage(DNS_RESOLVER_CTX(work)->name))
    {
        return PROTO_TRUE;
    }
    DNS_RESOLVER_CTX(work)->name = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX);
    return span.has_storage(DNS_RESOLVER_CTX(work)->name);
}

// Read the first A record out of @p pkt into @p out_ip, host order. Accepts the message only when
// its ID is @p id, QR is set, and RCODE is 0 (RFC 1035 sec 4.1.1, RFC 5452 sec 9.1).
static proto_bool answer_read(uint8_t *restrict work, const uint8_t *pkt, size_t len, uint16_t id, uint32_t *out_ip)
{
    if (pkt == NULL || out_ip == NULL || len < PROTOCORE_DNS_HDR_LEN || !name_bind(work))
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
    char *name = (char *)DNS_RESOLVER_CTX(work)->name.buf;
    for (uint16_t q = 0; q < qd; q++)
    {
        DnsWireV.msg.pkt = pkt;
        DnsWireV.msg.len = len;
        DnsWireV.msg.off = off;
        DnsWireV.msg.out = name;
        DnsWireV.msg.out_cap = DNS_RESOLVER_CTX(work)->name.cap;
        DnsWireV.msg.allow_ptr = PROTO_TRUE;
        DnsWire.decode(work);
        if (!DnsWireV.ok)
        {
            return PROTO_FALSE;
        }
        off = DnsWireV.next;
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
        DnsWireV.msg.pkt = pkt;
        DnsWireV.msg.len = len;
        DnsWireV.msg.off = off;
        DnsWireV.msg.out = name;
        DnsWireV.msg.out_cap = DNS_RESOLVER_CTX(work)->name.cap;
        DnsWireV.msg.allow_ptr = PROTO_TRUE;
        DnsWire.decode(work);
        if (!DnsWireV.ok)
        {
            return PROTO_FALSE;
        }
        off = DnsWireV.next;
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

// The one time source (server/clock/clock.h). Clock.ms is where the last reading landed, so a
// caller that only reads it measures against whichever instant something else stamped. Take the
// reading, then report it. Above the split: both arms wait to a deadline.
static uint32_t dns_now(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

#if PROTOCORE_HAS_VENDOR_DNS_RESOLVER

// The marshal record: the name to resolve and the borrow its outcome lands in, carried onto the
// stack's own thread.
typedef struct
{
    protocore_net_call base;
    uint8_t *work; ///< the borrow this ask reads its region out of
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
    // The stack fixes this signature, so the borrow rides across as the callback's argument.
    uint8_t *work = (uint8_t *)arg;
    if (addr != NULL)
    {
        DNS_RESOLVER_CTX(work)->addr = *addr;
        DNS_RESOLVER_CTX(work)->ok = PROTO_TRUE;
    }
    DNS_RESOLVER_CTX(work)->done = PROTO_TRUE;
}

// The ask, on the stack's own thread. A name the stack already holds completes inside this call.
static protocore_net_err dns_ask(protocore_net_call *c)
{
    DnsMarshal *m = (DnsMarshal *)c;
    protocore_net_err e = protocore_net_dns_resolve(m->host, &DNS_RESOLVER_CTX(m->work)->addr, dns_found, m->work);
    if (e == PROTOCORE_NET_OK) // already held
    {
        DNS_RESOLVER_CTX(m->work)->ok = PROTO_TRUE;
        DNS_RESOLVER_CTX(m->work)->done = PROTO_TRUE;
    }
    else if (e != PROTOCORE_NET_ERR_INPROGRESS) // hard failure
    {
        DNS_RESOLVER_CTX(m->work)->done = PROTO_TRUE;
    }
    return PROTOCORE_NET_OK;
}

void protocore_resolver_resolve(uint8_t *restrict work)
{
    ResolverV.u32 = 0;
    ResolverV.state = PROTOCORE_DNS_FAILED;
    const char *host = ResolverV.query.host;
    if (host == NULL)
    {
        return;
    }

    protocore_net_ip literal;
    if (protocore_net_ip_parse(host, &literal)) // dotted-quad fast path, no query
    {
        ResolverV.u32 = addr_host_order(&literal);
        ResolverV.state = PROTOCORE_DNS_READY;
        return;
    }

    // The query already out: the stack's callback ran, or the timer ran out.
    if (DNS_RESOLVER_CTX(work)->busy)
    {
        if (DNS_RESOLVER_CTX(work)->done)
        {
            DNS_RESOLVER_CTX(work)->busy = PROTO_FALSE;
            if (!DNS_RESOLVER_CTX(work)->ok)
            {
                return;
            }
            ResolverV.u32 = addr_host_order(&DNS_RESOLVER_CTX(work)->addr);
            ResolverV.state = PROTOCORE_DNS_READY;
            return;
        }
        if ((uint32_t)(dns_now() - DNS_RESOLVER_CTX(work)->timer) >= PROTOCORE_DNS_TIMEOUT_MS)
        {
            DNS_RESOLVER_CTX(work)->busy = PROTO_FALSE;
            return;
        }
        ResolverV.state = PROTOCORE_DNS_BUSY;
        return;
    }

    DNS_RESOLVER_CTX(work)->done = PROTO_FALSE;
    DNS_RESOLVER_CTX(work)->ok = PROTO_FALSE;
    DnsMarshal m;
    mem.set(&m, 0, sizeof(m));
    m.work = work;
    m.host = host;
    (void)protocore_net_call_marshal(dns_ask, &m.base);

    // Already held, so the callback ran inside the marshal and there is nothing to wait for.
    if (DNS_RESOLVER_CTX(work)->done)
    {
        if (!DNS_RESOLVER_CTX(work)->ok)
        {
            return;
        }
        ResolverV.u32 = addr_host_order(&DNS_RESOLVER_CTX(work)->addr);
        ResolverV.state = PROTOCORE_DNS_READY;
        return;
    }
    DNS_RESOLVER_CTX(work)->busy = PROTO_TRUE;
    DNS_RESOLVER_CTX(work)->timer = dns_now();
    ResolverV.state = PROTOCORE_DNS_BUSY;
}

// Reports false and changes nothing. The nameserver list is the stack's, learned from DHCP.
void protocore_resolver_set_server(uint8_t *restrict work)
{
    (void)work;
    ResolverV.ok = PROTO_FALSE;
}

#else // the portable resolver

// Take the query and nameserver borrows on first use, and seat the configured default in the latter.
static proto_bool client_bind(uint8_t *restrict work)
{
    if (span.has_storage(DNS_RESOLVER_CTX(work)->tx))
    {
        return PROTO_TRUE;
    }
    if (!name_bind(work))
    {
        return PROTO_FALSE;
    }
    DNS_RESOLVER_CTX(work)->tx = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX + 32);
    DNS_RESOLVER_CTX(work)->server = protocore_secure_persist_span(PROTOCORE_IP_STR_MAX);
    if (!span.has_storage(DNS_RESOLVER_CTX(work)->tx) || !span.has_storage(DNS_RESOLVER_CTX(work)->server))
    {
        return PROTO_FALSE;
    }
    size_t n = str.len(PROTOCORE_DNS_SERVER, DNS_RESOLVER_CTX(work)->server.cap);
    if (n >= DNS_RESOLVER_CTX(work)->server.cap)
    {
        return PROTO_FALSE;
    }
    raw.read(DNS_RESOLVER_CTX(work)->server.buf, PROTOCORE_DNS_SERVER, n);
    DNS_RESOLVER_CTX(work)->server.buf[n] = '\0';
    return PROTO_TRUE;
}

// Take a response, on the listener's drain. Always armed: busy is the sending side, so this runs
// whether or not a query is out. Anything that does not parse as an answer to the query in flight is
// ignored, and that query keeps waiting rather than failing on it.
static void dns_reply(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *arg)
{
    (void)peer;
    // UdpListener fixes this signature and hands back whatever listen was given, which is the
    // borrow the query in flight lives in.
    uint8_t *work = (uint8_t *)arg;
    uint32_t ip = 0;
    if (answer_read(work, data, len, DNS_RESOLVER_CTX(work)->id, &ip))
    {
        DNS_RESOLVER_CTX(work)->answer = ip;
        DNS_RESOLVER_CTX(work)->done = PROTO_TRUE;
    }
}

void protocore_resolver_set_server(uint8_t *restrict work)
{
    ResolverV.ok = PROTO_FALSE;
    const char *ip = ResolverV.server.ip;
    if (ip == NULL || !client_bind(work))
    {
        return;
    }
    protocore_ip probe = {PROTOCORE_IP_NONE, {0}};
    IpV.args.text = ip;
    IpV.args.out = &probe;
    Ip.parse(work);
    if (!IpV.ok)
    {
        return;
    }
    size_t n = str.len(ip, DNS_RESOLVER_CTX(work)->server.cap);
    if (n >= DNS_RESOLVER_CTX(work)->server.cap)
    {
        return;
    }
    raw.read(DNS_RESOLVER_CTX(work)->server.buf, ip, n);
    DNS_RESOLVER_CTX(work)->server.buf[n] = '\0';
    ResolverV.ok = PROTO_TRUE;
}

void protocore_resolver_resolve(uint8_t *restrict work)
{
    ResolverV.u32 = 0;
    ResolverV.state = PROTOCORE_DNS_FAILED;
    const char *host = ResolverV.query.host;
    if (host == NULL)
    {
        return;
    }

    protocore_ip literal = {PROTOCORE_IP_NONE, {0}};
    IpV.args.text = host;
    IpV.args.out = &literal;
    Ip.parse(work);
    if (IpV.ok) // a dotted quad answers itself, no query
    {
        ResolverV.u32 = ((uint32_t)literal.bytes[0] << 24) | ((uint32_t)literal.bytes[1] << 16) |
                        ((uint32_t)literal.bytes[2] << 8) | (uint32_t)literal.bytes[3];
        ResolverV.state = PROTOCORE_DNS_READY;
        return;
    }

    // The query already out: the response landed on the listener's drain, or the timer ran out.
    if (DNS_RESOLVER_CTX(work)->busy)
    {
        if (DNS_RESOLVER_CTX(work)->done)
        {
            DNS_RESOLVER_CTX(work)->busy = PROTO_FALSE;
            ResolverV.u32 = DNS_RESOLVER_CTX(work)->answer;
            ResolverV.state = PROTOCORE_DNS_READY;
            return;
        }
        if ((uint32_t)(dns_now() - DNS_RESOLVER_CTX(work)->timer) >= PROTOCORE_DNS_TIMEOUT_MS)
        {
            DNS_RESOLVER_CTX(work)->busy = PROTO_FALSE;
            return;
        }
        ResolverV.state = PROTOCORE_DNS_BUSY;
        return;
    }

    protocore_ip server = {PROTOCORE_IP_NONE, {0}};
    if (!client_bind(work))
    {
        return;
    }
    IpV.args.text = (const char *)DNS_RESOLVER_CTX(work)->server.buf;
    IpV.args.out = &server;
    Ip.parse(work);
    if (!IpV.ok)
    {
        return;
    }

    // The handler is given these bytes back, so the drain reaches the query it is answering.
    UdpListenerV.port = PROTOCORE_DNS_CLIENT_PORT;
    UdpListenerV.bind.handler = dns_reply;
    UdpListenerV.bind.handler_ctx = work;
    UdpListener.listen(protocore_udp_listener_span());
    if (!UdpListenerV.ok)
    {
        return;
    }

    // The ID ties the response to this query. It ticks, so two resolves in a row do not share one.
    DNS_RESOLVER_CTX(work)->id = (uint16_t)(dns_now() | 1u);
    DNS_RESOLVER_CTX(work)->answer = 0;
    DNS_RESOLVER_CTX(work)->done = PROTO_FALSE;
    size_t n = question_build(DNS_RESOLVER_CTX(work)->tx.buf, DNS_RESOLVER_CTX(work)->tx.cap,
                              DNS_RESOLVER_CTX(work)->id, host);
    if (n == 0)
    {
        return;
    }

    UdpListenerV.port = PROTOCORE_DNS_CLIENT_PORT;
    UdpListenerV.send_args.dst = &server;
    UdpListenerV.send_args.dst_port = PROTOCORE_DNS_PORT;
    UdpListenerV.send_args.data = DNS_RESOLVER_CTX(work)->tx.buf;
    UdpListenerV.send_args.len = n;
    UdpListener.sendto(protocore_udp_listener_span());
    if (!UdpListenerV.ok)
    {
        return;
    }
    DNS_RESOLVER_CTX(work)->busy = PROTO_TRUE;
    DNS_RESOLVER_CTX(work)->timer = dns_now();
    ResolverV.state = PROTOCORE_DNS_BUSY;
}

#endif // PROTOCORE_HAS_VENDOR_DNS_RESOLVER

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

void protocore_resolver_classify(uint8_t *restrict work)
{
    (void)work;
    ResolverV.cls = ip_class(ResolverV.addr.ip);
}

void protocore_resolver_verify(uint8_t *restrict work)
{
    (void)work;
    ResolverV.ok = ip_plausible(ResolverV.addr.ip);
}

void protocore_resolver_query_build(uint8_t *restrict work)
{
    (void)work;
    ResolverV.n = question_build(ResolverV.query.out, ResolverV.query.cap, ResolverV.query.id, ResolverV.query.host);
}

void protocore_resolver_answer_parse(uint8_t *restrict work)
{
    ResolverV.u32 = 0;
    ResolverV.ok = answer_read(work, ResolverV.answer.pkt, ResolverV.answer.len, ResolverV.query.id, &ResolverV.u32);
}

void protocore_resolver_resolve_verified(uint8_t *restrict work)
{
    protocore_resolver_resolve(work);
    if (ResolverV.state != PROTOCORE_DNS_READY)
    {
        return;
    }
    if (!ip_plausible(ResolverV.u32))
    {
        ResolverV.u32 = 0;
        ResolverV.state = PROTOCORE_DNS_FAILED;
    }
}

void protocore_resolver_busy(uint8_t *restrict work)
{
    ResolverV.ok = DNS_RESOLVER_CTX(work)->busy;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
ResolverVars ResolverV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DNS_RESOLVER
