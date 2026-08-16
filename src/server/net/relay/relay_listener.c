// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay_listener.c
 * @brief Server-side TCP relay / DNAT listener (see relay_listener.h). Bridges a ProtoConn::PROTO_RELAY
 *        connection to an origin protocore_client connection via the pure relay engine.
 */

#include "relay_listener.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_RELAY

#include "mmgr/protostr.h"
#include "network_drivers/session/session.h"                 // Session.proto->add: the handler registration
#include "network_drivers/transport/tcp/client/client.h"     // TcpClient: the dialed connection
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the accepted slot
#include "network_drivers/transport/tcp/tcp.h"
#include "relay.h"
#include "server/core/proto_handler.h"
#if PROTOCORE_ENABLE_RADIO_POWER
#include "network_drivers/physical/radio_power.h" // keep the radio awake during a relayed transfer
#endif

// One published front port -> origin.
typedef struct
{
    proto_bool active;
    uint8_t listener_id;
    char host[PROTOCORE_RELAY_HOST_MAX];
    uint16_t port;
} RelayBind;

// One live relayed connection: an inbound conn slot bridged to an origin protocore_client.
typedef struct
{
    proto_bool active;
    uint8_t conn_slot;
    int origin_cid;
    protocore_relay relay;
} RelayBridge;

// All of the listener's mutable state in one owned, feature-gated context (least-privilege; the
// owner-context guard requires the single file-scope mutable to be a `*Ctx` instance).
typedef struct
{
    RelayBind binds[PROTOCORE_RELAY_MAX_PUBLISH];
    RelayBridge bridges[PROTOCORE_RELAY_MAX_CONNS];
    proto_bool registered;
} RelayListenerCtx;
static RelayListenerCtx s_ctx;

static RelayBind *bind_by_listener(uint8_t lid)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
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
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
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
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
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
    ConnPool.slot = br->conn_slot;
    ConnPool.available(ConnPool.internal);
    if (ConnPool.n)
    {
        ConnPool.slot = br->conn_slot;
        ConnPool.io.buf = buf;
        ConnPool.io.cap = cap;
        ConnPool.read(ConnPool.internal);
        return (int)ConnPool.n;
    }
    return 0;
}
static int a_send(void *c, const uint8_t *buf, size_t len)
{
    RelayBridge *br = (RelayBridge *)c;
    // Send as much as the inbound TCP send window currently allows (partial), not all-or-nothing: a
    // whole PROTOCORE_RELAY_BUF chunk rarely fits tcp_sndbuf in one shot, and a failed all-or-nothing send
    // forwards zero bytes and stalls the transfer. room==0 is real backpressure - the pump retries.
    ConnPool.slot = br->conn_slot;
    ConnPool.sndbuf(ConnPool.internal);
    proto_u16 room = ConnPool.u16;
    if (room == 0)
    {
        return 0;
    }
    proto_u16 n = (len < (size_t)room) ? (proto_u16)len : room;
    ConnPool.slot = br->conn_slot;
    ConnPool.io.data = buf;
    ConnPool.io.len = n;
    ConnPool.send(ConnPool.internal);
    return ConnPool.ok ? (int)n : 0;
}
// Origin (b) = the outbound protocore_client; it reports EOF through the recv seam.
static int b_recv(void *c, uint8_t *buf, size_t cap)
{
    RelayBridge *br = (RelayBridge *)c;
    TcpClient.cid = br->origin_cid;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = cap;
    TcpClient.read(TcpClient.internal);
    size_t n = TcpClient.n;
    if (n)
    {
        return (int)n;
    }
    TcpClient.cid = br->origin_cid;
    TcpClient.is_closed(TcpClient.internal);
    return TcpClient.ok ? -1 : 0;
}
static int b_send(void *c, const uint8_t *buf, size_t len)
{
    RelayBridge *br = (RelayBridge *)c;
    TcpClient.cid = br->origin_cid;
    TcpClient.io.data = buf;
    TcpClient.io.len = len;
    TcpClient.send(TcpClient.internal);
    return TcpClient.ok ? (int)len : 0;
}

// Close the origin (and optionally the inbound) and free the bridge. active=false first so a
// re-entrant close callback is a no-op.
static void teardown(RelayBridge *br, proto_bool close_inbound)
{
    br->active = PROTO_FALSE;
#if PROTOCORE_ENABLE_RADIO_POWER
    Radio.busy_release(Radio.internal); // this bridge is done relaying
#endif
    TcpClient.cid = br->origin_cid;
    TcpClient.close(TcpClient.internal);
    if (close_inbound)
    {
        ConnPool.slot = br->conn_slot;
        ConnPool.close(ConnPool.internal);
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
    // so one poll forwards the whole buffered origin RX ring (PROTOCORE_CLIENT_RX_BUF) instead of a single
    // PROTOCORE_RELAY_BUF chunk. Bounded by PROTOCORE_RELAY_DRAIN_MAX so one busy bridge cannot starve others.
    for (int pass = 0; pass < PROTOCORE_RELAY_DRAIN_MAX; pass++)
    {
        uint32_t moved = br->relay.bytes_a2b + br->relay.bytes_b2a;
        protocore_relay_status st = protocore_relay_step(&br->relay);
        if (st == PROTOCORE_RELAY_ERROR || st == PROTOCORE_RELAY_DONE)
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
    TcpClient.cid = br->origin_cid;
    TcpClient.is_closed(TcpClient.internal);
    const proto_bool origin_closed = TcpClient.ok;
    TcpClient.cid = br->origin_cid;
    TcpClient.available(TcpClient.internal);
    if (origin_closed && TcpClient.n == 0 && br->relay.b2a_off >= br->relay.b2a_len)
    {
        teardown(br, PROTO_TRUE);
    }
}

static void relay_on_accept(uint8_t slot)
{
    ConnPool.slot = slot;
    ConnPool.listener_id(ConnPool.internal);
    RelayBind *bd = bind_by_listener(ConnPool.u8);
    if (!bd)
    {
        ConnPool.slot = slot;
        ConnPool.close(ConnPool.internal); // no origin published for this listener
        return;
    }
    int idx = bridge_find_free();
    if (idx < 0)
    {
        ConnPool.slot = slot;
        ConnPool.close(ConnPool.internal); // bridge table full
        return;
    }
    // open() takes a slot and returns; the origin is not up yet. The bridge arms anyway: the pump
    // steps the connect through b_recv's is_closed, a send before it is up reads as backpressure and
    // retries, and the slot's own PROTOCORE_RELAY_CONNECT_MS is what ends an origin that never answers.
    TcpClient.dial.host = bd->host;
    TcpClient.dial.port = bd->port;
    TcpClient.dial.timeout_ms = PROTOCORE_RELAY_CONNECT_MS;
    TcpClient.open(TcpClient.internal);
    int cid = TcpClient.i32;
    if (cid < 0)
    {
        ConnPool.slot = slot;
        ConnPool.close(ConnPool.internal); // no free client slot
        return;
    }
    RelayBridge *br = &s_ctx.bridges[idx];
    br->active = PROTO_TRUE;
    br->conn_slot = slot;
    br->origin_cid = cid;
    protocore_relay_end a = {a_recv, a_send, NULL, br};
    protocore_relay_end b = {b_recv, b_send, NULL, br};
    protocore_relay_init(&br->relay, &a, &b);
#if PROTOCORE_ENABLE_RADIO_POWER
    Radio.busy_hold(Radio.internal); // hold the radio awake for the life of this bridge
#endif
}

static void relay_on_data(uint8_t slot)
{
    service(slot);
}

static void relay_on_poll(uint8_t slot)
{
    ConnPool.slot = slot;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok)
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

// Designated, so a member's position in the struct does not decide what it binds to. on_abort is
// unset: a null one falls back to on_close.
static const ProtoHandler s_relay_handler = {
    .on_accept = relay_on_accept, .on_data = relay_on_data, .on_close = relay_on_close, .on_poll = relay_on_poll};

proto_bool protocore_relay_publish(uint8_t listener_id, const char *origin_host, uint16_t origin_port)
{
    if (!origin_host)
    {
        return PROTO_FALSE;
    }
    size_t hl = str.len(origin_host, PROTOCORE_RELAY_HOST_MAX + 1);
    if (hl == 0 || hl >= PROTOCORE_RELAY_HOST_MAX)
    {
        return PROTO_FALSE;
    }
    int idx = -1;
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
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
    mem.cpy(s_ctx.binds[idx].host, origin_host, hl + 1);
    s_ctx.binds[idx].port = origin_port;
    if (!s_ctx.registered)
    {
        Session.proto->proto = PROTO_RELAY;
        Session.proto->h = &s_relay_handler;
        Session.proto->add(Session.proto->internal);
        s_ctx.registered = PROTO_TRUE;
    }
    return PROTO_TRUE;
}

void protocore_relay_listener_reset(void)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        s_ctx.binds[i].active = PROTO_FALSE;
    }
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
    {
        if (s_ctx.bridges[i].active)
        {
            s_ctx.bridges[i].active = PROTO_FALSE;
#if PROTOCORE_ENABLE_RADIO_POWER
            Radio.busy_release(Radio.internal); // balance the hold taken when the bridge was opened
#endif
        }
    }
}

#endif // PROTOCORE_ENABLE_RELAY
