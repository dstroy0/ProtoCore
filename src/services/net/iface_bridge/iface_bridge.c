// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "services/net/iface_bridge/iface_bridge.h"

#if PC_ENABLE_IFACE_BRIDGE

#include <string.h>

/// The one owned mutable: the address:port -> bus rule table.
typedef struct
{
    BridgeRule rules[PC_BRIDGE_MAX_RULES];
    uint8_t count;
} BridgeCtx;
static BridgeCtx s_bridge;

void pc_iface_bridge_clear()
{
    for (uint8_t i = 0; i < PC_BRIDGE_MAX_RULES; i++)
    {
        s_bridge.rules[i].used = PROTO_FALSE;
    }
    s_bridge.count = 0;
}

proto_bool pc_iface_bridge_add(const BridgeRule *rule)
{
    if (!rule)
    {
        return PROTO_FALSE;
    }
    if (pc_iface_bridge_find(rule->listen_port, rule->proto))
    {
        return PROTO_FALSE; // a rule already binds this port+proto
    }
    for (uint8_t i = 0; i < PC_BRIDGE_MAX_RULES; i++)
    {
        if (!s_bridge.rules[i].used)
        {
            s_bridge.rules[i] = *rule;
            s_bridge.rules[i].used = PROTO_TRUE;
            s_bridge.count++;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE; // table full
}

proto_bool pc_iface_bridge_map(const char *ip, uint16_t port, BridgeProto proto, const BridgeTarget *target)
{
    if (!target)
    {
        return PROTO_FALSE;
    }
    BridgeRule r;
    memset(&r, 0, sizeof(r));
    r.listen_ip.family = pc_ip_family::PC_IP_NONE; // "any interface" unless a valid address is given
    if (ip && ip[0] && !pc_ip_parse(ip, &r.listen_ip))
    {
        return PROTO_FALSE; // malformed bind address
    }
    r.listen_port = port;
    r.proto = proto;
    r.target = *target;
    return pc_iface_bridge_add(&r);
}

const BridgeRule *pc_iface_bridge_find(uint16_t port, BridgeProto proto)
{
    for (uint8_t i = 0; i < PC_BRIDGE_MAX_RULES; i++)
    {
        if (s_bridge.rules[i].used && s_bridge.rules[i].listen_port == port && s_bridge.rules[i].proto == proto)
        {
            return &s_bridge.rules[i];
        }
    }
    return NULL;
}

uint8_t pc_iface_bridge_count()
{
    return s_bridge.count;
}

size_t pc_iface_bridge_txn_parse(const uint8_t *buf, size_t len, uint16_t *write_len, uint16_t *read_len,
                                 const uint8_t **write_data)
{
    if (!buf || len < (size_t)PC_BRIDGE_TXN_HDR)
    {
        return 0;
    }
    uint16_t wl = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    uint16_t rl = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    if (len < (size_t)PC_BRIDGE_TXN_HDR + wl)
    {
        return 0; // the write payload has not fully arrived yet
    }
    if (write_len)
    {
        *write_len = wl;
    }
    if (read_len)
    {
        *read_len = rl;
    }
    if (write_data)
    {
        *write_data = buf + PC_BRIDGE_TXN_HDR;
    }
    return (size_t)PC_BRIDGE_TXN_HDR + wl;
}

size_t pc_iface_bridge_txn_build(uint8_t *out, size_t cap, const uint8_t *write_data, uint16_t write_len,
                                 uint16_t read_len)
{
    size_t need = (size_t)PC_BRIDGE_TXN_HDR + write_len;
    if (!out || cap < need)
    {
        return 0;
    }
    out[0] = (uint8_t)(write_len >> 8);
    out[1] = (uint8_t)write_len;
    out[2] = (uint8_t)(read_len >> 8);
    out[3] = (uint8_t)read_len;
    if (write_len && write_data)
    {
        memcpy(out + PC_BRIDGE_TXN_HDR, write_data, write_len);
    }
    return need;
}

#endif // PC_ENABLE_IFACE_BRIDGE
