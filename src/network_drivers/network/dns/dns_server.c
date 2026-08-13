// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_server.c
 * @brief The answering side of DNS (RFC 1035) - implementation. See dns_server.h.
 */

#include "network_drivers/network/dns/dns_server.h"
#include "mmgr/protostr.h"  // str.len
#include "mmgr/rawmemcpy.h" // raw.read: the exact mover, for a destination inside a buffer
#include "protocore_config.h"

#if PROTOCORE_ENABLE_DNS_SERVER

#include "network_drivers/network/dns/dns_wire.h"        // the name codec both DNS halves read and write
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the port 53 bind and the reply

/**
 * @brief The name table's compile-time storage: the A records and the response stage.
 *
 * All of it BSS, so a record costs no heap and no response lands on a task stack. The stage holds
 * one response for the length of one handler call, and poll() runs the handler, so no two calls
 * reach it at once.
 *
 * @var DnsServerStorage::names  the owner names, NUL-terminated (RFC 1035 sec 3.4.1)
 * @var DnsServerStorage::ips    their ADDRESS values, host order, index-parallel to names
 * @var DnsServerStorage::count  records held
 * @var DnsServerStorage::tx     header + question + one A answer (RFC 1035 sec 4.1)
 */
struct DnsServerStorage
{
    char names[PROTOCORE_DNS_SERVER_MAX_RECORDS][PROTOCORE_DNS_NAME_MAX];
    uint32_t ips[PROTOCORE_DNS_SERVER_MAX_RECORDS];
    size_t count;
    uint8_t tx[PROTOCORE_DNS_NAME_MAX + 32];
};

/**
 * @brief The name table and the calls that reach it - what DnsServerNs points at.
 *
 * @var DnsServerInternal::store  the A records and the response stage
 * @var DnsServerInternal::ns     the handle a caller sets a call's members on
 */
struct DnsServerInternal
{
    struct DnsServerStorage *store;
    DnsServerNs *ns;
};

static struct DnsServerStorage s_store;

static struct DnsServerInternal s_dns = {.store = &s_store, .ns = &DnsServer};

// Parse the first question (RFC 1035 sec 4.1.2): write QNAME into @p name as a dotted string, set
// *qtype to QTYPE and *qend to the offset just past QTYPE and QCLASS. Returns false on a malformed
// or over-long question. The name is decoded with pointers refused: a question carries the first
// name in its message, so one has nothing earlier to point at (sec 4.1.4).
static proto_bool parse_question(const uint8_t *q, size_t qlen, char *name, size_t name_cap, uint16_t *qtype,
                                 size_t *qend)
{
    if (qlen < 12)
    {
        return PROTO_FALSE;
    }
    DnsWire.msg.pkt = q;
    DnsWire.msg.len = qlen;
    DnsWire.msg.off = 12;
    DnsWire.msg.out = name;
    DnsWire.msg.out_cap = name_cap;
    DnsWire.msg.allow_ptr = PROTO_FALSE;
    DnsWire.decode(DnsWire.internal);
    if (!DnsWire.ok)
    {
        return PROTO_FALSE;
    }
    size_t i = DnsWire.next;
    if (i + 4 > qlen)
    {
        return PROTO_FALSE;
    }
    *qtype = (uint16_t)((q[i] << 8) | q[i + 1]);
    *qend = i + 4;
    return PROTO_TRUE;
}

// The ADDRESS recorded for @p name, host order, 0 when absent. Names compare ignoring ASCII case
// (RFC 1035 sec 2.3.3) through DnsWire.eq. Reads the table and sets no member of DnsServer, so it
// serves both the lookup call and the resolver callback a response is being framed around.
static uint32_t table_find(struct DnsServerInternal *restrict ctx, const char *name)
{
    if (!name)
    {
        return 0;
    }
    for (size_t i = 0; i < ctx->store->count; i++)
    {
        DnsWire.cmp.a = ctx->store->names[i];
        DnsWire.cmp.b = name;
        DnsWire.eq(DnsWire.internal);
        if (DnsWire.ok)
        {
            return ctx->store->ips[i];
        }
    }
    return 0;
}

// Frame a response to msg.query in msg.out and report its length in ns->n. Header fields are
// RFC 1035 sec 4.1.1, the question sec 4.1.2, the answer RR sec 4.1.3, the name pointer sec 4.1.4.
static void dns_build_response(struct DnsServerInternal *restrict ctx)
{
    const uint8_t *query = ctx->ns->msg.query;
    const size_t qlen = ctx->ns->msg.qlen;
    uint8_t *out = ctx->ns->msg.out;
    const size_t out_cap = ctx->ns->msg.out_cap;

    ctx->ns->n = 0;
    if (!query || !out || !ctx->ns->ans.resolve || qlen < 12)
    {
        return;
    }

    // A valid header with an OPCODE other than 0 QUERY (IQUERY, STATUS, ...): answer RCODE 4
    // Not Implemented, header only.
    uint8_t opcode = (uint8_t)((query[2] >> 3) & 0xF);
    if (opcode != 0)
    {
        if (out_cap < 12)
        {
            return;
        }
        raw.read(out, query, 12);
        out[2] = (uint8_t)(0x84 | (query[2] & 0x01)); // QR=1, AA=1, RD copied
        out[3] = 0x04;                                // RCODE = 4 Not Implemented
        out[6] = out[7] = 0;                          // ANCOUNT 0
        out[8] = out[9] = out[10] = out[11] = 0;      // NSCOUNT / ARCOUNT 0
        ctx->ns->n = 12;
        return;
    }

    char name[PROTOCORE_DNS_NAME_MAX];
    uint16_t qtype = 0;
    size_t qend = 0;
    if (!parse_question(query, qlen, name, sizeof(name), &qtype, &qend))
    {
        return; // malformed question - drop it rather than echo garbage back
    }

    if (qend > out_cap)
    {
        return;
    }
    raw.read(out, query, qend);                   // header + question (preserves ID and the question octets)
    out[2] = (uint8_t)(0x84 | (query[2] & 0x01)); // QR=1, OPCODE=0, AA=1, RD copied
    out[3] = 0x00;                                // RA=0, RCODE=0 No Error
    out[4] = 0x00;
    out[5] = 0x01;                           // QDCOUNT = 1
    out[8] = out[9] = out[10] = out[11] = 0; // NSCOUNT / ARCOUNT = 0

    uint32_t ip = (qtype == 1) ? ctx->ns->ans.resolve(name) : 0; // QTYPE 1 = A (RFC 1035 sec 3.2.2)
    if (!ip)
    {
        out[6] = 0x00;
        out[7] = 0x00;                    // ANCOUNT = 0
        out[3] = (qtype == 1) ? 0x03 : 0; // an A miss -> RCODE 3 Name Error; another QTYPE -> no error, no answer
        ctx->ns->n = qend;
        return;
    }

    if (qend + 16 > out_cap)
    {
        return;
    }
    out[6] = 0x00;
    out[7] = 0x01; // ANCOUNT = 1
    size_t n = qend;
    out[n++] = 0xC0; // NAME: pointer to the question at offset 0x000C (RFC 1035 sec 4.1.4)
    out[n++] = 0x0C;
    out[n++] = 0x00;
    out[n++] = 0x01; // TYPE = A
    out[n++] = 0x00;
    out[n++] = 0x01; // CLASS = IN
    out[n++] = (uint8_t)(ctx->ns->ans.ttl >> 24);
    out[n++] = (uint8_t)(ctx->ns->ans.ttl >> 16);
    out[n++] = (uint8_t)(ctx->ns->ans.ttl >> 8);
    out[n++] = (uint8_t)ctx->ns->ans.ttl;
    out[n++] = 0x00;
    out[n++] = 0x04; // RDLENGTH = 4
    out[n++] = (uint8_t)(ip >> 24);
    out[n++] = (uint8_t)(ip >> 16);
    out[n++] = (uint8_t)(ip >> 8);
    out[n++] = (uint8_t)ip; // RDATA: the ADDRESS (RFC 1035 sec 3.4.1)
    ctx->ns->n = n;
}

// Record rec.name with the ADDRESS rec.a.rec.b.rec.c.rec.d; ok is false for an empty, absent, or
// over-long name, or a full table.
static void dns_add(struct DnsServerInternal *restrict ctx)
{
    const char *name = ctx->ns->rec.name;

    ctx->ns->ok = PROTO_FALSE;
    if (!name || !name[0])
    {
        return;
    }
    size_t nlen = str.len(name, PROTOCORE_DNS_NAME_MAX);
    if (nlen >= PROTOCORE_DNS_NAME_MAX)
    {
        return;
    }
    if (ctx->store->count >= PROTOCORE_DNS_SERVER_MAX_RECORDS)
    {
        return;
    }
    raw.read(ctx->store->names[ctx->store->count], name, nlen + 1);
    ctx->store->ips[ctx->store->count] = ((uint32_t)ctx->ns->rec.a << 24) | ((uint32_t)ctx->ns->rec.b << 16) |
                                         ((uint32_t)ctx->ns->rec.c << 8) | (uint32_t)ctx->ns->rec.d;
    ctx->store->count++;
    ctx->ns->ok = PROTO_TRUE;
}

static void dns_lookup(struct DnsServerInternal *restrict ctx)
{
    ctx->ns->ip = table_find(ctx, ctx->ns->rec.name);
}

static void dns_clear(struct DnsServerInternal *restrict ctx)
{
    ctx->store->count = 0;
}

uint32_t protocore_dns_server_resolve(const char *name)
{
    return table_find(&s_dns, name);
}

// One received datagram: build the response on the stage and hand it back to the sender
// (RFC 1035 sec 4.2.1). A build that reports 0 octets is answered with nothing.
static void dns_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *arg)
{
    (void)arg;
    struct DnsServerInternal *ctx = &s_dns;

    ctx->ns->msg.query = data;
    ctx->ns->msg.qlen = len;
    ctx->ns->msg.out = ctx->store->tx;
    ctx->ns->msg.out_cap = sizeof(ctx->store->tx);
    ctx->ns->ans.ttl = PROTOCORE_DNS_SERVER_TTL;
    ctx->ns->ans.resolve = protocore_dns_server_resolve;
    dns_build_response(ctx);
    if (ctx->ns->n == 0)
    {
        return;
    }
    UdpListener.peer_args.peer = peer;
    UdpListener.send_args.data = ctx->store->tx;
    UdpListener.send_args.len = ctx->ns->n;
    UdpListener.reply(UdpListener.internal);
}

// Bind UDP port 53, the port a DNS message is carried to (RFC 1035 sec 4.2.1).
static void dns_begin(struct DnsServerInternal *restrict ctx)
{
    UdpListener.port = 53;
    UdpListener.bind.handler = dns_udp_handler;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);
    ctx->ns->ok = UdpListener.ok;
}

// Designated, so a member's position in the struct does not decide what it binds to.
DnsServerNs DnsServer = {.build_response = dns_build_response,
                         .add = dns_add,
                         .clear = dns_clear,
                         .begin = dns_begin,
                         .lookup = dns_lookup,
                         .internal = &s_dns};

#endif // PROTOCORE_ENABLE_DNS_SERVER
