// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proxy_protocol.c
 * @brief HAProxy PROXY protocol v1 / v2 parser + builder (pure, host-tested).
 */

#include "network_drivers/transport/proxy_protocol/proxy_protocol.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_PROXY_PROTOCOL

static const uint8_t kV2Sig[PROXY_V2_SIG_LEN] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D,
                                                 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Parse a dotted-quad IPv4 in [s, s+n) into a host-order uint32; false on malformed.
static proto_bool parse_ipv4(const char *s, size_t n, uint32_t *out)
{
    uint32_t v = 0;
    int octets = 0;
    size_t i = 0;
    while (octets < 4)
    {
        if (i >= n || s[i] < '0' || s[i] > '9')
        {
            return PROTO_FALSE;
        }
        uint32_t o = 0;
        size_t digits = 0;
        const proto_bool zero_first = s[i] == '0';
        while (i < n && s[i] >= '0' && s[i] <= '9')
        {
            o = o * 10 + (uint32_t)(s[i] - '0');
            i++;
            if (++digits > 3 || o > 255)
            {
                return PROTO_FALSE;
            }
        }
        if (zero_first && digits > 1) // sec 2.1: heading zeroes are not permitted
        {
            return PROTO_FALSE;
        }
        v = (v << 8) | o;
        octets++;
        if (octets < 4)
        {
            if (i >= n || s[i] != '.')
            {
                return PROTO_FALSE;
            }
            i++;
        }
    }
    if (i != n) // trailing junk
    {
        return PROTO_FALSE;
    }
    *out = v;
    return PROTO_TRUE;
}

static proto_bool parse_u16(const char *s, size_t n, uint16_t *out)
{
    // n == 0 is never true here: both call sites (below, in parse_v1) pass a token produced by
    // parse_v1's own space-delimited tokenizer, which only ever records tokens of length >= 1.
    if (n == 0 || n > 5 || (n > 1 && s[0] == '0')) // sec 2.1: heading zeroes are not permitted
    {
        return PROTO_FALSE;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return PROTO_FALSE;
        }
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    if (v > 0xFFFF)
    {
        return PROTO_FALSE;
    }
    *out = (uint16_t)v;
    return PROTO_TRUE;
}

// Parse the v1 text header (already known to start with "PROXY ").
static proto_bool parse_v1(const uint8_t *buf, size_t len, ProxyInfo *out, size_t *consumed)
{
    // Find the terminating CRLF (the line is bounded at 107 octets).
    size_t crlf = len;
    size_t scan = len < 108 ? len : 108;
    for (size_t i = 0; i + 1 < scan; i++)
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
        {
            crlf = i;
            break;
        }
    }
    if (crlf == len)
    {
        return PROTO_FALSE; // line not complete
    }
    if (crlf + 2 > 107)
    {
        return PROTO_FALSE; // the line, CRLF included, is bounded at 107 octets
    }

    const char *s = (const char *)buf;
    // Tokenize the line by single spaces.
    const char *tok[6];
    size_t tlen[6];
    size_t ntok = 0;
    size_t i = 0;
    while (i < crlf && ntok < 6)
    {
        while (i < crlf && s[i] == ' ')
        {
            i++;
        }
        if (i >= crlf)
        {
            break;
        }
        size_t start = i;
        while (i < crlf && s[i] != ' ')
        {
            i++;
        }
        tok[ntok] = s + start;
        tlen[ntok] = i - start;
        ntok++;
    }
    out->version = 1;
    out->has_addr = PROTO_FALSE;
    out->src_addr = out->dst_addr = 0;
    out->src_port = out->dst_port = 0;
    *consumed = crlf + 2;
    // "PROXY TCP4 <src> <dst> <sport> <dport>". TCP4, TCP6 and UNKNOWN are the published family
    // tokens; any other sequence does not match the protocol and is discarded.
    proto_bool tcp4 = (ntok == 6 && tlen[1] == 4 && mem.cmp(tok[1], "TCP4", 4) == 0);
    proto_bool tcp6 = (ntok == 6 && tlen[1] == 4 && mem.cmp(tok[1], "TCP6", 4) == 0);
    proto_bool unknown = (ntok >= 2 && tlen[1] == 7 && mem.cmp(tok[1], "UNKNOWN", 7) == 0);
    if (!tcp4 && !tcp6 && !unknown)
    {
        return PROTO_FALSE;
    }
    if (tcp4)
    {
        // A TCP4 line whose fields are outside the published ranges is the same mismatch.
        if (!parse_ipv4(tok[2], tlen[2], &out->src_addr) || !parse_ipv4(tok[3], tlen[3], &out->dst_addr) ||
            !parse_u16(tok[4], tlen[4], &out->src_port) || !parse_u16(tok[5], tlen[5], &out->dst_port))
        {
            return PROTO_FALSE;
        }
        out->has_addr = PROTO_TRUE;
    }
    return PROTO_TRUE;
}

proto_bool proxy_parse(const uint8_t *buf, size_t len, ProxyInfo *out, size_t *consumed)
{
    if (!buf || !out || !consumed)
    {
        return PROTO_FALSE;
    }

    // v2: the 12-octet binary signature.
    if (len >= PROXY_V2_SIG_LEN && mem.cmp(buf, kV2Sig, PROXY_V2_SIG_LEN) == 0)
    {
        if (len < 16) // signature + ver_cmd + fam + 2-octet length
        {
            return PROTO_FALSE;
        }
        uint8_t ver_cmd = buf[12];
        uint8_t fam = buf[13];
        uint16_t addr_len = rd16(buf + 14);
        size_t total = 16 + (size_t)addr_len;
        if (total > len)
        {
            return PROTO_FALSE; // address block not fully buffered
        }
        if ((ver_cmd & 0xF0) != 0x20) // must be version 2
        {
            return PROTO_FALSE;
        }
        if ((ver_cmd & 0x0Fu) > 0x1u) // LOCAL and PROXY are the assigned commands
        {
            return PROTO_FALSE;
        }
        // AF_UNSPEC/AF_INET/AF_INET6/AF_UNIX over UNSPEC/STREAM/DGRAM are the assigned pairs.
        if ((fam >> 4) > 0x3u || (fam & 0x0Fu) > 0x2u)
        {
            return PROTO_FALSE;
        }
        out->version = 2;
        out->has_addr = PROTO_FALSE;
        out->src_addr = out->dst_addr = 0;
        out->src_port = out->dst_port = 0;
        if (ver_cmd == PROXY_V2_VER_CMD_PROXY && fam == PROXY_V2_FAM_TCP4 && addr_len >= 12)
        {
            out->src_addr = rd32(buf + 16);
            out->dst_addr = rd32(buf + 20);
            out->src_port = rd16(buf + 24);
            out->dst_port = rd16(buf + 26);
            out->has_addr = PROTO_TRUE;
        }
        *consumed = total;
        return PROTO_TRUE;
    }

    // v1: the "PROXY " text prefix.
    if (len >= 6 && mem.cmp(buf, "PROXY ", 6) == 0)
    {
        return parse_v1(buf, len, out, consumed);
    }

    return PROTO_FALSE; // no PROXY header present
}

size_t proxy_v1_build(char *buf, size_t cap, uint32_t src_addr, uint32_t dst_addr, uint16_t src_port, uint16_t dst_port)
{
    if (!buf)
    {
        return 0;
    }
    protocore_sb sb_buf = {buf, cap, 0, PROTO_TRUE};
    Sb.put(&sb_buf, "PROXY TCP4 ");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)((src_addr >> 24) & 0xFF)));
    Sb.put(&sb_buf, ".");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)((src_addr >> 16) & 0xFF)));
    Sb.put(&sb_buf, ".");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)((src_addr >> 8) & 0xFF)));
    Sb.put(&sb_buf, ".");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)(src_addr & 0xFF)));
    Sb.put(&sb_buf, " ");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)((dst_addr >> 24) & 0xFF)));
    Sb.put(&sb_buf, ".");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)((dst_addr >> 16) & 0xFF)));
    Sb.put(&sb_buf, ".");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)((dst_addr >> 8) & 0xFF)));
    Sb.put(&sb_buf, ".");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)(dst_addr & 0xFF)));
    Sb.put(&sb_buf, " ");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)src_port));
    Sb.put(&sb_buf, " ");
    Sb.u32(&sb_buf, (uint32_t)((unsigned)dst_port));
    Sb.put(&sb_buf, "\r\n");
    int n = (int)Sb.finish(&sb_buf);
    // n < 0 is never true here: the format string uses only %u conversions (no wide/multibyte
    // specifiers), so snprintf can't fail with an encoding error for this call.
    if (n < 0 || (size_t)n >= cap)
    {
        return 0;
    }
    return (size_t)n;
}

size_t proxy_v2_build(uint8_t *buf, size_t cap, uint32_t src_addr, uint32_t dst_addr, uint16_t src_port,
                      uint16_t dst_port)
{
    const size_t total = 16 + 12; // header + TCP/IPv4 address block
    if (!buf || cap < total)
    {
        return 0;
    }
    mem.cpy(buf, kV2Sig, PROXY_V2_SIG_LEN);
    buf[12] = PROXY_V2_VER_CMD_PROXY;
    buf[13] = PROXY_V2_FAM_TCP4;
    buf[14] = 0x00; // address-block length (12), big-endian
    buf[15] = 0x0C;
    buf[16] = (uint8_t)(src_addr >> 24);
    buf[17] = (uint8_t)(src_addr >> 16);
    buf[18] = (uint8_t)(src_addr >> 8);
    buf[19] = (uint8_t)(src_addr);
    buf[20] = (uint8_t)(dst_addr >> 24);
    buf[21] = (uint8_t)(dst_addr >> 16);
    buf[22] = (uint8_t)(dst_addr >> 8);
    buf[23] = (uint8_t)(dst_addr);
    buf[24] = (uint8_t)(src_port >> 8);
    buf[25] = (uint8_t)(src_port);
    buf[26] = (uint8_t)(dst_port >> 8);
    buf[27] = (uint8_t)(dst_port);
    return total;
}

#endif // PROTOCORE_ENABLE_PROXY_PROTOCOL
