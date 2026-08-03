// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gateway.c
 * @brief Radio / wireless gateway bridge - implementation.
 *
 * A static port table; pc_gateway_uplink() envelopes a received frame and publishes it through
 * the installed northbound callback (per-port rate-capped, fail-closed), pc_gateway_downlink()
 * routes a command to a port's transmit callback, and pc_gateway_topic() formats a routing key.
 * Zero heap.
 */

#include "services/net/gateway/gateway.h"

#if PC_ENABLE_GATEWAY

#include <string.h>

#if PROTOCORE_HOT
#include "server/clock/clock.h" // pc_millis()
#endif

typedef struct
{
    pc_gateway_tx_fn tx;
    void *ctx;
    uint32_t window_start; // ms of the current uplink rate window
    uint16_t rate_cap;     // uplink frames per second (0 = unlimited)
    uint16_t count;        // uplinks in the current window
    uint8_t id;
    pc_gateway_kind kind;
    proto_bool used;
} port;

// All gateway state, owned by one instance (internal linkage): the port table, the
// northbound uplink callback + context, the topic prefix, the uplink sequence, and stats,
// grouped so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    port ports[PC_GW_MAX_PORTS];
    pc_gateway_uplink_fn uplink;
    void *uplink_ctx;
    const char *prefix;
    uint32_t seq;
    pc_gateway_stats stats;
#if !PROTOCORE_HOT
    uint32_t now_ms; // host test clock (real builds use pc_millis())
#endif
} GatewayCtx;
static GatewayCtx s_gw = {.prefix = PC_GW_DEFAULT_PREFIX};

#if PROTOCORE_HOT
static uint32_t gw_now()
{
    return pc_millis();
}
#else
static uint32_t gw_now()
{
    return s_gw.now_ms;
}
#endif

// Returns a mutable port (callers mutate it), so it takes the owner by non-const reference.
static port *find_port(GatewayCtx &g, uint8_t id)
{
    for (uint8_t i = 0; i < PC_GW_MAX_PORTS; i++)
    {
        if (g.ports[i].used && g.ports[i].id == id)
        {
            return &g.ports[i];
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

void pc_gateway_reset(void)
{
    memset(s_gw.ports, 0, sizeof(s_gw.ports));
    s_gw.uplink = NULL;
    s_gw.uplink_ctx = NULL;
    s_gw.prefix = PC_GW_DEFAULT_PREFIX;
    s_gw.seq = 0;
    memset(&s_gw.stats, 0, sizeof(s_gw.stats));
}

proto_bool pc_gateway_add_port(const pc_gateway_port_config *cfg)
{
    if (!cfg || find_port(s_gw, cfg->port_id))
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < PC_GW_MAX_PORTS; i++)
    {
        if (s_gw.ports[i].used)
        {
            continue;
        }
        s_gw.ports[i].tx = cfg->tx;
        s_gw.ports[i].ctx = cfg->ctx;
        s_gw.ports[i].window_start = 0;
        s_gw.ports[i].rate_cap = cfg->rate_cap;
        s_gw.ports[i].count = 0;
        s_gw.ports[i].id = cfg->port_id;
        s_gw.ports[i].kind = cfg->kind;
        s_gw.ports[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // table full
}

void pc_gateway_set_uplink_cb(pc_gateway_uplink_fn fn, void *ctx)
{
    s_gw.uplink = fn;
    s_gw.uplink_ctx = ctx;
}

void pc_gateway_set_topic_prefix(const char *prefix)
{
    s_gw.prefix = prefix ? prefix : PC_GW_DEFAULT_PREFIX;
}

proto_bool pc_gateway_uplink(uint8_t port_id, uint16_t src_addr, const uint8_t *payload, uint16_t len, int16_t rssi)
{
    s_gw.stats.up_in++;
    port *p = find_port(s_gw, port_id);
    if (!p || !s_gw.uplink || rate_exceeded(p))
    {
        s_gw.stats.up_dropped++;
        return PROTO_FALSE;
    }
    pc_gateway_msg msg;
    msg.payload = payload;
    msg.seq = s_gw.seq++;
    msg.len = len;
    msg.src_addr = src_addr;
    msg.rssi = rssi;
    msg.port_id = port_id;
    msg.kind = p->kind;
    if (s_gw.uplink(&msg, s_gw.uplink_ctx))
    {
        s_gw.stats.up_published++;
        return PROTO_TRUE;
    }
    s_gw.stats.up_dropped++;
    return PROTO_FALSE;
}

proto_bool pc_gateway_downlink(uint8_t port_id, uint16_t dst_addr, const uint8_t *payload, uint16_t len)
{
    s_gw.stats.down_in++;
    port *p = find_port(s_gw, port_id);
    if (!p || !p->tx || !p->tx(port_id, dst_addr, payload, len, p->ctx))
    {
        s_gw.stats.down_dropped++;
        return PROTO_FALSE;
    }
    s_gw.stats.down_sent++;
    return PROTO_TRUE;
}

uint16_t pc_gateway_topic(const pc_gateway_msg *msg, char *buf, uint16_t buflen)
{
    if (!msg || !buf || buflen == 0)
    {
        return 0;
    }
    uint16_t pos = 0;
    for (const char *s = s_gw.prefix; *s; s++)
    {
        if (!put_ch(buf, &pos, buflen, *s))
        {
            return 0;
        }
    }
    // Sequential, not one `||` chain: the two '/' separators are written at different
    // positions (put_ch advances pos), so writing them as separate steps keeps each
    // append distinct rather than repeating an identical-looking subexpression.
    if (!put_ch(buf, &pos, buflen, '/'))
    {
        return 0;
    }
    if (!put_u32(buf, &pos, buflen, msg->port_id))
    {
        return 0;
    }
    if (!put_ch(buf, &pos, buflen, '/'))
    {
        return 0;
    }
    if (!put_u32(buf, &pos, buflen, msg->src_addr))
    {
        return 0;
    }
    buf[pos] = '\0';
    return pos;
}

void pc_gateway_get_stats(pc_gateway_stats *out)
{
    if (out)
    {
        *out = s_gw.stats;
    }
}

#if !PROTOCORE_HOT
void pc_gateway_test_set_now(uint32_t ms)
{
    s_gw.now_ms = ms;
}
#endif

#endif // PC_ENABLE_GATEWAY
