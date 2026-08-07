// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_wire.c
 * @brief The DNS name codec. See dns_wire.h.
 */

#include "network_drivers/network/dns/dns_wire.h"

#include "mmgr/rawmemcpy.h" // proto_raw_read: the label bytes move whole

PROTO_BEGIN_DECLS

// A label length byte carries its type in the top two bits: 00 is a length, 11 is a pointer, and
// 01 / 10 are undefined.
#define PC_DNS_LABEL_TYPE 0xC0u
#define PC_DNS_LABEL_PTR 0xC0u
#define PC_DNS_PTR_OFF 0x3Fu

proto_bool pc_dns_name_decode(const uint8_t *pkt, size_t len, size_t off, char *out, size_t out_cap, size_t *next,
                              proto_bool allow_ptr)
{
    if (pkt == NULL || out == NULL || out_cap == 0)
    {
        return PROTO_FALSE;
    }
    size_t n = 0;     // dotted bytes written
    size_t cur = off; // where the next length byte sits
    size_t after = 0; // offset just past the name as it sits at off
    size_t hops = 0;  // pointers followed
    proto_bool jumped = PROTO_FALSE;
    for (;;)
    {
        if (cur >= len)
        {
            return PROTO_FALSE;
        }
        uint8_t b = pkt[cur];
        if ((b & PC_DNS_LABEL_TYPE) == PC_DNS_LABEL_PTR)
        {
            if (!allow_ptr || cur + 1 >= len || hops >= PC_DNS_PTR_HOPS)
            {
                return PROTO_FALSE;
            }
            // The first pointer is where this name ends; the rest are inside what it pointed at.
            if (!jumped)
            {
                after = cur + 2;
                jumped = PROTO_TRUE;
            }
            hops++;
            cur = (size_t)(((uint16_t)(b & PC_DNS_PTR_OFF) << 8) | pkt[cur + 1]);
            continue;
        }
        if ((b & PC_DNS_LABEL_TYPE) != 0)
        {
            return PROTO_FALSE; // 01 / 10: no such label type
        }
        cur++;
        if (b == 0)
        {
            if (!jumped)
            {
                after = cur;
            }
            break;
        }
        if (b > PC_DNS_LABEL_MAX || cur + b > len)
        {
            return PROTO_FALSE;
        }
        if (n != 0)
        {
            if (n + 1 >= out_cap)
            {
                return PROTO_FALSE;
            }
            out[n] = '.';
            n++;
        }
        if (n + b >= out_cap)
        {
            return PROTO_FALSE;
        }
        proto_raw_read(out + n, pkt + cur, b);
        n += b;
        cur += b;
    }
    out[n] = '\0';
    if (next != NULL)
    {
        *next = after;
    }
    return PROTO_TRUE;
}

size_t pc_dns_name_encode(uint8_t *out, size_t cap, const char *dotted)
{
    if (out == NULL || dotted == NULL)
    {
        return 0;
    }
    size_t n = 0; // bytes written
    size_t i = 0; // read cursor
    for (;;)
    {
        size_t start = i;
        while (dotted[i] != '\0' && dotted[i] != '.')
        {
            i++;
        }
        size_t label = i - start;
        if (label == 0)
        {
            if (dotted[i] == '\0')
            {
                break; // end of the name, with or without the trailing root dot
            }
            return 0; // an empty label inside a name encodes to nothing readable
        }
        if (label > PC_DNS_LABEL_MAX)
        {
            return 0;
        }
        if (n + 1 + label >= cap) // the root byte still has to fit after this label
        {
            return 0;
        }
        out[n] = (uint8_t)label;
        n++;
        proto_raw_read(out + n, dotted + start, label);
        n += label;
        if (dotted[i] == '\0')
        {
            break;
        }
        i++; // step over the dot
    }
    if (n >= cap)
    {
        return 0;
    }
    out[n] = 0; // root
    n++;
    return n;
}

proto_bool pc_dns_name_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
    {
        return PROTO_FALSE;
    }
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0')
    {
        char ca = a[i];
        char cb = b[i];
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
        i++;
    }
    return a[i] == b[i];
}

PROTO_END_DECLS
