// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ip.c
 * @brief pc_ip implementation: RFC 4291 text parsing, RFC 5952 canonical formatting, scope
 *        classification. Pure, hand-rolled (no stdlib parsing), host-identical.
 *
 * The public entry points (pc_ip_parse / pc_ip_format / pc_ip_classify) are thin: the work is
 * split into single-purpose static helpers below - one per concern (parse a hextet, assemble the
 * 16 bytes, find the zero run to compress, classify one family).
 */

#include "shared_primitives/ip.h"
#include "shared_primitives/hex.h" // PC_HEX_LOWER - the shared digit table

// -------------------------------------------------------------------------------------------
// Parsing helpers (text -> bytes)
// -------------------------------------------------------------------------------------------

/** Value of one hex digit, or -1 if @p c is not a hex digit. */
static int hexval(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/** Parse the dotted-quad in s[0, len) into out[4]. Rejects empty octets, > 3 digits, or > 255. */
static proto_bool parse_v4(const char *s, size_t len, uint8_t out[4])
{
    int oct = 0;
    int val = -1;
    int digits = 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (c == '.') // octet separator: commit the octet in progress
        {
            if (digits == 0 || oct >= 3)
            {
                return PROTO_FALSE;
            }
            out[oct++] = (uint8_t)val;
            val = -1;
            digits = 0;
        }
        else if (c >= '0' && c <= '9')
        {
            if (digits >= 3)
            {
                return PROTO_FALSE;
            }
            val = (val < 0 ? 0 : val) * 10 + (c - '0');
            if (val > 255)
            {
                return PROTO_FALSE;
            }
            digits++;
        }
        else
        {
            return PROTO_FALSE;
        }
    }
    if (oct != 3 || digits == 0) // exactly four octets, the last one non-empty
    {
        return PROTO_FALSE;
    }
    out[3] = (uint8_t)val;
    return PROTO_TRUE;
}

/** Parse one 1-4 digit IPv6 hextet in s[0, len) into *out. False if empty / too long / non-hex. */
static proto_bool parse_hextet(const char *s, size_t len, uint16_t *out)
{
    if (len == 0 || len > 4)
    {
        return PROTO_FALSE;
    }
    int v = 0;
    for (size_t i = 0; i < len; i++)
    {
        int h = hexval(s[i]);
        if (h < 0)
        {
            return PROTO_FALSE;
        }
        v = (v << 4) | h;
    }
    *out = (uint16_t)v;
    return PROTO_TRUE;
}

/**
 * Place the @p nhead hextets that preceded "::" and the @p ntail that followed it into the 16
 * output bytes (big-endian), zero-filling the gap the "::" stands for: head fills from the left,
 * tail from the right.
 */
static void assemble_v6(const uint16_t *head, int nhead, const uint16_t *tail, int ntail, uint8_t out[16])
{
    uint16_t g[8];
    memset(g, 0, sizeof(g));
    for (int k = 0; k < nhead; k++)
    {
        g[k] = head[k];
    }
    for (int k = 0; k < ntail; k++)
    {
        g[8 - ntail + k] = tail[k];
    }
    for (int k = 0; k < 8; k++)
    {
        out[2 * k] = (uint8_t)(g[k] >> 8);
        out[2 * k + 1] = (uint8_t)(g[k] & 0xFF);
    }
}

/**
 * Parse an IPv6 text address (RFC 4291 §2.2) into out[16].
 *
 * The address is up to eight 16-bit hextets separated by ':'. A single "::" may stand in for one
 * or more all-zero hextets: the groups before it fill a head list from the left, those after fill
 * a tail list from the right, and assemble_v6() zero-fills the middle. The last token may be a
 * dotted-quad (an embedded IPv4 tail such as ::ffff:1.2.3.4), which expands to two hextets.
 */
static proto_bool parse_v6(const char *s, size_t len, uint8_t out[16])
{
    uint16_t head[8];
    uint16_t tail[8];
    int nhead = 0;
    int ntail = 0;
    proto_bool seen_dc = PROTO_FALSE; // have we passed the "::" yet?
    uint16_t *cur = head;             // the list currently being filled (head, then tail after "::")
    int *ncur = &nhead;

    size_t i = 0;
    // len >= 1 is never false here: the only caller (pc_ip_parse) rejects an empty string before
    // calling parse_v6, and colon (which gates this call) requires at least one ':' character.
    if (len >= 1 && s[0] == ':')
    {
        if (len < 2 || s[1] != ':')
        {
            return PROTO_FALSE; // a lone leading ':' is illegal
        }
        seen_dc = PROTO_TRUE;
        cur = tail;
        ncur = &ntail;
        i = 2;
        if (i == len) // the whole address is "::"
        {
            memset(out, 0, 16);
            return PROTO_TRUE;
        }
    }

    while (i < len)
    {
        // Span this token up to the next ':' (noting a '.' -> it is an embedded-v4 tail).
        size_t j = i;
        proto_bool has_dot = PROTO_FALSE;
        while (j < len && s[j] != ':')
        {
            if (s[j] == '.')
            {
                has_dot = PROTO_TRUE;
            }
            j++;
        }
        size_t tlen = j - i;

        if (has_dot) // embedded IPv4 -> two hextets; it must be the final token
        {
            uint8_t q[4];
            if (!parse_v4(s + i, tlen, q) || *ncur > 6 || j != len)
            {
                return PROTO_FALSE;
            }
            cur[(*ncur)++] = (uint16_t)((q[0] << 8) | q[1]);
            cur[(*ncur)++] = (uint16_t)((q[2] << 8) | q[3]);
            break;
        }

        uint16_t hx;
        if (!parse_hextet(s + i, tlen, &hx) || *ncur >= 8)
        {
            return PROTO_FALSE;
        }
        cur[(*ncur)++] = hx;
        i = j;

        // Consume the separator: "::" switches us to the tail list, a single ":" just continues.
        if (i < len)
        {
            if (i + 1 < len && s[i + 1] == ':') // "::"
            {
                if (seen_dc)
                {
                    return PROTO_FALSE; // only one "::" is allowed
                }
                seen_dc = PROTO_TRUE;
                cur = tail;
                ncur = &ntail;
                i += 2;
                if (i == len)
                {
                    break; // trailing "::"
                }
            }
            else // single ':'
            {
                i += 1;
                if (i == len)
                {
                    return PROTO_FALSE; // a trailing lone ':' is illegal
                }
            }
        }
    }

    // Without "::" all eight hextets must be present; with it, the gap stands for >= 1 group.
    int total = nhead + ntail;
    if (seen_dc ? (total > 7) : (total != 8))
    {
        return PROTO_FALSE;
    }

    assemble_v6(head, nhead, tail, ntail, out);
    return PROTO_TRUE;
}

// -------------------------------------------------------------------------------------------
// Formatting helpers (bytes -> text)
// -------------------------------------------------------------------------------------------

/** Append the decimal form of @p v (0-255) at @p o. Returns the digit count (1-3). */
static size_t put_u8(uint8_t v, char *o)
{
    size_t n = 0;
    if (v >= 100)
    {
        o[n++] = (char)('0' + v / 100);
    }
    if (v >= 10)
    {
        o[n++] = (char)('0' + (v / 10) % 10);
    }
    o[n++] = (char)('0' + v % 10);
    return n;
}

/** Format the four bytes at @p b as "a.b.c.d" into @p out (NUL-terminated). 0 if it won't fit. */
static size_t format_v4(const uint8_t *b, char *out, size_t cap)
{
    char tmp[16];
    size_t n = 0;
    for (int k = 0; k < 4; k++)
    {
        if (k)
        {
            tmp[n++] = '.';
        }
        n += put_u8(b[k], tmp + n);
    }
    if (n + 1 > cap)
    {
        return 0;
    }
    memcpy(out, tmp, n);
    out[n] = '\0';
    return n;
}

/** Append @p v as lower-case hex with no leading zeros at @p o. Returns the digit count (1-4). */
static size_t put_hex16(uint16_t v, char *o)
{
    char t[4];
    int n = 0;
    do
    {
        t[n] = PC_HEX_LOWER[v & 0xF];
        n++;
        v >>= 4;
    } while (v);
    for (int k = 0; k < n; k++) // reverse into place (we built it least-significant first)
    {
        o[k] = t[n - 1 - k];
    }
    return (size_t)n;
}

/**
 * Find the longest run of >= 2 zero hextets to compress to "::" (leftmost wins a tie,
 * RFC 5952 §4.2.3). Writes the run start to *start (-1 if none qualifies) and its length to *len.
 */
static void longest_zero_run(const uint16_t g[8], int *start, int *len)
{
    int best_start = -1;
    int best_len = 0;
    int cur_start = -1;
    int cur_len = 0;
    for (int k = 0; k < 8; k++)
    {
        if (g[k] == 0)
        {
            if (cur_start < 0)
            {
                cur_start = k;
            }
            cur_len++;
            if (cur_len > best_len)
            {
                best_len = cur_len;
                best_start = cur_start;
            }
        }
        else
        {
            cur_start = -1;
            cur_len = 0;
        }
    }
    *start = (best_len >= 2) ? best_start : -1;
    *len = best_len;
}

// -------------------------------------------------------------------------------------------
// Classification helpers
// -------------------------------------------------------------------------------------------

/** True if the 16 v6 bytes carry the ::ffff:0:0/96 IPv4-mapped prefix (RFC 4291 §2.5.5.2). */
static proto_bool is_v4_mapped_bytes(const uint8_t *b)
{
    for (int k = 0; k < 10; k++)
    {
        if (b[k])
        {
            return PROTO_FALSE;
        }
    }
    return b[10] == 0xff && b[11] == 0xff;
}

/** Classify the four v4 bytes at @p b. */
static pc_ip_scope classify_v4(const uint8_t *b)
{
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0)
    {
        return PC_IP_SCOPE_UNSPECIFIED; // 0.0.0.0
    }
    if (b[0] == 127)
    {
        return PC_IP_SCOPE_LOOPBACK; // 127/8
    }
    if (b[0] == 169 && b[1] == 254)
    {
        return PC_IP_SCOPE_LINK_LOCAL; // 169.254/16
    }
    if (b[0] == 10 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168))
    {
        return PC_IP_SCOPE_PRIVATE; // RFC 1918
    }
    if (b[0] >= 224 && b[0] <= 239)
    {
        return PC_IP_SCOPE_MULTICAST; // 224/4
    }
    return PC_IP_SCOPE_GLOBAL;
}

/** Classify the sixteen v6 bytes at @p b (v4-mapped addresses defer to their embedded v4). */
static pc_ip_scope classify_v6(const uint8_t *b)
{
    proto_bool allzero = PROTO_TRUE;
    for (int k = 0; k < 16; k++)
    {
        if (b[k])
        {
            allzero = PROTO_FALSE;
            break;
        }
    }
    if (allzero)
    {
        return PC_IP_SCOPE_UNSPECIFIED; // ::
    }

    proto_bool loopback = (b[15] == 1);
    for (int k = 0; k < 15 && loopback; k++)
    {
        if (b[k])
        {
            loopback = PROTO_FALSE;
        }
    }
    if (loopback)
    {
        return PC_IP_SCOPE_LOOPBACK; // ::1
    }

    if (is_v4_mapped_bytes(b))
    {
        return classify_v4(b + 12); // ::ffff:a.b.c.d takes the v4 scope
    }
    if (b[0] == 0xff)
    {
        return PC_IP_SCOPE_MULTICAST; // ff00::/8
    }
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80)
    {
        return PC_IP_SCOPE_LINK_LOCAL; // fe80::/10
    }
    if ((b[0] & 0xfe) == 0xfc)
    {
        return PC_IP_SCOPE_PRIVATE; // fc00::/7 (unique-local)
    }
    return PC_IP_SCOPE_GLOBAL;
}

// -------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------

static proto_bool parse(const char *s, pc_ip *out)
{
    if (!s || !out)
    {
        return PROTO_FALSE;
    }
    // A ':' means it is v6; a '.' (and no ':') means v4. Bound the scan to a legal length.
    size_t len = 0;
    proto_bool colon = PROTO_FALSE;
    proto_bool dot = PROTO_FALSE;
    while (s[len])
    {
        if (s[len] == ':')
        {
            colon = PROTO_TRUE;
        }
        else if (s[len] == '.')
        {
            dot = PROTO_TRUE;
        }
        if (++len > 45)
        {
            return PROTO_FALSE; // longer than any legal textual address
        }
    }
    if (len == 0)
    {
        return PROTO_FALSE;
    }

    memset(out->bytes, 0, 16);
    if (colon)
    {
        if (!parse_v6(s, len, out->bytes))
        {
            return PROTO_FALSE;
        }
        out->family = PC_IP_V6;
        return PROTO_TRUE;
    }
    if (dot)
    {
        if (!parse_v4(s, len, out->bytes))
        {
            return PROTO_FALSE;
        }
        out->family = PC_IP_V4;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static size_t format(const pc_ip *ip, char *out, size_t cap)
{
    if (!ip || !out || cap == 0)
    {
        return 0;
    }
    if (ip->family == PC_IP_V4)
    {
        return format_v4(ip->bytes, out, cap);
    }
    if (ip->family != PC_IP_V6)
    {
        return 0;
    }

    // IPv4-mapped addresses print with a dotted tail: ::ffff:a.b.c.d (RFC 5952 §5).
    if (pc_ip_is_v4_mapped(ip))
    {
        char tail[16];
        size_t tn = format_v4(ip->bytes + 12, tail, sizeof(tail));
        // tn == 0 is never true here: tail is a fixed 16 bytes, and the longest possible v4 text
        // ("255.255.255.255", 15 chars + NUL) always fits, so format_v4 can't fail on this call.
        if (tn == 0 || 7 + tn + 1 > cap)
        {
            return 0;
        }
        memcpy(out, "::ffff:", 7);
        memcpy(out + 7, tail, tn);
        out[7 + tn] = '\0';
        return 7 + tn;
    }

    uint16_t g[8];
    for (int k = 0; k < 8; k++)
    {
        g[k] = (uint16_t)((ip->bytes[2 * k] << 8) | ip->bytes[2 * k + 1]);
    }

    int zs;
    int zl;
    longest_zero_run(g, &zs, &zl);

    // Emit the hextets, replacing the [zs, zs+zl) run with "::" (inet_ntop6-style colon placement).
    char tmp[PC_IP_STR_MAX];
    size_t n = 0;
    for (int k = 0; k < 8; k++)
    {
        if (zs != -1 && k >= zs && k < zs + zl)
        {
            if (k == zs)
            {
                tmp[n++] = ':'; // one colon here; the pair around the run forms "::"
            }
            continue;
        }
        if (k != 0)
        {
            tmp[n++] = ':';
        }
        n += put_hex16(g[k], tmp + n);
    }
    if (zs != -1 && zs + zl == 8)
    {
        tmp[n++] = ':'; // the run reached the end: close the trailing "::"
    }

    if (n + 1 > cap)
    {
        return 0;
    }
    memcpy(out, tmp, n);
    out[n] = '\0';
    return n;
}

proto_bool pc_ip_is_v4_mapped(const pc_ip *ip)
{
    return ip && ip->family == PC_IP_V6 && is_v4_mapped_bytes(ip->bytes);
}

static pc_ip_scope classify(const pc_ip *ip)
{
    if (!ip)
    {
        return PC_IP_SCOPE_UNSPECIFIED;
    }
    if (ip->family == PC_IP_V4)
    {
        return classify_v4(ip->bytes);
    }
    if (ip->family == PC_IP_V6)
    {
        return classify_v6(ip->bytes);
    }
    return PC_IP_SCOPE_UNSPECIFIED;
}

static proto_bool equal(const pc_ip *a, const pc_ip *b)
{
    if (!a || !b || a->family != b->family)
    {
        return PROTO_FALSE;
    }
    int n = 0;
    if (a->family == PC_IP_V4)
    {
        n = 4;
    }
    else if (a->family == PC_IP_V6)
    {
        n = 16;
    }
    if (n == 0)
    {
        return PROTO_TRUE; // both the same non-address family (PC_IP_NONE)
    }
    return memcmp(a->bytes, b->bytes, (size_t)n) == 0;
}

pc_ip pc_ip_from_v4_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    pc_ip ip;
    memset(&ip, 0, sizeof(ip));
    ip.family = PC_IP_V4;
    ip.bytes[0] = a;
    ip.bytes[1] = b;
    ip.bytes[2] = c;
    ip.bytes[3] = d;
    return ip;
}

pc_ip pc_ip_from_v6_bytes(const uint8_t bytes[16])
{
    pc_ip ip;
    ip.family = PC_IP_V6;
    memcpy(ip.bytes, bytes, 16);
    return ip;
}

uint32_t pc_ip_to_v4_be(const pc_ip *ip)
{
    if (!ip)
    {
        return 0;
    }
    const uint8_t *b = ip->bytes;
    if (ip->family == PC_IP_V4)
    {
        return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    }
    if (pc_ip_is_v4_mapped(ip))
    {
        return ((uint32_t)b[12] << 24) | ((uint32_t)b[13] << 16) | ((uint32_t)b[14] << 8) | b[15];
    }
    return 0;
}

static proto_bool is_unspecified(const pc_ip *ip)
{
    if (!ip || ip->family == PC_IP_NONE)
    {
        return PROTO_TRUE;
    }
    int n = (ip->family == PC_IP_V4) ? 4 : 16;
    for (int i = 0; i < n; i++)
    {
        if (ip->bytes[i])
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

static proto_bool prefix_match(const pc_ip *addr, const pc_ip *net, uint8_t prefix_len)
{
    if (!addr || !net || addr->family != net->family)
    {
        return PROTO_FALSE;
    }
    int bits = (addr->family == PC_IP_V4) ? 32 : (addr->family == PC_IP_V6 ? 128 : 0);
    if (bits == 0 || prefix_len > bits)
    {
        return PROTO_FALSE;
    }
    int whole = prefix_len / 8; // bytes that must match exactly
    for (int i = 0; i < whole; i++)
    {
        if (addr->bytes[i] != net->bytes[i])
        {
            return PROTO_FALSE;
        }
    }
    int rem = prefix_len % 8; // leftover high bits in the next byte
    if (rem)
    {
        uint8_t mask = (uint8_t)(0xFF << (8 - rem));
        if ((addr->bytes[whole] & mask) != (net->bytes[whole] & mask))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

const IpNs Ip = {parse, format, classify, equal, is_unspecified, prefix_match};
