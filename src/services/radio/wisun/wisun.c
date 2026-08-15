// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wisun.c
 * @brief Wi-SUN FAN border-router connector (see wisun.h).
 */

#include "services/radio/wisun/wisun.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_WISUN

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

size_t protocore_wisun_build_coap(uint8_t type, uint8_t code, uint16_t msg_id, const uint8_t *token, uint8_t tkl,
                                  const char *uri_path, const uint8_t *payload, size_t plen, uint8_t *out, size_t cap)
{
    if (!out || tkl > 8 || (tkl && !token) || (plen && !payload))
    {
        return 0;
    }
    size_t o = 0;
    if (cap < (size_t)(4 + tkl))
    {
        return 0;
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
            return 0;
        }
        last = 11;
    }

    if (plen)
    {
        if (o + 1 + plen > cap)
        {
            return 0;
        }
        out[o++] = 0xFF; // payload marker
        for (size_t i = 0; i < plen; i++)
        {
            out[o++] = payload[i];
        }
    }
    return o;
}

void protocore_wisun_init(WisunFan *fan, const protocore_ip *border_router, WisunNode *storage, size_t cap)
{
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

int protocore_wisun_node_register(WisunFan *fan, const protocore_ip *addr, uint32_t now)
{
    if (!fan || !fan->nodes || !addr)
    {
        return -1;
    }
    size_t idx = 0;
    if (protocore_wisun_node_find(fan, addr, &idx))
    {
        fan->nodes[idx].joined = PROTO_TRUE;
        fan->nodes[idx].last_seen = now;
        return (int)idx;
    }
    if (fan->count >= fan->cap)
    {
        return -1;
    }
    fan->nodes[fan->count].addr = *addr;
    fan->nodes[fan->count].joined = PROTO_TRUE;
    fan->nodes[fan->count].last_seen = now;
    return (int)fan->count++;
}

proto_bool protocore_wisun_node_find(const WisunFan *fan, const protocore_ip *addr, size_t *idx)
{
    if (!fan || !fan->nodes || !addr)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < fan->count; i++)
    {
        Ip.args.ip = &fan->nodes[i].addr;
        Ip.args.b = addr;
        Ip.equal(Ip.internal);
        if (Ip.ok)
        {
            if (idx)
            {
                *idx = i;
            }
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

size_t protocore_wisun_joined_count(const WisunFan *fan)
{
    if (!fan || !fan->nodes)
    {
        return 0;
    }
    size_t c = 0;
    for (size_t i = 0; i < fan->count; i++)
    {
        if (fan->nodes[i].joined)
        {
            c++;
        }
    }
    return c;
}

size_t protocore_wisun_nodes_json(const WisunFan *fan, char *out, size_t cap)
{
    if (!fan || !out || cap == 0)
    {
        return 0;
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
        Ip.args.ip = &fan->nodes[i].addr;
        Ip.args.buf = astr;
        Ip.args.cap = sizeof(astr);
        Ip.format(Ip.internal);
        Sb.put(&b, "{\"addr\":\"");
        Sb.put(&b, astr);
        Sb.put(&b, "\",\"joined\":");
        Sb.put(&b, fan->nodes[i].joined ? "true" : "false");
        Sb.put(&b, "}");
    }
    Sb.put(&b, "]");
    if (!b.ok)
    {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}

#endif // PROTOCORE_ENABLE_WISUN
