// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_IFACE_BRIDGE

#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/net/iface_bridge/iface_bridge/iface_bridge.h"
#include "shared/ip/ip.h"

PROTOCORE_BEGIN_DECLS

/// The one owned mutable: the address:port -> bus rule table.
typedef struct
{
    BridgeRule rules[PROTOCORE_BRIDGE_MAX_RULES];
    uint8_t count;
} BridgeCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define IFACE_BRIDGE_OFF_CTX 0u
static_assert(IFACE_BRIDGE_OFF_CTX + sizeof(BridgeCtx) <= PROTOCORE_IFACE_BRIDGE_BORROW,
              "PROTOCORE_IFACE_BRIDGE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    IFACE_BRIDGE_OFF_CTX % _Alignof(BridgeCtx) == 0,
    "IFACE_BRIDGE_OFF_CTX is not a multiple of alignof(BridgeCtx) - IFACE_BRIDGE_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define IFACE_BRIDGE_CTX(w) ((BridgeCtx *)(void *)((w) + IFACE_BRIDGE_OFF_CTX))

// The entries this file calls before reaching their definitions.
void protocore_iface_bridge_find(uint8_t *restrict work);

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_IFACE_BRIDGE_BORROW persistent bytes
} IfaceBridgeOwnCtx;
static IfaceBridgeOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_iface_bridge_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_IFACE_BRIDGE_BORROW).buf;
    }
    return s_own.span;
}

void protocore_iface_bridge_clear(uint8_t *restrict work)
{

    for (uint8_t i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        IFACE_BRIDGE_CTX(work)->rules[i].used = PROTO_FALSE;
    }
    IFACE_BRIDGE_CTX(work)->count = 0;
}

void protocore_iface_bridge_add(uint8_t *restrict work)
{
    const BridgeRule *rule = IfaceBridgeV.add_args.rule;

    if (!rule)
    {
        IfaceBridgeV.ok = PROTO_FALSE;
        return;
    }
    IfaceBridgeV.find_args.port = rule->listen_port;
    IfaceBridgeV.find_args.proto = rule->proto;
    protocore_iface_bridge_find(work);
    if (IfaceBridgeV.rule)
    {
        IfaceBridgeV.ok = PROTO_FALSE;
        return; // a rule already binds this port+proto
    }
    for (uint8_t i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        if (!IFACE_BRIDGE_CTX(work)->rules[i].used)
        {
            IFACE_BRIDGE_CTX(work)->rules[i] = *rule;
            IFACE_BRIDGE_CTX(work)->rules[i].used = PROTO_TRUE;
            IFACE_BRIDGE_CTX(work)->count++;
            IfaceBridgeV.ok = PROTO_TRUE;
            return;
        }
    }
    IfaceBridgeV.ok = PROTO_FALSE;
    return; // table full
}

void protocore_iface_bridge_map(uint8_t *restrict work)
{
    (void)work;
    const char *ip = IfaceBridgeV.map_args.ip;
    uint16_t port = IfaceBridgeV.map_args.port;
    BridgeProto proto = IfaceBridgeV.map_args.proto;
    const BridgeTarget *target = IfaceBridgeV.map_args.target;

    if (!target)
    {
        IfaceBridgeV.ok = PROTO_FALSE;
        return;
    }
    BridgeRule r;
    mem.set(&r, 0, sizeof(r));
    r.listen_ip.family = PROTOCORE_IP_NONE; // "any interface" unless a valid address is given
    if (ip && ip[0])
    {
        Ip.args.text = ip;
        Ip.args.out = &r.listen_ip;
        Ip.parse(ip_work);
        if (!Ip.ok)
        {
            IfaceBridgeV.ok = PROTO_FALSE;
            return; // malformed bind address
        }
    }
    r.listen_port = port;
    r.proto = proto;
    r.target = *target;
    IfaceBridgeV.add_args.rule = &r;
    protocore_iface_bridge_add(work);
}

void protocore_iface_bridge_find(uint8_t *restrict work)
{
    uint16_t port = IfaceBridgeV.find_args.port;
    BridgeProto proto = IfaceBridgeV.find_args.proto;

    for (uint8_t i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        if (IFACE_BRIDGE_CTX(work)->rules[i].used && IFACE_BRIDGE_CTX(work)->rules[i].listen_port == port &&
            IFACE_BRIDGE_CTX(work)->rules[i].proto == proto)
        {
            IfaceBridgeV.rule = &IFACE_BRIDGE_CTX(work)->rules[i];
            return;
        }
    }
    IfaceBridgeV.rule = NULL;
}

void protocore_iface_bridge_count(uint8_t *restrict work)
{

    IfaceBridgeV.u8 = IFACE_BRIDGE_CTX(work)->count;
}

void protocore_iface_bridge_txn_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = IfaceBridgeV.txn_parse_args.buf;
    size_t len = IfaceBridgeV.txn_parse_args.len;
    uint16_t *write_len = IfaceBridgeV.txn_parse_args.write_len;
    uint16_t *read_len = IfaceBridgeV.txn_parse_args.read_len;
    const uint8_t **write_data = IfaceBridgeV.txn_parse_args.write_data;

    if (!buf || len < (size_t)PROTOCORE_BRIDGE_TXN_HDR)
    {
        IfaceBridgeV.n = 0;
        return;
    }
    uint16_t wl = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    uint16_t rl = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    if (len < (size_t)PROTOCORE_BRIDGE_TXN_HDR + wl)
    {
        IfaceBridgeV.n = 0;
        return; // the write payload has not fully arrived yet
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
        *write_data = buf + PROTOCORE_BRIDGE_TXN_HDR;
    }
    IfaceBridgeV.n = (size_t)PROTOCORE_BRIDGE_TXN_HDR + wl;
}

void protocore_iface_bridge_txn_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = IfaceBridgeV.txn_build_args.out;
    size_t cap = IfaceBridgeV.txn_build_args.cap;
    const uint8_t *write_data = IfaceBridgeV.txn_build_args.write_data;
    uint16_t write_len = IfaceBridgeV.txn_build_args.write_len;
    uint16_t read_len = IfaceBridgeV.txn_build_args.read_len;

    size_t need = (size_t)PROTOCORE_BRIDGE_TXN_HDR + write_len;
    if (!out || cap < need)
    {
        IfaceBridgeV.n = 0;
        return;
    }
    out[0] = (uint8_t)(write_len >> 8);
    out[1] = (uint8_t)write_len;
    out[2] = (uint8_t)(read_len >> 8);
    out[3] = (uint8_t)read_len;
    if (write_len && write_data)
    {
        mem.cpy(out + PROTOCORE_BRIDGE_TXN_HDR, write_data, write_len);
    }
    IfaceBridgeV.n = need;
}

/** @brief The operands and the outcome. */
IfaceBridgeVars IfaceBridgeV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IFACE_BRIDGE
