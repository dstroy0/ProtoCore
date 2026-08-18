// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iface_bridge_hw.c
 * @brief ESP32 glue for the interface bridge (see iface_bridge_hw.h): the PROTO_BRIDGE connection handler
 *        and the UART / SPI / I2C transfers. The rule table and frame codec live in the pure core.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IFACE_BRIDGE

#include "mmgr/secure.h" // the persistent end this module's state is taken from
#include "server/net/iface_bridge/iface_bridge.h"
#include "server/net/iface_bridge/iface_bridge_hw.h"
#include "shared/ip/ip.h"

#include "network_drivers/session/session.h"                 // Session.proto->add: the handler registration
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the accepted slot
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h" // protocore_millis() pluggable monotonic clock
#include "server/core/proto_handler.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_HAS_BUS
#include "server/peripherals/i2c.h"  // the shared I2C bus owner
#include "server/peripherals/spi.h"  // the shared SPI bus owner, and chip select
#include "server/peripherals/uart.h" // the shared UART owner
#endif

// One published listener -> hardware rule. Dispatch is by the listener id the transport stamps on each
// accepted slot (identical to server/net/relay); the rule pointer is stable for the life of the binding
// because rules live in the pure table's static storage.
typedef struct
{
    proto_bool active;
    uint8_t listener_id;
    const BridgeRule *rule;
} BridgeBind;

// All of the glue's mutable state in one owned, feature-gated context (the owner-context guard requires
// the single file-scope mutable to be a `*Ctx` instance).
typedef struct
{
    BridgeBind binds[PROTOCORE_BRIDGE_MAX_RULES];
    proto_bool registered;                         ///< the PROTO_BRIDGE handler is installed
    proto_bool spi_begun;                          ///< the shared SPI bus has been brought up (once)
    uint8_t stream[PROTOCORE_BRIDGE_STREAM_CHUNK]; ///< the chunk a STREAM target moves per pump
} BridgeGlueCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define IFACE_BRIDGE_HW_OFF_CTX 0u
static_assert(IFACE_BRIDGE_HW_OFF_CTX + sizeof(BridgeGlueCtx) <= PROTOCORE_IFACE_BRIDGE_HW_BORROW,
              "PROTOCORE_IFACE_BRIDGE_HW_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define IFACE_BRIDGE_HW_CTX(w) ((BridgeGlueCtx *)(void *)((w) + IFACE_BRIDGE_HW_OFF_CTX))

static const BridgeRule *rule_for_slot(uint8_t *restrict work, uint8_t slot)
{
    ConnPool.slot = slot;
    ConnPool.listener_id(protocore_conn_pool_span());
    uint8_t lid = ConnPool.u8;
    for (int i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        if (IFACE_BRIDGE_HW_CTX(work)->binds[i].active && IFACE_BRIDGE_HW_CTX(work)->binds[i].listener_id == lid)
        {
            return IFACE_BRIDGE_HW_CTX(work)->binds[i].rule;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------
// Bus I/O, compiled where a bus seam exists. With none these are stubs and the codec + rule table
// are host-tested.
// ---------------------------------------------------------------------------------------------

#if PROTOCORE_HAS_BUS

// Bring the target's bus up once at publish. UART opens at its baud on the unit's default pins;
// SPI parks the CS gpio high and starts the shared bus once; I2C uses the shared bus owner.
static void bus_begin(uint8_t *restrict work, const BridgeTarget *t)
{
    switch (t->bus)
    {
    case BRIDGE_BUS_UART:
        (void)protocore_uart_begin(t->unit, t->rate ? t->rate : 115200, -1, -1);
        break;
    case BRIDGE_BUS_SPI:
        protocore_spi_cs_idle((uint8_t)(t->addr_cs));
        if (!IFACE_BRIDGE_HW_CTX(work)->spi_begun)
        {
            (void)protocore_spi_begin();
            IFACE_BRIDGE_HW_CTX(work)->spi_begun = PROTO_TRUE;
        }
        break;
    case BRIDGE_BUS_I2C:
        (void)protocore_i2c_begin();
        break;
    }
}

// One write-then-read transaction against the target's bus. Clocks @p wlen bytes out, reads @p rlen bytes
// back into @p rbuf (short reads are zero-padded). Returns false only on a bus-level failure.
static proto_bool bus_txn(const BridgeTarget *t, const uint8_t *wbuf, uint16_t wlen, uint8_t *rbuf, uint16_t rlen)
{
    switch (t->bus)
    {
    case BRIDGE_BUS_I2C:
        // A write followed by a read is one transaction joined by a repeated start, which is what
        // keeps the device's register pointer between the halves.
        if (wlen && rlen)
        {
            return protocore_i2c_write_read((uint8_t)t->addr_cs, wbuf, wlen, rbuf, rlen);
        }
        if (wlen)
        {
            return protocore_i2c_write((uint8_t)t->addr_cs, wbuf, wlen);
        }
        return rlen ? protocore_i2c_read((uint8_t)t->addr_cs, rbuf, rlen) : PROTO_TRUE;

    case BRIDGE_BUS_SPI: {
        // The target names its own clock, bit order and mode, so the transfer carries them rather
        // than taking the bus owner's configured defaults. CS is held across both halves.
        uint8_t order = t->bit_order ? PROTOCORE_SPI_LSBFIRST : PROTOCORE_SPI_MSBFIRST;
        uint32_t hz = t->rate ? t->rate : PROTOCORE_SPI_HZ;
        proto_bool ok = PROTO_TRUE;
        protocore_spi_cs_select((uint8_t)(t->addr_cs));
        if (wlen)
        {
            ok = protocore_spi_txn_at(hz, order, (uint8_t)(t->spi_mode & 0x3), wbuf, NULL, wlen);
        }
        if (ok && rlen)
        {
            ok = protocore_spi_txn_at(hz, order, (uint8_t)(t->spi_mode & 0x3), NULL, rbuf, rlen);
        }
        protocore_spi_cs_release((uint8_t)(t->addr_cs));
        return ok;
    }

    case BRIDGE_BUS_UART: {
        if (wlen && !protocore_uart_write(t->unit, wbuf, wlen))
        {
            return PROTO_FALSE;
        }
        // One bounded read for the whole reply, then zero-pad whatever did not arrive.
        size_t got = rlen ? protocore_uart_read(t->unit, rbuf, rlen, PROTOCORE_BRIDGE_UART_TXN_MS) : 0u;
        for (size_t i = got; i < rlen; i++)
        {
            rbuf[i] = 0;
        }
        return PROTO_TRUE;
    }
    }
    return PROTO_FALSE;
}

// STREAM: pipe socket RX -> UART (called from on_data).
static void stream_sock_to_uart(uint8_t *restrict work, uint8_t slot, const BridgeTarget *t)
{
    for (;;)
    {
        ConnPool.slot = slot;
        ConnPool.io.buf = IFACE_BRIDGE_HW_CTX(work)->stream;
        ConnPool.io.cap = sizeof IFACE_BRIDGE_HW_CTX(work)->stream;
        ConnPool.read(protocore_conn_pool_span());
        const size_t n = ConnPool.n;
        if (n == 0)
        {
            break;
        }
        (void)protocore_uart_write(t->unit, IFACE_BRIDGE_HW_CTX(work)->stream, n);
    }
}

// STREAM: pipe UART RX -> socket (called from on_poll).
static void stream_uart_to_sock(uint8_t *restrict work, uint8_t slot, const BridgeTarget *t)
{
    // The driver ISR refills the UART ring independently of this loop, so the chunk count is what ends
    // the poll slice: at sustained line rate the available count never falls to zero on its own.
    for (uint8_t i = 0; i < PROTOCORE_BRIDGE_MAX_DRAIN && protocore_uart_available(t->unit) > 0; i++)
    {
        size_t n = protocore_uart_read(t->unit, IFACE_BRIDGE_HW_CTX(work)->stream,
                                       sizeof IFACE_BRIDGE_HW_CTX(work)->stream, 0);
        if (n == 0)
        {
            return;
        }
        ConnPool.slot = slot;
        ConnPool.active(protocore_conn_pool_span());
        if (ConnPool.ok)
        {
            ConnPool.slot = slot;
            ConnPool.io.data = IFACE_BRIDGE_HW_CTX(work)->stream;
            ConnPool.io.len = (proto_u16)n;
            ConnPool.send(protocore_conn_pool_span());
        }
    }
}

#else // no bus seam. The codec + rule table are host-tested elsewhere.

static void bus_begin(work, const BridgeTarget *t)
{
    (void)t;
}
static proto_bool bus_txn(const BridgeTarget *t, const uint8_t *wbuf, uint16_t wlen, uint8_t *rbuf, uint16_t rlen)
{
    (void)t;
    (void)wbuf;
    (void)wlen;
    (void)rbuf;
    (void)rlen;
    return PROTO_FALSE;
}
static void stream_sock_to_uart(work, uint8_t slot, const BridgeTarget *t)
{
    (void)slot;
    (void)t;
}
static void stream_uart_to_sock(work, uint8_t slot, const BridgeTarget *t)
{
    (void)slot;
    (void)t;
}

#endif // PROTOCORE_HAS_BUS

// TRANSACTION: drain complete write-then-read frames out of the slot's RX ring, run each against the bus,
// and send the read bytes back. Peeks a whole frame into a linear scratch so the pure codec stays the one
// owner of the frame format; consumes only once a frame is fully buffered (partial frames wait for more).
static void service_txn(uint8_t slot, const BridgeTarget *t)
{
    uint8_t frame[PROTOCORE_BRIDGE_TXN_HDR + PROTOCORE_BRIDGE_TXN_MAX];
    uint8_t rbuf[PROTOCORE_BRIDGE_TXN_MAX];
    for (;;)
    {
        ConnPool.slot = slot;
        ConnPool.available(protocore_conn_pool_span());
        const size_t avail = ConnPool.n;
        if (avail < PROTOCORE_BRIDGE_TXN_HDR)
        {
            return; // header not yet complete
        }
        uint8_t hdr[PROTOCORE_BRIDGE_TXN_HDR];
        ConnPool.slot = slot;
        ConnPool.io.off = 0;
        ConnPool.io.buf = hdr;
        ConnPool.io.count = PROTOCORE_BRIDGE_TXN_HDR;
        ConnPool.peek(protocore_conn_pool_span());
        uint16_t wlen = (uint16_t)((hdr[0] << 8) | hdr[1]);
        uint16_t rlen = (uint16_t)((hdr[2] << 8) | hdr[3]);
        if (wlen > PROTOCORE_BRIDGE_TXN_MAX || rlen > PROTOCORE_BRIDGE_TXN_MAX)
        {
            ConnPool.slot = slot;
            ConnPool.close(protocore_conn_pool_span()); // frame exceeds the configured cap - protocol error
            return;
        }
        size_t need = (size_t)PROTOCORE_BRIDGE_TXN_HDR + wlen;
        if (avail < need)
        {
            return; // write payload not fully buffered yet
        }
        ConnPool.slot = slot;
        ConnPool.io.off = 0;
        ConnPool.io.buf = frame;
        ConnPool.io.count = need;
        ConnPool.peek(protocore_conn_pool_span());
        uint16_t pw = 0;
        uint16_t pr = 0;
        const uint8_t *wd = NULL;
        IfaceBridge.txn_parse_args.buf = frame;
        IfaceBridge.txn_parse_args.len = need;
        IfaceBridge.txn_parse_args.write_len = &pw;
        IfaceBridge.txn_parse_args.read_len = &pr;
        IfaceBridge.txn_parse_args.write_data = &wd;
        IfaceBridge.txn_parse(protocore_iface_bridge_span());
        if (IfaceBridge.n != need)
        {
            ConnPool.slot = slot;
            ConnPool.close(protocore_conn_pool_span()); // codec disagreed with the header - drop the connection
            return;
        }
        ConnPool.slot = slot;
        ConnPool.io.count = need;
        ConnPool.consume(protocore_conn_pool_span());
        if (!bus_txn(t, wd, pw, rbuf, pr))
        {
            ConnPool.slot = slot;
            ConnPool.close(protocore_conn_pool_span()); // bus fault
            return;
        }
        ConnPool.slot = slot;
        ConnPool.active(protocore_conn_pool_span());
        if (pr && ConnPool.ok)
        {
            ConnPool.slot = slot;
            ConnPool.io.data = rbuf;
            ConnPool.io.len = pr;
            ConnPool.send(protocore_conn_pool_span());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// PROTO_BRIDGE connection handler.
// ---------------------------------------------------------------------------------------------

static void bridge_on_accept(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_iface_bridge_hw_span();
    if (work == NULL)
    {
        return;
    }

    if (!rule_for_slot(work, slot))
    {
        ConnPool.slot = slot;
        ConnPool.close(protocore_conn_pool_span()); // no rule published for this listener
    }
}

static void bridge_on_data(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_iface_bridge_hw_span();
    if (work == NULL)
    {
        return;
    }

    const BridgeRule *r = rule_for_slot(work, slot);
    if (!r)
    {
        ConnPool.slot = slot;
        ConnPool.close(protocore_conn_pool_span());
        return;
    }
    if (r->target.mode == BRIDGE_MODE_STREAM)
    {
        stream_sock_to_uart(work, slot, &r->target);
    }
    else
    {
        service_txn(slot, &r->target);
    }
}

static void bridge_on_poll(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_iface_bridge_hw_span();
    if (work == NULL)
    {
        return;
    }

    ConnPool.slot = slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        return;
    }
    const BridgeRule *r = rule_for_slot(work, slot);
    if (!r || r->target.mode != BRIDGE_MODE_STREAM)
    {
        return; // transaction mode is request-driven; nothing to pump on poll
    }
    stream_uart_to_sock(work, slot, &r->target);
}

static void bridge_on_close(uint8_t slot)
{
    (void)slot;
    // Per-connection is stateless (the rule is re-derived from the listener id each callback), so there is
    // nothing to free; the transport owns the closing slot.
}

// Designated, so a member's position in the struct does not decide what it binds to. on_abort is
// unset: a null one falls back to on_close.
static const ProtoHandler s_bridge_handler = {
    .on_accept = bridge_on_accept, .on_data = bridge_on_data, .on_close = bridge_on_close, .on_poll = bridge_on_poll};

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_IFACE_BRIDGE_HW_BORROW persistent bytes, or null while the pool was short
} IfaceBridgeHwOwnCtx;
static IfaceBridgeHwOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_iface_bridge_hw_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_IFACE_BRIDGE_HW_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void iface_bridge_hw_publish(uint8_t *restrict work)
{
    uint8_t listener_id = IfaceBridgeHw.publish_args.listener_id;
    uint16_t port = IfaceBridgeHw.publish_args.port;
    BridgeProto proto = IfaceBridgeHw.publish_args.proto;
    const BridgeTarget *target = IfaceBridgeHw.publish_args.target;

    if (!target)
    {
        IfaceBridgeHw.ok = PROTO_FALSE;
        return;
    }
    IfaceBridge.map_args.ip = NULL;
    IfaceBridge.map_args.port = port;
    IfaceBridge.map_args.proto = proto;
    IfaceBridge.map_args.target = target;
    IfaceBridge.map(protocore_iface_bridge_span());
    if (!IfaceBridge.ok) // store + validate + dedupe in the pure table
    {
        IfaceBridgeHw.ok = PROTO_FALSE;
        return;
    }
    IfaceBridge.find_args.port = port;
    IfaceBridge.find_args.proto = proto;
    IfaceBridge.find(protocore_iface_bridge_span());
    const BridgeRule *rule = IfaceBridge.rule;
    if (!rule)
    {
        IfaceBridgeHw.ok = PROTO_FALSE;
        return;
    }
    int idx = -1;
    for (int i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        if (!IFACE_BRIDGE_HW_CTX(work)->binds[i].active)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        IfaceBridgeHw.ok = PROTO_FALSE;
        return;
    }
    IFACE_BRIDGE_HW_CTX(work)->binds[idx].active = PROTO_TRUE;
    IFACE_BRIDGE_HW_CTX(work)->binds[idx].listener_id = listener_id;
    IFACE_BRIDGE_HW_CTX(work)->binds[idx].rule = rule;
    bus_begin(work, &rule->target);
    if (!IFACE_BRIDGE_HW_CTX(work)->registered)
    {
        Session.proto->proto = PROTO_BRIDGE;
        Session.proto->h = &s_bridge_handler;
        Session.proto->add(protocore_session_span());
        IFACE_BRIDGE_HW_CTX(work)->registered = PROTO_TRUE;
    }
    IfaceBridgeHw.ok = PROTO_TRUE;
}

static void iface_bridge_hw_reset(uint8_t *restrict work)
{

    for (int i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        IFACE_BRIDGE_HW_CTX(work)->binds[i].active = PROTO_FALSE;
    }
    IfaceBridge.clear(protocore_iface_bridge_span());
}

IfaceBridgeHwNs IfaceBridgeHw = {.publish = iface_bridge_hw_publish, .reset = iface_bridge_hw_reset};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IFACE_BRIDGE
