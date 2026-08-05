// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file qpack.c
 * @brief QPACK (RFC 9204) - implementation. See qpack.h.
 *
 * The static table is generated verbatim from RFC 9204 Appendix A (0-indexed). The prefix-integer
 * and Huffman primitives are shared with HPACK via pc_hpack_prim.h. No dynamic table is maintained:
 * we encode only against the static table and reject any dynamic-table reference on decode.
 */

#include "network_drivers/presentation/http/http3/qpack.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/codec/hpack_prim/hpack_prim.h" // shared prefix-int + Huffman

// QPACK static table (RFC 9204 Appendix A, 0-indexed). {name, value}. Generated from the RFC.
static const char *const QPACK_STATIC[99][2] = {
    {":authority", ""},
    {":path", "/"},
    {"age", "0"},
    {"content-disposition", ""},
    {"content-length", "0"},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"referer", ""},
    {"set-cookie", ""},
    {":method", "CONNECT"},
    {":method", "DELETE"},
    {":method", "GET"},
    {":method", "HEAD"},
    {":method", "OPTIONS"},
    {":method", "POST"},
    {":method", "PUT"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "103"},
    {":status", "200"},
    {":status", "304"},
    {":status", "404"},
    {":status", "503"},
    {"accept", "*/*"},
    {"accept", "application/dns-message"},
    {"accept-encoding", "gzip, deflate, br"},
    {"accept-ranges", "bytes"},
    {"access-control-allow-headers", "cache-control"},
    {"access-control-allow-headers", "content-type"},
    {"access-control-allow-origin", "*"},
    {"cache-control", "max-age=0"},
    {"cache-control", "max-age=2592000"},
    {"cache-control", "max-age=604800"},
    {"cache-control", "no-cache"},
    {"cache-control", "no-store"},
    {"cache-control", "public, max-age=31536000"},
    {"content-encoding", "br"},
    {"content-encoding", "gzip"},
    {"content-type", "application/dns-message"},
    {"content-type", "application/javascript"},
    {"content-type", "application/json"},
    {"content-type", "application/x-www-form-urlencoded"},
    {"content-type", "image/gif"},
    {"content-type", "image/jpeg"},
    {"content-type", "image/png"},
    {"content-type", "text/css"},
    {"content-type", "text/html;charset=utf-8"},
    {"content-type", "text/plain"},
    {"content-type", "text/plain;charset=utf-8"},
    {"range", "bytes=0-"},
    {"strict-transport-security", "max-age=31536000"},
    {"strict-transport-security", "max-age=31536000;includesubdomains"},
    {"strict-transport-security", "max-age=31536000;includesubdomains;preload"},
    {"vary", "accept-encoding"},
    {"vary", "origin"},
    {"x-content-type-options", "nosniff"},
    {"x-xss-protection", "1; mode=block"},
    {":status", "100"},
    {":status", "204"},
    {":status", "206"},
    {":status", "302"},
    {":status", "400"},
    {":status", "403"},
    {":status", "421"},
    {":status", "425"},
    {":status", "500"},
    {"accept-language", ""},
    {"access-control-allow-credentials", "FALSE"},
    {"access-control-allow-credentials", "TRUE"},
    {"access-control-allow-headers", "*"},
    {"access-control-allow-methods", "get"},
    {"access-control-allow-methods", "get, post, options"},
    {"access-control-allow-methods", "options"},
    {"access-control-expose-headers", "content-length"},
    {"access-control-request-headers", "content-type"},
    {"access-control-request-method", "get"},
    {"access-control-request-method", "post"},
    {"alt-svc", "clear"},
    {"authorization", ""},
    {"content-security-policy", "script-src 'none';object-src 'none';base-uri 'none'"},
    {"early-data", "1"},
    {"expect-ct", ""},
    {"forwarded", ""},
    {"if-range", ""},
    {"origin", ""},
    {"purpose", "prefetch"},
    {"server", ""},
    {"timing-allow-origin", "*"},
    {"upgrade-insecure-requests", "1"},
    {"user-agent", ""},
    {"x-forwarded-for", ""},
    {"x-frame-options", "deny"},
    {"x-frame-options", "sameorigin"},
};

size_t pc_qpack_encode_prefix(uint8_t *out, size_t cap)
{
    if (cap < 2)
    {
        return 0;
    }
    out[0] = 0x00; // Required Insert Count = 0
    out[1] = 0x00; // S = 0, Delta Base = 0
    return 2;
}

size_t pc_qpack_encode_header(uint8_t *out, size_t cap, const char *name, size_t name_len, const char *value,
                              size_t value_len)
{
    int name_idx = -1, full_idx = -1;
    for (int i = 0; i < 99; i++)
    {
        if (strnlen(QPACK_STATIC[i][0], name_len + 1) == name_len && memcmp(QPACK_STATIC[i][0], name, name_len) == 0)
        {
            if (name_idx < 0)
            {
                name_idx = i;
            }
            if (strnlen(QPACK_STATIC[i][1], value_len + 1) == value_len &&
                memcmp(QPACK_STATIC[i][1], value, value_len) == 0)
            {
                full_idx = i;
                break;
            }
        }
    }
    if (full_idx >= 0) // Indexed Field Line, static: 1 T=1 i(6)
    {
        return pc_hpack_encode_int(out, cap, 6, 0xC0, (uint32_t)full_idx);
    }

    if (name_idx >= 0)
    { // Literal Field Line with Name Reference, static: 01 N=0 T=1 i(4)
        size_t o = pc_hpack_encode_int(out, cap, 4, 0x50, (uint32_t)name_idx);
        if (!o)
        {
            return 0;
        }
        size_t vs = pc_hpack_encode_str(out + o, cap - o, value, value_len);
        if (!vs)
        {
            return 0;
        }
        return o + vs;
    }

    // Literal Field Line with Literal Name: 001 N=0 H NameLen(3), name string, value string.
    size_t hl = pc_hpack_huff_len(name, name_len);
    proto_bool huff = hl < name_len;
    size_t nbytes = huff ? hl : name_len;
    size_t o = pc_hpack_encode_int(out, cap, 3, (uint8_t)(0x20 | (huff ? 0x08 : 0x00)), (uint32_t)nbytes);
    if (!o)
    {
        return 0;
    }
    if (huff)
    {
        size_t body = pc_hpack_huff_encode(out + o, cap - o, name, name_len);
        if (body != hl)
        {
            return 0;
        }
        o += body;
    }
    else
    {
        if (o + name_len > cap)
        {
            return 0;
        }
        memcpy(out + o, name, name_len);
        o += name_len;
    }
    size_t vs = pc_hpack_encode_str(out + o, cap - o, value, value_len);
    if (!vs)
    {
        return 0;
    }
    return o + vs;
}

proto_bool pc_qpack_decode(const uint8_t *block, size_t len, char *scratch, size_t scratch_cap, QpackEmitFn emit,
                           void *ctx)
{
    size_t pos = 0;
    // Encoded Field Section Prefix (RFC 9204 sec 4.5.1): Required Insert Count, then S + Delta Base.
    size_t c = 0;
    uint32_t ric = 0;
    if (!pc_hpack_decode_int(block + pos, len - pos, 8, &c, &ric))
    {
        return PROTO_FALSE;
    }
    pos += c;
    if (ric != 0) // a non-zero Required Insert Count references the dynamic table (capacity 0)
    {
        return PROTO_FALSE;
    }
    uint32_t base = 0;
    if (!pc_hpack_decode_int(block + pos, len - pos, 7, &c, &base)) // S bit + Delta Base; ignored when RIC = 0
    {
        return PROTO_FALSE;
    }
    pos += c;

    while (pos < len)
    {
        uint8_t b = block[pos];
        if (b & 0x80)
        {                    // Indexed Field Line (sec 4.5.2): 1 T i(6)
            if (!(b & 0x40)) // T = 0 -> dynamic table
            {
                return PROTO_FALSE;
            }
            uint32_t idx = 0;
            if (!pc_hpack_decode_int(block + pos, len - pos, 6, &c, &idx) || idx >= 99)
            {
                return PROTO_FALSE;
            }
            pos += c;
            const char *nm = QPACK_STATIC[idx][0];
            const char *vl = QPACK_STATIC[idx][1];
            if (!emit(ctx, nm, strnlen(nm, scratch_cap + 1), vl, strnlen(vl, scratch_cap + 1)))
            {
                return PROTO_FALSE;
            }
        }
        else if ((b & 0xC0) == 0x40)
        { // Literal Field Line with Name Reference (sec 4.5.4): 01 N T i(4)
            proto_bool is_static = (b & 0x10) != 0;
            uint32_t idx = 0;
            if (!pc_hpack_decode_int(block + pos, len - pos, 4, &c, &idx))
            {
                return PROTO_FALSE;
            }
            pos += c;
            if (!is_static || idx >= 99) // dynamic name reference
            {
                return PROTO_FALSE;
            }
            const char *nm = QPACK_STATIC[idx][0];
            size_t nlen = strnlen(nm, scratch_cap + 1);
            if (nlen > scratch_cap)
            {
                return PROTO_FALSE;
            }
            memcpy(scratch, nm, nlen);
            size_t vlen = 0;
            if (!pc_hpack_decode_str(block, len, &pos, scratch + nlen, scratch_cap - nlen, &vlen))
            {
                return PROTO_FALSE;
            }
            if (!emit(ctx, scratch, nlen, scratch + nlen, vlen))
            {
                return PROTO_FALSE;
            }
        }
        else if ((b & 0xE0) == 0x20)
        { // Literal Field Line with Literal Name (sec 4.5.6): 001 N H NameLen(3)
            proto_bool huff = (b & 0x08) != 0;
            uint32_t nlen32 = 0;
            if (!pc_hpack_decode_int(block + pos, len - pos, 3, &c, &nlen32))
            {
                return PROTO_FALSE;
            }
            pos += c;
            if (pos + nlen32 > len)
            {
                return PROTO_FALSE;
            }
            size_t nlen = 0;
            if (huff)
            {
                if (!pc_hpack_huff_decode(block + pos, nlen32, scratch, scratch_cap, &nlen))
                {
                    return PROTO_FALSE;
                }
            }
            else
            {
                if (nlen32 > scratch_cap)
                {
                    return PROTO_FALSE;
                }
                memcpy(scratch, block + pos, nlen32);
                nlen = nlen32;
            }
            pos += nlen32;
            size_t vlen = 0;
            if (!pc_hpack_decode_str(block, len, &pos, scratch + nlen, scratch_cap - nlen, &vlen))
            {
                return PROTO_FALSE;
            }
            if (!emit(ctx, scratch, nlen, scratch + nlen, vlen))
            {
                return PROTO_FALSE;
            }
        }
        else
        { // 0001 xxxx Indexed Post-Base / 0000 xxxx Literal Post-Base Name Ref: both dynamic
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_HTTP3
