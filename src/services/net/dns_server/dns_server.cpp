// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_server.cpp
 * @brief Authoritative DNS server - implementation. See dns_server.h.
 */

#include "services/net/dns_server/dns_server.h"
#include "protocore_config.h"

#if PC_ENABLE_DNS_SERVER

#include <string.h> // memcpy, strlen

#if PROTOCORE_HOT
#include "network_drivers/transport/udp.h"
#endif
// Case-insensitive ASCII string equality (DNS names are case-insensitive).
static proto_bool ci_eq(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
        {
            ca += 32;
        }
        if (cb >= 'A' && cb <= 'Z')
        {
            cb += 32;
        }
        if (ca != cb)
        {
            return PROTO_FALSE;
        }
        a++;
        b++;
    }
    return *a == *b;
}

// Parse the first question: write the dotted name into @p name, set *qtype and *qend (the
// byte just past QTYPE/QCLASS). Returns false on a malformed or over-long question.
static proto_bool parse_question(const uint8_t *q, size_t qlen, char *name, size_t name_cap, uint16_t *qtype,
                                 size_t *qend)
{
    // GCOVR_EXCL_START  the sole caller (pc_dns_server_build_response) already rejects qlen < 12
    if (qlen < 12)
    {
        return PROTO_FALSE;
    }
    // GCOVR_EXCL_STOP
    size_t i = 12;
    size_t n = 0;
    for (;;)
    {
        if (i >= qlen)
        {
            return PROTO_FALSE;
        }
        uint8_t len = q[i++];
        if (len == 0)
        {
            break;
        }
        if (len & 0xC0) // a compression pointer is illegal inside a question
        {
            return PROTO_FALSE;
        }
        if (i + len > qlen)
        {
            return PROTO_FALSE;
        }
        if (n)
        {
            if (n + 1 >= name_cap)
            {
                return PROTO_FALSE;
            }
            name[n++] = '.';
        }
        for (uint8_t k = 0; k < len; k++)
        {
            if (n + 1 >= name_cap)
            {
                return PROTO_FALSE;
            }
            name[n++] = (char)q[i++];
        }
    }
    name[n] = '\0';
    if (i + 4 > qlen)
    {
        return PROTO_FALSE;
    }
    *qtype = (uint16_t)((q[i] << 8) | q[i + 1]);
    *qend = i + 4;
    return PROTO_TRUE;
}

size_t pc_dns_server_build_response(const uint8_t *query, size_t qlen, uint32_t ttl, DnsResolveFn resolve, uint8_t *out,
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
        memcpy(out, query, 12);
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
    memcpy(out, query, qend);                     // header + question (preserves id + question bytes)
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
// Built-in A-record table (host-testable; used by pc_dns_server_begin()).
// ---------------------------------------------------------------------------

// All DNS-server state, owned by one instance (internal linkage): the A-record table,
// grouped so it is one named owner, unreachable from other translation units.
typedef struct
{
    char names[PC_DNS_SERVER_MAX_RECORDS][PC_DNS_NAME_MAX];
    uint32_t ips[PC_DNS_SERVER_MAX_RECORDS];
    size_t count = 0;
} DnsSrvCtx;
static DnsSrvCtx s_dns;

proto_bool pc_dns_server_add(const char *name, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    if (!name || !name[0])
    {
        return PROTO_FALSE;
    }
    size_t nlen = strnlen(name, PC_DNS_NAME_MAX);
    if (nlen >= PC_DNS_NAME_MAX)
    {
        return PROTO_FALSE;
    }
    if (s_dns.count >= PC_DNS_SERVER_MAX_RECORDS)
    {
        return PROTO_FALSE;
    }
    memcpy(s_dns.names[s_dns.count], name, nlen + 1);
    s_dns.ips[s_dns.count] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    s_dns.count++;
    return PROTO_TRUE;
}

uint32_t pc_dns_server_lookup(const char *name)
{
    if (!name)
    {
        return 0;
    }
    for (size_t i = 0; i < s_dns.count; i++)
    {
        if (ci_eq(s_dns.names[i], name))
        {
            return s_dns.ips[i];
        }
    }
    return 0;
}

void pc_dns_server_clear()
{
    s_dns.count = 0;
}

#if PROTOCORE_HOT

static void pc_dns_server_udp_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    uint8_t resp[PC_DNS_NAME_MAX + 32]; // header + question + one A answer
    size_t n = pc_dns_server_build_response(data, len, PC_DNS_SERVER_TTL, pc_dns_server_lookup, resp, sizeof(resp));
    if (n)
    {
        pc_udp_send(peer, resp, n);
    }
}

proto_bool pc_dns_server_begin()
{
    return pc_udp_listen(53, pc_dns_server_udp_handler, NULL);
}

#else // host build: no lwIP. The codec + table above are host-tested; begin is a stub.

proto_bool pc_dns_server_begin()
{
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_DNS_SERVER
