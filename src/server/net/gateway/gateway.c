// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gateway.c
 * @brief Radio / wireless gateway bridge - implementation.
 *
 * A static port table; protocore_gateway_uplink() envelopes a received frame and publishes it through
 * the installed northbound callback (per-port rate-capped, fail-closed), protocore_gateway_downlink()
 * routes a command to a port's transmit callback, and protocore_gateway_topic() formats a routing key.
 * Zero heap.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_GATEWAY

#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/net/gateway/gateway.h"

#include "server/clock/clock.h" // protocore_millis(): the one time source the rate window reads

PROTOCORE_BEGIN_DECLS

typedef struct
{
    protocore_gateway_tx_fn tx;
    void *ctx;
    uint32_t window_start; // ms of the current uplink rate window
    uint16_t rate_cap;     // uplink frames per second (0 = unlimited)
    uint16_t count;        // uplinks in the current window
    uint8_t id;
    protocore_gateway_kind kind;
    proto_bool used;
} port;

// All gateway state, owned by one instance (internal linkage): the port table, the
// northbound uplink callback + context, the topic prefix, the uplink sequence, and stats,
// grouped so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    port ports[PROTOCORE_GW_MAX_PORTS];
    protocore_gateway_uplink_fn uplink;
    void *uplink_ctx;
    const char *prefix;
    uint32_t seq;
    protocore_gateway_stats stats;
} GatewayCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define GATEWAY_OFF_CTX 0u
static_assert(GATEWAY_OFF_CTX + sizeof(GatewayCtx) <= PROTOCORE_GATEWAY_BORROW,
              "PROTOCORE_GATEWAY_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define GATEWAY_CTX(w) ((GatewayCtx *)(void *)((w) + GATEWAY_OFF_CTX))

// The one time source (server/clock/clock.h). A caller that needs to drive the rate window - a
// test stepping it - installs its own clock with protocore_set_clock(), which governs every module.
static uint32_t gw_now()
{
    Clock.millis(Clock.internal); // take a reading; Clock.ms is where the last one landed
    return Clock.ms;
}

// Returns a mutable port (callers mutate it), so it takes the owner by non-const reference.
static port *find_port(GatewayCtx *g, uint8_t id)
{
    for (uint8_t i = 0; i < PROTOCORE_GW_MAX_PORTS; i++)
    {
        if (g->ports[i].used && g->ports[i].id == id)
        {
            return &g->ports[i];
        }
    }
    return NULL;
}

// Fixed 1-second window uplink rate cap; fail-closed (true = drop) once the cap is hit.
static proto_bool rate_exceeded(port *p)
{
    if (p->rate_cap == 0)
    {
        return PROTO_FALSE;
    }
    uint32_t now = gw_now();
    if ((uint32_t)(now - p->window_start) >= 1000)
    {
        p->window_start = now;
        p->count = 0;
    }
    if (p->count >= p->rate_cap)
    {
        return PROTO_TRUE;
    }
    p->count++;
    return PROTO_FALSE;
}

static proto_bool put_ch(char *buf, uint16_t *pos, uint16_t cap, char c)
{
    if ((uint16_t)(*pos + 1) >= cap) // keep room for the NUL
    {
        return PROTO_FALSE;
    }
    buf[(*pos)++] = c;
    return PROTO_TRUE;
}

static proto_bool put_u32(char *buf, uint16_t *pos, uint16_t cap, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0;
    if (v == 0)
    {
        tmp[n++] = '0';
    }
    while (v)
    {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
    {
        if (!put_ch(buf, pos, cap, tmp[--n]))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_GATEWAY_BORROW persistent bytes
} GatewayOwnCtx;
static GatewayOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_gateway_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_GATEWAY_BORROW).buf;
    }
    return s_own.span;
}

static void gateway_reset(uint8_t *restrict work)
{

    mem.set(GATEWAY_CTX(work)->ports, 0, sizeof(GATEWAY_CTX(work)->ports));
    GATEWAY_CTX(work)->uplink = NULL;
    GATEWAY_CTX(work)->uplink_ctx = NULL;
    GATEWAY_CTX(work)->prefix = PROTOCORE_GW_DEFAULT_PREFIX;
    GATEWAY_CTX(work)->seq = 0;
    mem.set(&GATEWAY_CTX(work)->stats, 0, sizeof(GATEWAY_CTX(work)->stats));
}

static void gateway_add_port(uint8_t *restrict work)
{
    const protocore_gateway_port_config *cfg = Gateway.add_port_args.cfg;

    if (!cfg || find_port(GATEWAY_CTX(work), cfg->port_id))
    {
        Gateway.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < PROTOCORE_GW_MAX_PORTS; i++)
    {
        if (GATEWAY_CTX(work)->ports[i].used)
        {
            continue;
        }
        GATEWAY_CTX(work)->ports[i].tx = cfg->tx;
        GATEWAY_CTX(work)->ports[i].ctx = cfg->ctx;
        GATEWAY_CTX(work)->ports[i].window_start = 0;
        GATEWAY_CTX(work)->ports[i].rate_cap = cfg->rate_cap;
        GATEWAY_CTX(work)->ports[i].count = 0;
        GATEWAY_CTX(work)->ports[i].id = cfg->port_id;
        GATEWAY_CTX(work)->ports[i].kind = cfg->kind;
        GATEWAY_CTX(work)->ports[i].used = PROTO_TRUE;
        Gateway.ok = PROTO_TRUE;
        return;
    }
    Gateway.ok = PROTO_FALSE;
    return; // table full
}

static void gateway_set_uplink_cb(uint8_t *restrict work)
{
    protocore_gateway_uplink_fn fn = Gateway.set_uplink_cb_args.fn;
    void *ctx = Gateway.set_uplink_cb_args.ctx;

    GATEWAY_CTX(work)->uplink = fn;
    GATEWAY_CTX(work)->uplink_ctx = ctx;
}

static void gateway_set_topic_prefix(uint8_t *restrict work)
{
    const char *prefix = Gateway.set_topic_prefix_args.prefix;

    GATEWAY_CTX(work)->prefix = prefix ? prefix : PROTOCORE_GW_DEFAULT_PREFIX;
}

static void gateway_uplink(uint8_t *restrict work)
{
    uint8_t port_id = Gateway.uplink_args.port_id;
    uint16_t src_addr = Gateway.uplink_args.src_addr;
    const uint8_t *payload = Gateway.uplink_args.payload;
    uint16_t len = Gateway.uplink_args.len;
    int16_t rssi = Gateway.uplink_args.rssi;

    GATEWAY_CTX(work)->stats.up_in++;
    port *p = find_port(GATEWAY_CTX(work), port_id);
    if (!p || !GATEWAY_CTX(work)->uplink || rate_exceeded(p))
    {
        GATEWAY_CTX(work)->stats.up_dropped++;
        Gateway.ok = PROTO_FALSE;
        return;
    }
    protocore_gateway_msg msg;
    msg.payload = payload;
    msg.seq = GATEWAY_CTX(work)->seq++;
    msg.len = len;
    msg.src_addr = src_addr;
    msg.rssi = rssi;
    msg.port_id = port_id;
    msg.kind = p->kind;
    if (GATEWAY_CTX(work)->uplink(&msg, GATEWAY_CTX(work)->uplink_ctx))
    {
        GATEWAY_CTX(work)->stats.up_published++;
        Gateway.ok = PROTO_TRUE;
        return;
    }
    GATEWAY_CTX(work)->stats.up_dropped++;
    Gateway.ok = PROTO_FALSE;
}

static void gateway_downlink(uint8_t *restrict work)
{
    uint8_t port_id = Gateway.downlink_args.port_id;
    uint16_t dst_addr = Gateway.downlink_args.dst_addr;
    const uint8_t *payload = Gateway.downlink_args.payload;
    uint16_t len = Gateway.downlink_args.len;

    GATEWAY_CTX(work)->stats.down_in++;
    port *p = find_port(GATEWAY_CTX(work), port_id);
    if (!p || !p->tx || !p->tx(port_id, dst_addr, payload, len, p->ctx))
    {
        GATEWAY_CTX(work)->stats.down_dropped++;
        Gateway.ok = PROTO_FALSE;
        return;
    }
    GATEWAY_CTX(work)->stats.down_sent++;
    Gateway.ok = PROTO_TRUE;
}

static void gateway_topic(uint8_t *restrict work)
{
    const protocore_gateway_msg *msg = Gateway.topic_args.msg;
    char *buf = Gateway.topic_args.buf;
    uint16_t buflen = Gateway.topic_args.buflen;

    if (!msg || !buf || buflen == 0)
    {
        Gateway.n = 0;
        return;
    }
    uint16_t pos = 0;
    // Null is "never set", which is the default - stated here rather than on the declaration so
    // the context carries no initializer and can live in a borrow that arrives zeroed.
    const char *prefix = GATEWAY_CTX(work)->prefix ? GATEWAY_CTX(work)->prefix : PROTOCORE_GW_DEFAULT_PREFIX;
    for (const char *s = prefix; *s; s++)
    {
        if (!put_ch(buf, &pos, buflen, *s))
        {
            Gateway.n = 0;
            return;
        }
    }
    // Sequential, not one `||` chain: the two '/' separators are written at different
    // positions (put_ch advances pos), so writing them as separate steps keeps each
    // append distinct rather than repeating an identical-looking subexpression.
    if (!put_ch(buf, &pos, buflen, '/'))
    {
        Gateway.n = 0;
        return;
    }
    if (!put_u32(buf, &pos, buflen, msg->port_id))
    {
        Gateway.n = 0;
        return;
    }
    if (!put_ch(buf, &pos, buflen, '/'))
    {
        Gateway.n = 0;
        return;
    }
    if (!put_u32(buf, &pos, buflen, msg->src_addr))
    {
        Gateway.n = 0;
        return;
    }
    buf[pos] = '\0';
    Gateway.n = pos;
}

static void gateway_get_stats(uint8_t *restrict work)
{
    protocore_gateway_stats *out = Gateway.get_stats_args.out;

    if (out)
    {
        *out = GATEWAY_CTX(work)->stats;
    }
}

GatewayNs Gateway = {.reset = gateway_reset,
                     .add_port = gateway_add_port,
                     .set_uplink_cb = gateway_set_uplink_cb,
                     .set_topic_prefix = gateway_set_topic_prefix,
                     .uplink = gateway_uplink,
                     .downlink = gateway_downlink,
                     .topic = gateway_topic,
                     .get_stats = gateway_get_stats};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GATEWAY
