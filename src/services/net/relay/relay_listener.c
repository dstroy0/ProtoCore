// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay_listener.c
 * @brief Server-side TCP relay / DNAT listener (see relay_listener.h). Bridges a ConnProto::PROTO_RELAY
 *        connection to an origin pc_client connection via the pure relay engine.
 */

#include "relay_listener.h"

#if PC_ENABLE_RELAY

#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
#include "network_drivers/transport/tcp.h"
#include "relay.h"
#if PC_ENABLE_RADIO_POWER
#include "network_drivers/physical/radio_power.h" // keep the radio awake during a relayed transfer
#endif

// One published front port -> origin.
typedef struct
{
    proto_bool active;
    uint8_t listener_id;
    char host[PC_RELAY_HOST_MAX];
    uint16_t port;
} RelayBind;

// One live relayed connection: an inbound conn slot bridged to an origin pc_client.
typedef struct
{
    proto_bool active;
    uint8_t conn_slot;
    int origin_cid;
    pc_relay relay;
} RelayBridge;

// All of the listener's mutable state in one owned, feature-gated context (least-privilege; the
// owner-context guard requires the single file-scope mutable to be a `*Ctx` instance).
typedef struct
{
    RelayBind binds[PC_RELAY_MAX_PUBLISH];
    RelayBridge bridges[PC_RELAY_MAX_CONNS];
    proto_bool registered;
} RelayListenerCtx;
static RelayListenerCtx s_ctx;

static RelayBind *bind_by_listener(uint8_t lid)
{
    for (int i = 0; i < PC_RELAY_MAX_PUBLISH; i++)
    {
        if (s_ctx.binds[i].active && s_ctx.binds[i].listener_id == lid)
        {
            return &s_ctx.binds[i];
        }
    }
    return NULL;
}

static RelayBridge *bridge_by_conn(uint8_t slot)
{
    for (int i = 0; i < PC_RELAY_MAX_CONNS; i++)
    {
        if (s_ctx.bridges[i].active && s_ctx.bridges[i].conn_slot == slot)
        {
            return &s_ctx.bridges[i];
        }
    }
    return NULL;
}

static int bridge_find_free()
{
    for (int i = 0; i < PC_RELAY_MAX_CONNS; i++)
    {
        if (!s_ctx.bridges[i].active)
        {
            return i;
        }
    }
    return -1;
}

// --- Relay seams (ctx = the RelayBridge). ---
// Inbound (a) = the accepted server connection; its EOF arrives out of band via relay_on_close.
static int a_recv(void *c, uint8_t *buf, size_t cap)
{
    RelayBridge *br = (RelayBridge *)c;
    if (pc_conn_available(br->conn_slot))
    {
        return (int)pc_conn_read(br->conn_slot, buf, cap);
    }
    return 0;
}
static int a_send(void *c, const uint8_t *buf, size_t len)
{
    RelayBridge *br = (RelayBridge *)c;
    // Send as much as the inbound TCP send window currently allows (partial), not all-or-nothing: a
    // whole PC_RELAY_BUF chunk rarely fits tcp_sndbuf in one shot, and a failed all-or-nothing send
    // forwards zero bytes and stalls the transfer. room==0 is real backpressure - the pump retries.
    proto_u16 room = pc_conn_sndbuf(br->conn_slot);
    if (room == 0)
    {
        return 0;
    }
    proto_u16 n = (len < (size_t)room) ? (proto_u16)len : room;
    return Tcp.conn->send(br->conn_slot, buf, n) ? (int)n : 0;
}
// Origin (b) = the outbound pc_client; it reports EOF through the recv seam.
static int b_recv(void *c, uint8_t *buf, size_t cap)
{
    RelayBridge *br = (RelayBridge *)c;
    size_t n = Tcp.client->read(br->origin_cid, buf, cap);
    if (n)
    {
        return (int)n;
    }
    return Tcp.client->is_closed(br->origin_cid) ? -1 : 0;
}
static int b_send(void *c, const uint8_t *buf, size_t len)
{
    RelayBridge *br = (RelayBridge *)c;
    return Tcp.client->send(br->origin_cid, buf, len) ? (int)len : 0;
}

// Close the origin (and optionally the inbound) and free the bridge. active=false first so a
// re-entrant close callback is a no-op.
static void teardown(RelayBridge *br, proto_bool close_inbound)
{
    br->active = PROTO_FALSE;
#if PC_ENABLE_RADIO_POWER
    Radio.busy_release(); // this bridge is done relaying
#endif
    Tcp.client->close(br->origin_cid);
    if (close_inbound)
    {
        Tcp.conn->close(br->conn_slot);
    }
}

// Pump the bridge one pass and tear it down if the origin ended or the pump errored.
static void service(uint8_t slot)
{
    RelayBridge *br = bridge_by_conn(slot);
    if (!br)
    {
        return;
    }
    // Drain as much as the buffers allow this pass: keep stepping while a step actually moves bytes,
    // so one poll forwards the whole buffered origin RX ring (PC_CLIENT_RX_BUF) instead of a single
    // PC_RELAY_BUF chunk. Bounded by PC_RELAY_DRAIN_MAX so one busy bridge cannot starve others.
    for (int pass = 0; pass < PC_RELAY_DRAIN_MAX; pass++)
    {
        uint32_t moved = br->relay.bytes_a2b + br->relay.bytes_b2a;
        pc_relay_status st = pc_relay_step(&br->relay);
        if (st == PC_RELAY_ERROR || st == PC_RELAY_DONE)
        {
            teardown(br, PROTO_TRUE);
            return;
        }
        if (br->relay.bytes_a2b + br->relay.bytes_b2a == moved)
        {
            break; // no progress this pass; nothing more buffered to move right now
        }
    }
    // origin closed and everything it sent has been forwarded -> nothing more to do
    if (Tcp.client->is_closed(br->origin_cid) && Tcp.client->available(br->origin_cid) == 0 &&
        br->relay.b2a_off >= br->relay.b2a_len)
    {
        teardown(br, PROTO_TRUE);
    }
}

static void relay_on_accept(uint8_t slot)
{
    RelayBind *bd = bind_by_listener(pc_conn_listener_id(slot));
    if (!bd)
    {
        Tcp.conn->close(slot); // no origin published for this listener
        return;
    }
    int idx = bridge_find_free();
    if (idx < 0)
    {
        Tcp.conn->close(slot); // bridge table full
        return;
    }
    int cid = Tcp.client->open(bd->host, bd->port, PC_RELAY_CONNECT_MS); // blocking connect (LAN origin)
    if (cid < 0)
    {
        Tcp.conn->close(slot); // origin unreachable
        return;
    }
    RelayBridge *br = &s_ctx.bridges[idx];
    br->active = PROTO_TRUE;
    br->conn_slot = slot;
    br->origin_cid = cid;
    pc_relay_end a = {a_recv, a_send, NULL, br};
    pc_relay_end b = {b_recv, b_send, NULL, br};
    pc_relay_init(&br->relay, &a, &b);
#if PC_ENABLE_RADIO_POWER
    Radio.busy_hold(); // hold the radio awake for the life of this bridge
#endif
}

static void relay_on_data(uint8_t slot)
{
    service(slot);
}

static void relay_on_poll(uint8_t slot)
{
    if (!pc_conn_active(slot))
    {
        return;
    }
    service(slot);
}

static void relay_on_close(uint8_t slot)
{
    RelayBridge *br = bridge_by_conn(slot);
    if (br)
    {
        teardown(br, PROTO_FALSE); // the transport already owns the closing inbound slot
    }
}

static const ProtoHandler s_relay_handler = {relay_on_accept, relay_on_data, relay_on_close, relay_on_poll};

proto_bool pc_relay_publish(uint8_t listener_id, const char *origin_host, uint16_t origin_port)
{
    if (!origin_host)
    {
        return PROTO_FALSE;
    }
    size_t hl = strnlen(origin_host, PC_RELAY_HOST_MAX + 1);
    if (hl == 0 || hl >= PC_RELAY_HOST_MAX)
    {
        return PROTO_FALSE;
    }
    int idx = -1;
    for (int i = 0; i < PC_RELAY_MAX_PUBLISH; i++)
    {
        if (!s_ctx.binds[i].active)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        return PROTO_FALSE;
    }
    s_ctx.binds[idx].active = PROTO_TRUE;
    s_ctx.binds[idx].listener_id = listener_id;
    memcpy(s_ctx.binds[idx].host, origin_host, hl + 1);
    s_ctx.binds[idx].port = origin_port;
    if (!s_ctx.registered)
    {
        proto_register(PROTO_RELAY, &s_relay_handler);
        s_ctx.registered = PROTO_TRUE;
    }
    return PROTO_TRUE;
}

void pc_relay_listener_reset(void)
{
    for (int i = 0; i < PC_RELAY_MAX_PUBLISH; i++)
    {
        s_ctx.binds[i].active = PROTO_FALSE;
    }
    for (int i = 0; i < PC_RELAY_MAX_CONNS; i++)
    {
        if (s_ctx.bridges[i].active)
        {
            s_ctx.bridges[i].active = PROTO_FALSE;
#if PC_ENABLE_RADIO_POWER
            Radio.busy_release(); // balance the hold taken when the bridge was opened
#endif
        }
    }
}

#endif // PC_ENABLE_RELAY
