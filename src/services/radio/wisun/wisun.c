// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wisun.c
 * @brief Wi-SUN FAN border-router connector (see wisun.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WISUN

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem/protomem.h"
#include "services/radio/wisun/wisun.h"

PROTOCORE_BEGIN_DECLS

// Emit one CoAP option (RFC 7252 sec 3.1): the (delta,length) nibble header + extended bytes + value.
static proto_bool emit_option(uint8_t *out, size_t *o, size_t cap, uint16_t delta, const uint8_t *val, uint16_t vlen)
{
    size_t start = *o;
    if (start + 1 > cap)
    {
        return PROTO_FALSE;
    }
    uint8_t dn;
    uint8_t ln;
    uint8_t dext[2];
    uint8_t lext[2];
    int dexn = 0;
    int lexn = 0;
    if (delta < 13)
    {
        dn = (uint8_t)delta;
    }
    else if (delta < 269)
    {
        dn = 13;
        dext[0] = (uint8_t)(delta - 13);
        dexn = 1;
    }
    else
    {
        dn = 14;
        uint16_t x = (uint16_t)(delta - 269);
        dext[0] = (uint8_t)(x >> 8);
        dext[1] = (uint8_t)x;
        dexn = 2;
    }
    if (vlen < 13)
    {
        ln = (uint8_t)vlen;
    }
    else if (vlen < 269)
    {
        ln = 13;
        lext[0] = (uint8_t)(vlen - 13);
        lexn = 1;
    }
    else
    {
        ln = 14;
        uint16_t x = (uint16_t)(vlen - 269);
        lext[0] = (uint8_t)(x >> 8);
        lext[1] = (uint8_t)x;
        lexn = 2;
    }
    if (start + 1 + dexn + lexn + vlen > cap)
    {
        return PROTO_FALSE;
    }
    out[(*o)++] = (uint8_t)((dn << 4) | ln);
    for (int i = 0; i < dexn; i++)
    {
        out[(*o)++] = dext[i];
    }
    for (int i = 0; i < lexn; i++)
    {
        out[(*o)++] = lext[i];
    }
    for (uint16_t i = 0; i < vlen; i++)
    {
        out[(*o)++] = val[i];
    }
    return PROTO_TRUE;
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_wisun_node_find(uint8_t *restrict work);

void protocore_wisun_build_coap(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = WisunV.build_coap_args.type;
    uint8_t code = WisunV.build_coap_args.code;
    uint16_t msg_id = WisunV.build_coap_args.msg_id;
    const uint8_t *token = WisunV.build_coap_args.token;
    uint8_t tkl = WisunV.build_coap_args.tkl;
    const char *uri_path = WisunV.build_coap_args.uri_path;
    const uint8_t *payload = WisunV.build_coap_args.payload;
    size_t plen = WisunV.build_coap_args.plen;
    uint8_t *out = WisunV.build_coap_args.out;
    size_t cap = WisunV.build_coap_args.cap;

    if (!out || tkl > 8 || (tkl && !token) || (plen && !payload))
    {
        WisunV.n = 0;
        return;
    }
    size_t o = 0;
    if (cap < (size_t)(4 + tkl))
    {
        WisunV.n = 0;
        return;
    }
    out[o++] = (uint8_t)(0x40 | ((type & 0x03) << 4) | (tkl & 0x0F)); // version 1
    out[o++] = code;
    out[o++] = (uint8_t)(msg_id >> 8);
    out[o++] = (uint8_t)msg_id;
    for (uint8_t i = 0; i < tkl; i++)
    {
        out[o++] = token[i];
    }

    // Uri-Path (option 11), one option per '/'-separated segment; delta 11 then 0.
    uint16_t last = 0;
    const char *p = uri_path;
    while (p && *p)
    {
        if (*p == '/')
        {
            p++;
            continue;
        }
        const char *seg = p;
        while (*p && *p != '/')
        {
            p++;
        }
        uint16_t seglen = (uint16_t)(p - seg);
        uint16_t delta = (uint16_t)(11 - last);
        if (!emit_option(out, &o, cap, delta, (const uint8_t *)seg, seglen))
        {
            WisunV.n = 0;
            return;
        }
        last = 11;
    }

    if (plen)
    {
        if (o + 1 + plen > cap)
        {
            WisunV.n = 0;
            return;
        }
        out[o++] = 0xFF; // payload marker
        for (size_t i = 0; i < plen; i++)
        {
            out[o++] = payload[i];
        }
    }
    WisunV.n = o;
}

void protocore_wisun_init(uint8_t *restrict work)
{
    (void)work;
    WisunFan *fan = WisunV.init_args.fan;
    const protocore_ip *border_router = WisunV.init_args.border_router;
    WisunNode *storage = WisunV.init_args.storage;
    size_t cap = WisunV.init_args.cap;

    if (!fan)
    {
        return;
    }
    if (border_router)
    {
        fan->border_router = *border_router;
    }
    else
    {
        mem.set(&fan->border_router, 0, sizeof(fan->border_router));
    }
    fan->nodes = storage;
    fan->cap = storage ? cap : 0;
    fan->count = 0;
}

void protocore_wisun_node_register(uint8_t *restrict work)
{
    WisunFan *fan = WisunV.node_register_args.fan;
    const protocore_ip *addr = WisunV.node_register_args.addr;
    uint32_t now = WisunV.node_register_args.now;

    if (!fan || !fan->nodes || !addr)
    {
        WisunV.i32 = -1;
        return;
    }
    size_t idx = 0;
    WisunV.node_find_args.fan = fan;
    WisunV.node_find_args.addr = addr;
    WisunV.node_find_args.idx = &idx;
    protocore_wisun_node_find(work);
    if (WisunV.ok)
    {
        fan->nodes[idx].joined = PROTO_TRUE;
        fan->nodes[idx].last_seen = now;
        WisunV.i32 = (int)idx;
        return;
    }
    if (fan->count >= fan->cap)
    {
        WisunV.i32 = -1;
        return;
    }
    fan->nodes[fan->count].addr = *addr;
    fan->nodes[fan->count].joined = PROTO_TRUE;
    fan->nodes[fan->count].last_seen = now;
    WisunV.i32 = (int)fan->count++;
}

void protocore_wisun_node_find(uint8_t *restrict work)
{
    (void)work;
    const WisunFan *fan = WisunV.node_find_args.fan;
    const protocore_ip *addr = WisunV.node_find_args.addr;
    size_t *idx = WisunV.node_find_args.idx;

    if (!fan || !fan->nodes || !addr)
    {
        WisunV.ok = PROTO_FALSE;
        return;
    }
    for (size_t i = 0; i < fan->count; i++)
    {
        IpV.args.ip = &fan->nodes[i].addr;
        IpV.args.b = addr;
        Ip.equal(work);
        if (IpV.ok)
        {
            if (idx)
            {
                *idx = i;
            }
            WisunV.ok = PROTO_TRUE;
            return;
        }
    }
    WisunV.ok = PROTO_FALSE;
}

void protocore_wisun_joined_count(uint8_t *restrict work)
{
    (void)work;
    const WisunFan *fan = WisunV.joined_count_args.fan;

    if (!fan || !fan->nodes)
    {
        WisunV.n = 0;
        return;
    }
    size_t c = 0;
    for (size_t i = 0; i < fan->count; i++)
    {
        if (fan->nodes[i].joined)
        {
            c++;
        }
    }
    WisunV.n = c;
}

void protocore_wisun_nodes_json(uint8_t *restrict work)
{
    (void)work;
    const WisunFan *fan = WisunV.nodes_json_args.fan;
    char *out = WisunV.nodes_json_args.out;
    size_t cap = WisunV.nodes_json_args.cap;

    if (!fan || !out || cap == 0)
    {
        WisunV.n = 0;
        return;
    }
    protocore_sb b = {out, cap, 0, PROTO_TRUE};
    Sb.put(&b, "[");
    for (size_t i = 0; i < fan->count; i++)
    {
        if (i)
        {
            Sb.put(&b, ",");
        }
        char astr[PROTOCORE_IP_STR_MAX];
        astr[0] = '\0'; // a family-less address formats to nothing rather than to stack contents
        IpV.args.ip = &fan->nodes[i].addr;
        IpV.args.buf = astr;
        IpV.args.cap = sizeof(astr);
        Ip.format(work);
        Sb.put(&b, "{\"addr\":\"");
        Sb.put(&b, astr);
        Sb.put(&b, "\",\"joined\":");
        Sb.put(&b, fan->nodes[i].joined ? "true" : "false");
        Sb.put(&b, "}");
    }
    Sb.put(&b, "]");
    if (!b.ok)
    {
        WisunV.n = 0;
        return;
    }
    out[b.len] = '\0';
    WisunV.n = b.len;
}

/** @brief The operands and the outcome. */
WisunVars WisunV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WISUN
