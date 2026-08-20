// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay_listener.c
 * @brief Server-side TCP relay / DNAT listener (see relay_listener.h). Bridges a ProtoConn::PROTO_RELAY
 *        connection to an origin protocore_client connection via the pure relay engine.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_RELAY

#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/net/relay/relay_listener/relay_listener.h"

#include "mmgr/protostr/protostr.h"
#include "network_drivers/session/session.h"                 // Session.proto->add: the handler registration
#include "network_drivers/transport/tcp/client/client.h"     // TcpClient: the dialed connection
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the accepted slot
#include "network_drivers/transport/tcp/tcp.h"
#include "server/core/proto_handler.h"
#include "server/net/relay/relay/relay.h"
PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_RADIO_POWER
#include "network_drivers/physical/radio_power/radio_power.h" // keep the radio awake during a relayed transfer
#endif

static uint8_t relay_work[16]; // the borrow an entry takes; Relay never reads it

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define RELAY_LISTENER_OFF_CTX 0u
static_assert(RELAY_LISTENER_OFF_CTX + sizeof(RelayListenerCtx) <= PROTOCORE_RELAY_LISTENER_BORROW,
              "PROTOCORE_RELAY_LISTENER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(RELAY_LISTENER_OFF_CTX % _Alignof(RelayListenerCtx) == 0,
              "RELAY_LISTENER_OFF_CTX is not a multiple of alignof(RelayListenerCtx) - RELAY_LISTENER_CTX() would "
              "return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define RELAY_LISTENER_CTX(w) ((RelayListenerCtx *)(void *)((w) + RELAY_LISTENER_OFF_CTX))

static RelayBind *bind_by_listener(uint8_t *restrict work, uint8_t lid)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        if (RELAY_LISTENER_CTX(work)->binds[i].active && RELAY_LISTENER_CTX(work)->binds[i].listener_id == lid)
        {
            return &RELAY_LISTENER_CTX(work)->binds[i];
        }
    }
    return NULL;
}

static RelayBridge *bridge_by_conn(uint8_t *restrict work, uint8_t slot)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
    {
        if (RELAY_LISTENER_CTX(work)->bridges[i].active && RELAY_LISTENER_CTX(work)->bridges[i].conn_slot == slot)
        {
            return &RELAY_LISTENER_CTX(work)->bridges[i];
        }
    }
    return NULL;
}

static int bridge_find_free(uint8_t *restrict work)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
    {
        if (!RELAY_LISTENER_CTX(work)->bridges[i].active)
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
    ConnPoolV.slot = br->conn_slot;
    ConnPool.available(protocore_conn_pool_span());
    if (ConnPoolV.n)
    {
        ConnPoolV.slot = br->conn_slot;
        ConnPoolV.io.buf = buf;
        ConnPoolV.io.cap = cap;
        ConnPool.read(protocore_conn_pool_span());
        return (int)ConnPoolV.n;
    }
    return 0;
}
static int a_send(void *c, const uint8_t *buf, size_t len)
{
    RelayBridge *br = (RelayBridge *)c;
    // Send as much as the inbound TCP send window currently allows (partial), not all-or-nothing: a
    // whole PROTOCORE_RELAY_BUF chunk rarely fits tcp_sndbuf in one shot, and a failed all-or-nothing send
    // forwards zero bytes and stalls the transfer. room==0 is real backpressure - the pump retries.
    ConnPoolV.slot = br->conn_slot;
    ConnPool.sndbuf(protocore_conn_pool_span());
    proto_u16 room = ConnPoolV.u16;
    if (room == 0)
    {
        return 0;
    }
    proto_u16 n = (len < (size_t)room) ? (proto_u16)len : room;
    ConnPoolV.slot = br->conn_slot;
    ConnPoolV.io.data = buf;
    ConnPoolV.io.len = n;
    ConnPool.send(protocore_conn_pool_span());
    return ConnPoolV.ok ? (int)n : 0;
}
// Origin (b) = the outbound protocore_client; it reports EOF through the recv seam.
static int b_recv(void *c, uint8_t *buf, size_t cap)
{
    RelayBridge *br = (RelayBridge *)c;
    TcpClientV.cid = br->origin_cid;
    TcpClientV.io.buf = buf;
    TcpClientV.io.cap = cap;
    TcpClient.read(protocore_tcp_client_span());
    size_t n = TcpClientV.n;
    if (n)
    {
        return (int)n;
    }
    TcpClientV.cid = br->origin_cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    return TcpClientV.ok ? -1 : 0;
}
static int b_send(void *c, const uint8_t *buf, size_t len)
{
    RelayBridge *br = (RelayBridge *)c;
    TcpClientV.cid = br->origin_cid;
    TcpClientV.io.data = buf;
    TcpClientV.io.len = len;
    TcpClient.send(protocore_tcp_client_span());
    return TcpClientV.ok ? (int)len : 0;
}

// Close the origin (and optionally the inbound) and free the bridge. active=false first so a
// re-entrant close callback is a no-op.
static void teardown(RelayBridge *br, proto_bool close_inbound)
{
    br->active = PROTO_FALSE;
#if PROTOCORE_ENABLE_RADIO_POWER
    Radio.busy_release(protocore_radio_power_span()); // this bridge is done relaying
#endif
    TcpClientV.cid = br->origin_cid;
    TcpClient.close(protocore_tcp_client_span());
    if (close_inbound)
    {
        ConnPoolV.slot = br->conn_slot;
        ConnPool.close(protocore_conn_pool_span());
    }
}

// Pump the bridge one pass and tear it down if the origin ended or the pump errored.
static void service(uint8_t *restrict work, uint8_t slot)
{
    RelayBridge *br = bridge_by_conn(work, slot);
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
        RelayV.step_args.r = &br->relay;
        Relay.step(relay_work);
        protocore_relay_status st = RelayV.status;
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
    TcpClientV.cid = br->origin_cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    const proto_bool origin_closed = TcpClientV.ok;
    TcpClientV.cid = br->origin_cid;
    TcpClient.available(protocore_tcp_client_span());
    if (origin_closed && TcpClientV.n == 0 && br->relay.b2a_off >= br->relay.b2a_len)
    {
        teardown(br, PROTO_TRUE);
    }
}

static void relay_on_accept(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_relay_listener_span();

    ConnPoolV.slot = slot;
    ConnPool.listener_id(protocore_conn_pool_span());
    RelayBind *bd = bind_by_listener(work, ConnPoolV.u8);
    if (!bd)
    {
        ConnPoolV.slot = slot;
        ConnPool.close(protocore_conn_pool_span()); // no origin published for this listener
        return;
    }
    int idx = bridge_find_free(work);
    if (idx < 0)
    {
        ConnPoolV.slot = slot;
        ConnPool.close(protocore_conn_pool_span()); // bridge table full
        return;
    }
    // open() takes a slot and returns; the origin is not up yet. The bridge arms anyway: the pump
    // steps the connect through b_recv's is_closed, a send before it is up reads as backpressure and
    // retries, and the slot's own PROTOCORE_RELAY_CONNECT_MS is what ends an origin that never answers.
    TcpClientV.dial.host = bd->host;
    TcpClientV.dial.port = bd->port;
    TcpClientV.dial.timeout_ms = PROTOCORE_RELAY_CONNECT_MS;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    if (cid < 0)
    {
        ConnPoolV.slot = slot;
        ConnPool.close(protocore_conn_pool_span()); // no free client slot
        return;
    }
    RelayBridge *br = &RELAY_LISTENER_CTX(work)->bridges[idx];
    br->active = PROTO_TRUE;
    br->conn_slot = slot;
    br->origin_cid = cid;
    protocore_relay_end a = {a_recv, a_send, NULL, br};
    protocore_relay_end b = {b_recv, b_send, NULL, br};
    RelayV.init_args.r = &br->relay;
    RelayV.init_args.client = &a;
    RelayV.init_args.origin = &b;
    Relay.init(relay_work);
#if PROTOCORE_ENABLE_RADIO_POWER
    Radio.busy_hold(protocore_radio_power_span()); // hold the radio awake for the life of this bridge
#endif
}

static void relay_on_data(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_relay_listener_span();

    service(work, slot);
}

static void relay_on_poll(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_relay_listener_span();

    ConnPoolV.slot = slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPoolV.ok)
    {
        return;
    }
    service(work, slot);
}

static void relay_on_close(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_relay_listener_span();

    RelayBridge *br = bridge_by_conn(work, slot);
    if (br)
    {
        teardown(br, PROTO_FALSE); // the transport already owns the closing inbound slot
    }
}

// Designated, so a member's position in the struct does not decide what it binds to. on_abort is
// unset: a null one falls back to on_close.
static const ProtoHandler s_relay_handler = {
    .on_accept = relay_on_accept, .on_data = relay_on_data, .on_close = relay_on_close, .on_poll = relay_on_poll};

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_RELAY_LISTENER_BORROW persistent bytes
} RelayListenerOwnCtx;
static RelayListenerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_relay_listener_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_RELAY_LISTENER_BORROW).buf;
    }
    return s_own.span;
}

void protocore_relay_listener_publish(uint8_t *restrict work)
{
    uint8_t listener_id = RelayListenerV.publish_args.listener_id;
    const char *origin_host = RelayListenerV.publish_args.origin_host;
    uint16_t origin_port = RelayListenerV.publish_args.origin_port;

    if (!origin_host)
    {
        RelayListenerV.ok = PROTO_FALSE;
        return;
    }
    size_t hl = str.len(origin_host, PROTOCORE_RELAY_HOST_MAX + 1);
    if (hl == 0 || hl >= PROTOCORE_RELAY_HOST_MAX)
    {
        RelayListenerV.ok = PROTO_FALSE;
        return;
    }
    int idx = -1;
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        if (!RELAY_LISTENER_CTX(work)->binds[i].active)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        RelayListenerV.ok = PROTO_FALSE;
        return;
    }
    RELAY_LISTENER_CTX(work)->binds[idx].active = PROTO_TRUE;
    RELAY_LISTENER_CTX(work)->binds[idx].listener_id = listener_id;
    mem.cpy(RELAY_LISTENER_CTX(work)->binds[idx].host, origin_host, hl + 1);
    RELAY_LISTENER_CTX(work)->binds[idx].port = origin_port;
    if (!RELAY_LISTENER_CTX(work)->registered)
    {
        ProtocolsV.proto = PROTO_RELAY;
        ProtocolsV.h = &s_relay_handler;
        Protocols.add(protocore_session_span());
        RELAY_LISTENER_CTX(work)->registered = PROTO_TRUE;
    }
    RelayListenerV.ok = PROTO_TRUE;
}

void protocore_relay_listener_reset(uint8_t *restrict work)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        RELAY_LISTENER_CTX(work)->binds[i].active = PROTO_FALSE;
    }
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
    {
        if (RELAY_LISTENER_CTX(work)->bridges[i].active)
        {
            RELAY_LISTENER_CTX(work)->bridges[i].active = PROTO_FALSE;
#if PROTOCORE_ENABLE_RADIO_POWER
            Radio.busy_release(protocore_radio_power_span()); // balance the hold taken when the bridge was opened
#endif
        }
    }
}

/** @brief The operands and the outcome. */
RelayListenerVars RelayListenerV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RELAY
