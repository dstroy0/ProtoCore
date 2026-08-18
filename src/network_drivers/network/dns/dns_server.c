// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_server.c
 * @brief The answering side of DNS (RFC 1035) - implementation. See dns_server.h.
 */

#include "network_drivers/network/dns/dns_server.h"
#include "mmgr/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protostr.h"  // str.len
#include "mmgr/rawmemcpy.h" // raw.read: the exact mover, for a destination inside a buffer
#include "protocore_config.h"

static uint8_t dns_wire_work[16]; // the borrow an entry takes; DnsWire never reads it

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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define DNS_SERVER_OFF_CTX 0u
static_assert(DNS_SERVER_OFF_CTX + sizeof(struct DnsServerStorage) <= PROTOCORE_DNS_SERVER_BORROW,
              "PROTOCORE_DNS_SERVER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define DNS_SERVER_CTX(w) ((struct DnsServerStorage *)(void *)((w) + DNS_SERVER_OFF_CTX))

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
    DnsWire.decode(dns_wire_work);
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
static uint32_t table_find(uint8_t *restrict work, const char *name)
{
    if (!name)
    {
        return 0;
    }
    for (size_t i = 0; i < DNS_SERVER_CTX(work)->count; i++)
    {
        DnsWire.cmp.a = DNS_SERVER_CTX(work)->names[i];
        DnsWire.cmp.b = name;
        DnsWire.eq(dns_wire_work);
        if (DnsWire.ok)
        {
            return DNS_SERVER_CTX(work)->ips[i];
        }
    }
    return 0;
}

// Frame a response to msg.query in msg.out and report its length in ns->n. Header fields are
// RFC 1035 sec 4.1.1, the question sec 4.1.2, the answer RR sec 4.1.3, the name pointer sec 4.1.4.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_DNS_SERVER_BORROW persistent bytes, or null while the pool was short
} DnsServerOwnCtx;
static DnsServerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_dns_server_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_DNS_SERVER_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void dns_build_response(uint8_t *restrict work)
{
    const uint8_t *query = DnsServer.msg.query;
    const size_t qlen = DnsServer.msg.qlen;
    uint8_t *out = DnsServer.msg.out;
    const size_t out_cap = DnsServer.msg.out_cap;

    DnsServer.n = 0;
    if (!query || !out || !DnsServer.ans.resolve || qlen < 12)
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
        DnsServer.n = 12;
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

    uint32_t ip = (qtype == 1) ? DnsServer.ans.resolve(name) : 0; // QTYPE 1 = A (RFC 1035 sec 3.2.2)
    if (!ip)
    {
        out[6] = 0x00;
        out[7] = 0x00;                    // ANCOUNT = 0
        out[3] = (qtype == 1) ? 0x03 : 0; // an A miss -> RCODE 3 Name Error; another QTYPE -> no error, no answer
        DnsServer.n = qend;
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
    out[n++] = (uint8_t)(DnsServer.ans.ttl >> 24);
    out[n++] = (uint8_t)(DnsServer.ans.ttl >> 16);
    out[n++] = (uint8_t)(DnsServer.ans.ttl >> 8);
    out[n++] = (uint8_t)DnsServer.ans.ttl;
    out[n++] = 0x00;
    out[n++] = 0x04; // RDLENGTH = 4
    out[n++] = (uint8_t)(ip >> 24);
    out[n++] = (uint8_t)(ip >> 16);
    out[n++] = (uint8_t)(ip >> 8);
    out[n++] = (uint8_t)ip; // RDATA: the ADDRESS (RFC 1035 sec 3.4.1)
    DnsServer.n = n;
}

// Record rec.name with the ADDRESS rec.a.rec.b.rec.c.rec.d; ok is false for an empty, absent, or
// over-long name, or a full table.
static void dns_add(uint8_t *restrict work)
{
    const char *name = DnsServer.rec.name;

    DnsServer.ok = PROTO_FALSE;
    if (!name || !name[0])
    {
        return;
    }
    size_t nlen = str.len(name, PROTOCORE_DNS_NAME_MAX);
    if (nlen >= PROTOCORE_DNS_NAME_MAX)
    {
        return;
    }
    if (DNS_SERVER_CTX(work)->count >= PROTOCORE_DNS_SERVER_MAX_RECORDS)
    {
        return;
    }
    raw.read(DNS_SERVER_CTX(work)->names[DNS_SERVER_CTX(work)->count], name, nlen + 1);
    DNS_SERVER_CTX(work)->ips[DNS_SERVER_CTX(work)->count] =
        ((uint32_t)DnsServer.rec.a << 24) | ((uint32_t)DnsServer.rec.b << 16) | ((uint32_t)DnsServer.rec.c << 8) |
        (uint32_t)DnsServer.rec.d;
    DNS_SERVER_CTX(work)->count++;
    DnsServer.ok = PROTO_TRUE;
}

static void dns_lookup(uint8_t *restrict work)
{
    DnsServer.ip = table_find(work, DnsServer.rec.name);
}

static void dns_clear(uint8_t *restrict work)
{
    DNS_SERVER_CTX(work)->count = 0;
}

uint32_t protocore_dns_server_resolve(const char *name)
{
    return table_find(protocore_dns_server_span(), name);
}

// One received datagram: build the response on the stage and hand it back to the sender
// (RFC 1035 sec 4.2.1). A build that reports 0 octets is answered with nothing.
static void dns_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *arg)
{
    (void)arg;
    uint8_t *work = protocore_dns_server_span();

    DnsServer.msg.query = data;
    DnsServer.msg.qlen = len;
    DnsServer.msg.out = DNS_SERVER_CTX(work)->tx;
    DnsServer.msg.out_cap = sizeof(DNS_SERVER_CTX(work)->tx);
    DnsServer.ans.ttl = PROTOCORE_DNS_SERVER_TTL;
    DnsServer.ans.resolve = protocore_dns_server_resolve;
    dns_build_response(work);
    if (DnsServer.n == 0)
    {
        return;
    }
    UdpListener.peer_args.peer = peer;
    UdpListener.send_args.data = DNS_SERVER_CTX(work)->tx;
    UdpListener.send_args.len = DnsServer.n;
    UdpListener.reply(protocore_udp_listener_span());
}

// Bind UDP port 53, the port a DNS message is carried to (RFC 1035 sec 4.2.1).
static void dns_begin(uint8_t *restrict work)
{
    UdpListener.port = 53;
    UdpListener.bind.handler = dns_udp_handler;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    DnsServer.ok = UdpListener.ok;
}

// Designated, so a member's position in the struct does not decide what it binds to.
DnsServerNs DnsServer = {
    .build_response = dns_build_response, .add = dns_add, .clear = dns_clear, .begin = dns_begin, .lookup = dns_lookup};

#endif // PROTOCORE_ENABLE_DNS_SERVER
