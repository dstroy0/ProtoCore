// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.c
 * @brief Authoritative DNS server - implementation. See dns_server.h.
 */

#include "network_drivers/network/dns/dns_server.h"
#include "mmgr/protostr.h"  // str.len
#include "mmgr/rawmemcpy.h" // proto_raw_read: the exact mover, for a destination inside a buffer
#include "protocore_config.h"

#if PC_ENABLE_DNS_SERVER

#include "network_drivers/network/dns/dns_wire.h" // the name codec both DNS halves read and write
#include "network_drivers/transport/udp.h"        // Udp.listener: the port 53 bind and the reply

// Parse the first question: write the dotted name into @p name, set *qtype and *qend (the
// byte just past QTYPE/QCLASS). Returns false on a malformed or over-long question. The name is
// decoded with pointers refused: a question is the first name in its message, so one has nothing
// earlier to point at.
static proto_bool parse_question(const uint8_t *q, size_t qlen, char *name, size_t name_cap, uint16_t *qtype,
                                 size_t *qend)
{
    if (qlen < 12)
    {
        return PROTO_FALSE;
    }
    size_t i = 0;
    if (!pc_dns_name_decode(q, qlen, 12, name, name_cap, &i, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    if (i + 4 > qlen)
    {
        return PROTO_FALSE;
    }
    *qtype = (uint16_t)((q[i] << 8) | q[i + 1]);
    *qend = i + 4;
    return PROTO_TRUE;
}

static size_t build_response(const uint8_t *query, size_t qlen, uint32_t ttl, DnsResolveFn resolve, uint8_t *out,
                             size_t out_cap)
{
    if (!query || !out || !resolve || qlen < 12)
    {
        return 0;
    }

    // A valid header but a non-standard query (IQUERY / STATUS / ...): answer NOTIMP.
    uint8_t opcode = (uint8_t)((query[2] >> 3) & 0xF);
    if (opcode != 0)
    {
        if (out_cap < 12)
        {
            return 0;
        }
        proto_raw_read(out, query, 12);
        out[2] = (uint8_t)(0x84 | (query[2] & 0x01)); // QR=1, AA=1, RD copied
        out[3] = 0x04;                                // NOTIMP
        out[6] = out[7] = 0;                          // ANCOUNT 0
        out[8] = out[9] = out[10] = out[11] = 0;      // NS/AR 0
        return 12;
    }

    char name[PC_DNS_NAME_MAX];
    uint16_t qtype = 0;
    size_t qend = 0;
    if (!parse_question(query, qlen, name, sizeof(name), &qtype, &qend))
    {
        return 0; // malformed question - drop it rather than echo garbage back
    }

    if (qend > out_cap)
    {
        return 0;
    }
    proto_raw_read(out, query, qend);             // header + question (preserves id + question bytes)
    out[2] = (uint8_t)(0x84 | (query[2] & 0x01)); // QR=1, OPCODE=0, AA=1, RD copied
    out[3] = 0x00;                                // RA=0, RCODE=0
    out[4] = 0x00;
    out[5] = 0x01;                           // QDCOUNT = 1
    out[8] = out[9] = out[10] = out[11] = 0; // NSCOUNT / ARCOUNT = 0

    uint32_t ip = (qtype == 1) ? resolve(name) : 0; // 1 = A record
    if (!ip)
    {
        out[6] = 0x00;
        out[7] = 0x00;                    // ANCOUNT = 0
        out[3] = (qtype == 1) ? 0x03 : 0; // A miss -> NXDOMAIN; other type -> no error, no answer
        return qend;
    }

    if (qend + 16 > out_cap)
    {
        return 0;
    }
    out[6] = 0x00;
    out[7] = 0x01; // ANCOUNT = 1
    size_t n = qend;
    out[n++] = 0xC0; // name: compression pointer to the question at offset 0x000C
    out[n++] = 0x0C;
    out[n++] = 0x00;
    out[n++] = 0x01; // TYPE = A
    out[n++] = 0x00;
    out[n++] = 0x01; // CLASS = IN
    out[n++] = (uint8_t)(ttl >> 24);
    out[n++] = (uint8_t)(ttl >> 16);
    out[n++] = (uint8_t)(ttl >> 8);
    out[n++] = (uint8_t)ttl;
    out[n++] = 0x00;
    out[n++] = 0x04; // RDLENGTH = 4
    out[n++] = (uint8_t)(ip >> 24);
    out[n++] = (uint8_t)(ip >> 16);
    out[n++] = (uint8_t)(ip >> 8);
    out[n++] = (uint8_t)ip;
    return n;
}

// ---------------------------------------------------------------------------
// Built-in A-record table (host-testable; used by begin()).
// ---------------------------------------------------------------------------

// All DNS-server state, owned by one instance (internal linkage): the A-record table and the
// response stage, grouped so it is one named owner, unreachable from other translation units.
// The stage holds one answer for the length of one handler call, and poll() runs the handler, so
// no two calls reach it at once.
typedef struct
{
    char names[PC_DNS_SERVER_MAX_RECORDS][PC_DNS_NAME_MAX];
    uint32_t ips[PC_DNS_SERVER_MAX_RECORDS];
    size_t count;
    uint8_t tx[PC_DNS_NAME_MAX + 32]; ///< header + question + one A answer
} DnsSrvCtx;
static DnsSrvCtx s_dns;

static proto_bool add(const char *name, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    if (!name || !name[0])
    {
        return PROTO_FALSE;
    }
    size_t nlen = str.len(name, PC_DNS_NAME_MAX);
    if (nlen >= PC_DNS_NAME_MAX)
    {
        return PROTO_FALSE;
    }
    if (s_dns.count >= PC_DNS_SERVER_MAX_RECORDS)
    {
        return PROTO_FALSE;
    }
    proto_raw_read(s_dns.names[s_dns.count], name, nlen + 1);
    s_dns.ips[s_dns.count] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    s_dns.count++;
    return PROTO_TRUE;
}

static uint32_t lookup(const char *name)
{
    if (!name)
    {
        return 0;
    }
    for (size_t i = 0; i < s_dns.count; i++)
    {
        if (pc_dns_name_eq(s_dns.names[i], name))
        {
            return s_dns.ips[i];
        }
    }
    return 0;
}

static void clear()
{
    s_dns.count = 0;
}

static void udp_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    size_t n = build_response(data, len, PC_DNS_SERVER_TTL, lookup, s_dns.tx, sizeof(s_dns.tx));
    if (n != 0)
    {
        (void)Udp.listener->reply(peer, s_dns.tx, n);
    }
}

static proto_bool begin()
{
    return Udp.listener->listen(53, udp_handler, NULL);
}

// Designated, so a member's position in the struct does not decide what it binds to.
const DnsServerNs DnsServer = {
    .build_response = build_response, .add = add, .clear = clear, .begin = begin, .lookup = lookup};

#endif // PC_ENABLE_DNS_SERVER
