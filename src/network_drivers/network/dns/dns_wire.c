// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_wire.c
 * @brief The DNS name codec (RFC 1035 sec 3.1, sec 4.1.4). See dns_wire.h.
 */

#include "network_drivers/network/dns/dns_wire.h"

#include "mmgr/rawmemcpy.h" // raw.read: the label octets move whole

PROTOCORE_BEGIN_DECLS

// RFC 1035 sec 4.1.4: the high order two bits of a length octet. 00 is a label, 11 is a pointer,
// 10 and 01 are reserved.
#define PROTOCORE_DNS_LABEL_TYPE 0xC0u
#define PROTOCORE_DNS_LABEL_PTR 0xC0u

// RFC 1035 sec 4.1.4: the low six bits of a pointer's first octet, the high half of its OFFSET.
#define PROTOCORE_DNS_OFFSET_MASK 0x3Fu

/**
 * @brief The calls that read the handle - what DnsWireNs points at.
 *
 * No storage member: every octet a call touches belongs to the caller.
 *
 * @var DnsWireInternal::ns  the handle a caller sets a call's members on
 */
struct DnsWireInternal
{
    DnsWireNs *ns;
};

static struct DnsWireInternal s_dns_wire = {.ns = &DnsWire};

// Walks the labels at msg.off, writing each one into msg.out with a dot between, and follows a
// pointer's OFFSET when msg.allow_ptr is set.
static void dns_name_decode(struct DnsWireInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->next = 0;

    const uint8_t *pkt = ctx->ns->msg.pkt;
    const size_t len = ctx->ns->msg.len;
    char *out = ctx->ns->msg.out;
    const size_t out_cap = ctx->ns->msg.out_cap;
    if (pkt == NULL || out == NULL || out_cap == 0)
    {
        return;
    }

    size_t n = 0;                  // dotted octets written
    size_t cur = ctx->ns->msg.off; // where the next length octet sits
    size_t after = 0;              // offset just past the name as it sits at msg.off
    size_t hops = 0;               // pointers followed
    proto_bool jumped = PROTO_FALSE;
    for (;;)
    {
        if (cur >= len)
        {
            return;
        }
        uint8_t b = pkt[cur];
        if ((b & PROTOCORE_DNS_LABEL_TYPE) == PROTOCORE_DNS_LABEL_PTR)
        {
            if (!ctx->ns->msg.allow_ptr || cur + 1 >= len || hops >= PROTOCORE_DNS_PTR_HOPS)
            {
                return;
            }
            // The first pointer is where this name ends; the rest are inside what it pointed at.
            if (!jumped)
            {
                after = cur + 2;
                jumped = PROTO_TRUE;
            }
            hops++;
            // OFFSET: the pointer's low six bits over the whole second octet.
            cur = (size_t)(((uint16_t)(b & PROTOCORE_DNS_OFFSET_MASK) << 8) | pkt[cur + 1]);
            continue;
        }
        if ((b & PROTOCORE_DNS_LABEL_TYPE) != 0)
        {
            return; // 10 and 01 are reserved (RFC 1035 sec 4.1.4)
        }
        cur++;
        if (b == 0)
        {
            if (!jumped)
            {
                after = cur; // the null label of the root ends the name
            }
            break;
        }
        if (b > PROTOCORE_DNS_LABEL_MAX || cur + b > len)
        {
            return;
        }
        if (n != 0)
        {
            if (n + 1 >= out_cap)
            {
                return;
            }
            out[n] = '.';
            n++;
        }
        if (n + b >= out_cap)
        {
            return;
        }
        raw.read(out + n, pkt + cur, b);
        n += b;
        cur += b;
    }
    out[n] = '\0';
    ctx->ns->next = after;
    ctx->ns->ok = PROTO_TRUE;
}

// Splits text.dotted at each dot and writes every run as a length octet followed by its octets,
// then the null label of the root.
static void dns_name_encode(struct DnsWireInternal *restrict ctx)
{
    ctx->ns->n = 0;

    uint8_t *out = ctx->ns->text.out;
    const size_t cap = ctx->ns->text.out_cap;
    const char *dotted = ctx->ns->text.dotted;
    if (out == NULL || dotted == NULL)
    {
        return;
    }

    size_t w = 0; // octets written
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
            return; // an empty label inside a name encodes to nothing readable
        }
        if (label > PROTOCORE_DNS_LABEL_MAX)
        {
            return;
        }
        if (w + 1 + label >= cap) // the root octet still has to fit after this label
        {
            return;
        }
        out[w] = (uint8_t)label;
        w++;
        raw.read(out + w, dotted + start, label);
        w += label;
        if (dotted[i] == '\0')
        {
            break;
        }
        i++; // step over the dot
    }
    if (w >= cap)
    {
        return;
    }
    out[w] = 0; // the null label of the root
    w++;
    ctx->ns->n = w;
}

// Folds each A-Z to lower case and compares octet by octet, ends included.
static void dns_name_eq(struct DnsWireInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;

    const char *a = ctx->ns->cmp.a;
    const char *b = ctx->ns->cmp.b;
    if (a == NULL || b == NULL)
    {
        return;
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
            return;
        }
        i++;
    }
    ctx->ns->ok = (a[i] == b[i]) ? PROTO_TRUE : PROTO_FALSE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
DnsWireNs DnsWire = {.decode = dns_name_decode, .encode = dns_name_encode, .eq = dns_name_eq, .internal = &s_dns_wire};

PROTOCORE_END_DECLS
