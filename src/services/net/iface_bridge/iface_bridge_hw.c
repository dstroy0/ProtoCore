// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iface_bridge_hw.c
 * @brief ESP32 glue for the interface bridge (see iface_bridge_hw.h): the PROTO_BRIDGE connection handler
 *        and the UART / SPI / I2C transfers. The rule table and frame codec live in the pure core.
 */

#include "services/net/iface_bridge/iface_bridge_hw.h"

#if PC_ENABLE_IFACE_BRIDGE

#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
#include "server/clock/clock.h" // pc_millis() pluggable monotonic clock

#if PROTOCORE_HOT
#include "services/peripherals/i2c.h"  // the shared I2C bus owner
#include "services/peripherals/spi.h"  // the shared SPI bus owner, and chip select
#include "services/peripherals/uart.h" // the shared UART owner
#endif

// One published listener -> hardware rule. Dispatch is by the listener id the transport stamps on each
// accepted slot (identical to services/net/relay); the rule pointer is stable for the life of the binding
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
    BridgeBind binds[PC_BRIDGE_MAX_RULES];
    proto_bool registered;                  ///< the PROTO_BRIDGE handler is installed
    proto_bool spi_begun;                   ///< the shared SPI bus has been brought up (once)
    uint8_t stream[PC_BRIDGE_STREAM_CHUNK]; ///< the chunk a STREAM target moves per pump
} BridgeGlueCtx;
static BridgeGlueCtx s_ctx;

static const BridgeRule *rule_for_slot(uint8_t slot)
{
    uint8_t lid = pc_conn_listener_id(slot);
    for (int i = 0; i < PC_BRIDGE_MAX_RULES; i++)
    {
        if (s_ctx.binds[i].active && s_ctx.binds[i].listener_id == lid)
        {
            return s_ctx.binds[i].rule;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------
// Bus I/O (ESP32 only). Host builds stub these out - the codec + rule table are host-tested.
// ---------------------------------------------------------------------------------------------

#if PROTOCORE_HOT

// Bring the target's bus up once at publish. UART opens at its baud on the unit's default pins;
// SPI parks the CS gpio high and starts the shared bus once; I2C uses the shared bus owner.
static void bus_begin(const BridgeTarget *t)
{
    switch (t->bus)
    {
    case BRIDGE_BUS_UART:
        (void)pc_uart_begin(t->unit, t->rate ? t->rate : 115200, -1, -1);
        break;
    case BRIDGE_BUS_SPI:
        pc_spi_cs_idle((uint8_t)(t->addr_cs));
        if (!s_ctx.spi_begun)
        {
            (void)pc_spi_begin();
            s_ctx.spi_begun = PROTO_TRUE;
        }
        break;
    case BRIDGE_BUS_I2C:
        (void)pc_i2c_begin();
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
            return pc_i2c_write_read((uint8_t)t->addr_cs, wbuf, wlen, rbuf, rlen);
        }
        if (wlen)
        {
            return pc_i2c_write((uint8_t)t->addr_cs, wbuf, wlen);
        }
        return rlen ? pc_i2c_read((uint8_t)t->addr_cs, rbuf, rlen) : PROTO_TRUE;

    case BRIDGE_BUS_SPI: {
        // The target names its own clock, bit order and mode, so the transfer carries them rather
        // than taking the bus owner's configured defaults. CS is held across both halves.
        uint8_t order = t->bit_order ? PC_SPI_LSBFIRST : PC_SPI_MSBFIRST;
        uint32_t hz = t->rate ? t->rate : PC_SPI_HZ;
        proto_bool ok = PROTO_TRUE;
        pc_spi_cs_select((uint8_t)(t->addr_cs));
        if (wlen)
        {
            ok = pc_spi_txn_at(hz, order, (uint8_t)(t->spi_mode & 0x3), wbuf, NULL, wlen);
        }
        if (ok && rlen)
        {
            ok = pc_spi_txn_at(hz, order, (uint8_t)(t->spi_mode & 0x3), NULL, rbuf, rlen);
        }
        pc_spi_cs_release((uint8_t)(t->addr_cs));
        return ok;
    }

    case BRIDGE_BUS_UART: {
        if (wlen && !pc_uart_write(t->unit, wbuf, wlen))
        {
            return PROTO_FALSE;
        }
        // One bounded read for the whole reply, then zero-pad whatever did not arrive.
        size_t got = rlen ? pc_uart_read(t->unit, rbuf, rlen, PC_BRIDGE_UART_TXN_MS) : 0u;
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
static void stream_sock_to_uart(uint8_t slot, const BridgeTarget *t)
{
    size_t n = 0;
    while ((n = pc_conn_read(slot, s_ctx.stream, sizeof s_ctx.stream)) > 0)
    {
        (void)pc_uart_write(t->unit, s_ctx.stream, n);
    }
}

// STREAM: pipe UART RX -> socket (called from on_poll).
static void stream_uart_to_sock(uint8_t slot, const BridgeTarget *t)
{
    // The driver ISR refills the UART ring independently of this loop, so the chunk count is what ends
    // the poll slice: at sustained line rate the available count never falls to zero on its own.
    for (uint8_t i = 0; i < PC_BRIDGE_MAX_DRAIN && pc_uart_available(t->unit) > 0; i++)
    {
        size_t n = pc_uart_read(t->unit, s_ctx.stream, sizeof s_ctx.stream, 0);
        if (n == 0)
        {
            return;
        }
        if (pc_conn_active(slot))
        {
            (void)Tcp.conn->send(slot, s_ctx.stream, (proto_u16)n);
        }
    }
}

#else // host build: no bus. The codec + rule table are host-tested elsewhere.

void bus_begin(const BridgeTarget *t)
{
    (void)t;
}
proto_bool bus_txn(const BridgeTarget *t, const uint8_t *wbuf, uint16_t wlen, uint8_t *rbuf, uint16_t rlen)
{
    (void)t;
    (void)wbuf;
    (void)wlen;
    (void)rbuf;
    (void)rlen;
    return PROTO_FALSE;
}
void stream_sock_to_uart(uint8_t slot, const BridgeTarget *t)
{
    (void)slot;
    (void)t;
}
void stream_uart_to_sock(uint8_t slot, const BridgeTarget *t)
{
    (void)slot;
    (void)t;
}

#endif // PROTOCORE_HOT

// TRANSACTION: drain complete write-then-read frames out of the slot's RX ring, run each against the bus,
// and send the read bytes back. Peeks a whole frame into a linear scratch so the pure codec stays the one
// owner of the frame format; consumes only once a frame is fully buffered (partial frames wait for more).
static void service_txn(uint8_t slot, const BridgeTarget *t)
{
    uint8_t frame[PC_BRIDGE_TXN_HDR + PC_BRIDGE_TXN_MAX];
    uint8_t rbuf[PC_BRIDGE_TXN_MAX];
    for (;;)
    {
        size_t avail = pc_conn_available(slot);
        if (avail < PC_BRIDGE_TXN_HDR)
        {
            return; // header not yet complete
        }
        uint8_t hdr[PC_BRIDGE_TXN_HDR];
        pc_conn_peek(slot, 0, hdr, PC_BRIDGE_TXN_HDR);
        uint16_t wlen = (uint16_t)((hdr[0] << 8) | hdr[1]);
        uint16_t rlen = (uint16_t)((hdr[2] << 8) | hdr[3]);
        if (wlen > PC_BRIDGE_TXN_MAX || rlen > PC_BRIDGE_TXN_MAX)
        {
            Tcp.conn->close(slot); // frame exceeds the configured cap - protocol error
            return;
        }
        size_t need = (size_t)PC_BRIDGE_TXN_HDR + wlen;
        if (avail < need)
        {
            return; // write payload not fully buffered yet
        }
        pc_conn_peek(slot, 0, frame, need);
        uint16_t pw = 0;
        uint16_t pr = 0;
        const uint8_t *wd = NULL;
        if (pc_iface_bridge_txn_parse(frame, need, &pw, &pr, &wd) != need)
        {
            Tcp.conn->close(slot); // codec disagreed with the header - drop the connection
            return;
        }
        pc_conn_consume(slot, need);
        if (!bus_txn(t, wd, pw, rbuf, pr))
        {
            Tcp.conn->close(slot); // bus fault
            return;
        }
        if (pr && pc_conn_active(slot))
        {
            Tcp.conn->send(slot, rbuf, pr);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// PROTO_BRIDGE connection handler.
// ---------------------------------------------------------------------------------------------

static void bridge_on_accept(uint8_t slot)
{
    if (!rule_for_slot(slot))
    {
        Tcp.conn->close(slot); // no rule published for this listener
    }
}

static void bridge_on_data(uint8_t slot)
{
    const BridgeRule *r = rule_for_slot(slot);
    if (!r)
    {
        Tcp.conn->close(slot);
        return;
    }
    if (r->target.mode == BRIDGE_MODE_STREAM)
    {
        stream_sock_to_uart(slot, &r->target);
    }
    else
    {
        service_txn(slot, &r->target);
    }
}

static void bridge_on_poll(uint8_t slot)
{
    if (!pc_conn_active(slot))
    {
        return;
    }
    const BridgeRule *r = rule_for_slot(slot);
    if (!r || r->target.mode != BRIDGE_MODE_STREAM)
    {
        return; // transaction mode is request-driven; nothing to pump on poll
    }
    stream_uart_to_sock(slot, &r->target);
}

static void bridge_on_close(uint8_t slot)
{
    (void)slot;
    // Per-connection is stateless (the rule is re-derived from the listener id each callback), so there is
    // nothing to free; the transport owns the closing slot.
}

static const ProtoHandler s_bridge_handler = {bridge_on_accept, bridge_on_data, bridge_on_close, bridge_on_poll};

proto_bool pc_iface_bridge_publish(uint8_t listener_id, uint16_t port, BridgeProto proto, const BridgeTarget *target)
{
    if (!target)
    {
        return PROTO_FALSE;
    }
    if (!pc_iface_bridge_map(NULL, port, proto, target)) // store + validate + dedupe in the pure table
    {
        return PROTO_FALSE;
    }
    const BridgeRule *rule = pc_iface_bridge_find(port, proto);
    if (!rule)
    {
        return PROTO_FALSE;
    }
    int idx = -1;
    for (int i = 0; i < PC_BRIDGE_MAX_RULES; i++)
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
    s_ctx.binds[idx].rule = rule;
    bus_begin(&rule->target);
    if (!s_ctx.registered)
    {
        Session.proto->add(PROTO_BRIDGE, &s_bridge_handler);
        s_ctx.registered = PROTO_TRUE;
    }
    return PROTO_TRUE;
}

void pc_iface_bridge_listener_reset(void)
{
    for (int i = 0; i < PC_BRIDGE_MAX_RULES; i++)
    {
        s_ctx.binds[i].active = PROTO_FALSE;
    }
    pc_iface_bridge_clear();
}

#endif // PC_ENABLE_IFACE_BRIDGE
