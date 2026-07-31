// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iface_bridge_hw.cpp
 * @brief ESP32 glue for the interface bridge (see iface_bridge_hw.h): the PROTO_BRIDGE connection handler
 *        and the UART / SPI / I2C transfers. The rule table and frame codec live in the pure core.
 */

#include "services/net/iface_bridge/iface_bridge_hw.h"

#if PC_ENABLE_IFACE_BRIDGE

#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
#include "services/system/clock.h" // pc_millis() pluggable monotonic clock

// The Arduino bus headers MUST be included at global scope: pulling them into the anonymous namespace
// below would make `SPI` / `Wire` anonymous-namespace symbols with no definition (link failure).
#if defined(ARDUINO)
#include "services/peripherals/i2c.h" // pc_i2c_begin (the shared I2C bus owner)
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#endif

namespace
{

// One published listener -> hardware rule. Dispatch is by the listener id the transport stamps on each
// accepted slot (identical to services/net/relay); the rule pointer is stable for the life of the binding
// because rules live in the pure table's static storage.
struct BridgeBind
{
    bool active;
    uint8_t listener_id;
    const BridgeRule *rule;
};

// All of the glue's mutable state in one owned, feature-gated context (the owner-context guard requires
// the single file-scope mutable to be a `*Ctx` instance).
struct BridgeGlueCtx
{
    BridgeBind binds[PC_BRIDGE_MAX_RULES];
    bool registered; ///< the PROTO_BRIDGE handler is installed
    bool spi_begun;  ///< SPI.begin() has run (once, shared bus)
};
BridgeGlueCtx s_ctx;

const BridgeRule *rule_for_slot(uint8_t slot)
{
    uint8_t lid = pc_conn_listener_id(slot);
    for (int i = 0; i < PC_BRIDGE_MAX_RULES; i++)
    {
        if (s_ctx.binds[i].active && s_ctx.binds[i].listener_id == lid)
        {
            return s_ctx.binds[i].rule;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Bus I/O (ESP32 only). Host builds stub these out - the codec + rule table are host-tested.
// ---------------------------------------------------------------------------------------------

#if defined(ARDUINO)

// unit -> the matching HardwareSerial, or nullptr if this SoC has no such UART. SOC_UART_NUM mirrors the
// core's own guards on Serial1 / Serial2 (an S3 has fewer UARTs than a classic ESP32).
HardwareSerial *uart_for(uint8_t unit)
{
    switch (unit)
    {
    case 0:
        // With USB-CDC-on-boot, `Serial` is the USB CDC (HWCDC) and UART0 is exposed as `Serial0`
        // (a HardwareSerial); otherwise `Serial` itself is the UART0 HardwareSerial. Mirror the core's
        // own guard (cores/esp32/HardwareSerial.h) so unit 0 always resolves to a real HardwareSerial.
#if ARDUINO_USB_CDC_ON_BOOT
        return &Serial0;
#else
        return &Serial;
#endif
#if SOC_UART_NUM > 1
    case 1:
        return &Serial1;
#endif
#if SOC_UART_NUM > 2
    case 2:
        return &Serial2;
#endif
    default:
        return nullptr;
    }
}

// Bring the target's bus up once at publish. UART begins at its baud; SPI drives the CS gpio high (idle)
// and starts the shared SPI bus once; I2C uses the shared bus owner.
void bus_begin(const BridgeTarget *t)
{
    switch (t->bus)
    {
    case BridgeBus::uart: {
        HardwareSerial *s = uart_for(t->unit);
        if (s)
        {
            s->begin(t->rate ? t->rate : 115200);
        }
        break;
    }
    case BridgeBus::spi:
        pinMode(t->addr_cs, OUTPUT);
        digitalWrite(t->addr_cs, HIGH); // CS idle-high
        if (!s_ctx.spi_begun)
        {
            SPI.begin();
            s_ctx.spi_begun = true;
        }
        break;
    case BridgeBus::i2c:
        pc_i2c_begin();
        break;
    }
}

// One write-then-read transaction against the target's bus. Clocks @p wlen bytes out, reads @p rlen bytes
// back into @p rbuf (short reads are zero-padded). Returns false only on a bus-level failure.
bool bus_txn(const BridgeTarget *t, const uint8_t *wbuf, uint16_t wlen, uint8_t *rbuf, uint16_t rlen)
{
    switch (t->bus)
    {
    case BridgeBus::i2c:
        if (wlen)
        {
            Wire.beginTransmission((uint8_t)t->addr_cs);
            Wire.write(wbuf, wlen);
            // repeated-start (no stop) when a read follows, so the device holds the register pointer
            if (Wire.endTransmission(rlen == 0) != 0)
            {
                return false;
            }
        }
        if (rlen)
        {
            uint16_t got = (uint16_t)Wire.requestFrom((int)(uint8_t)t->addr_cs, (int)rlen);
            for (uint16_t i = 0; i < rlen; i++)
            {
                rbuf[i] = (i < got && Wire.available()) ? (uint8_t)Wire.read() : 0;
            }
        }
        return true;

    case BridgeBus::spi: {
        uint8_t order = t->bit_order ? LSBFIRST : MSBFIRST;
        SPI.beginTransaction(SPISettings(t->rate ? t->rate : 1000000, order, t->spi_mode & 0x3));
        digitalWrite(t->addr_cs, LOW);
        for (uint16_t i = 0; i < wlen; i++)
        {
            SPI.transfer(wbuf[i]);
        }
        for (uint16_t i = 0; i < rlen; i++)
        {
            rbuf[i] = SPI.transfer(0x00); // clock dummies to read
        }
        digitalWrite(t->addr_cs, HIGH);
        SPI.endTransaction();
        return true;
    }

    case BridgeBus::uart: {
        HardwareSerial *s = uart_for(t->unit);
        if (!s)
        {
            return false;
        }
        if (wlen)
        {
            s->write(wbuf, wlen);
        }
        uint16_t got = 0;
        uint32_t deadline = pc_millis() + PC_BRIDGE_UART_TXN_MS;
        while (got < rlen && (int32_t)(pc_millis() - deadline) < 0)
        {
            while (got < rlen && s->available())
            {
                rbuf[got++] = (uint8_t)s->read();
            }
        }
        for (; got < rlen; got++)
        {
            rbuf[got] = 0; // zero-pad a short read
        }
        return true;
    }
    }
    return false;
}

// STREAM: pipe socket RX -> UART (called from on_data).
void stream_sock_to_uart(uint8_t slot, const BridgeTarget *t)
{
    HardwareSerial *s = uart_for(t->unit);
    if (!s)
    {
        return;
    }
    uint8_t buf[PC_BRIDGE_STREAM_CHUNK];
    size_t n = 0;
    while ((n = pc_conn_read(slot, buf, sizeof buf)) > 0)
    {
        s->write(buf, n);
    }
}

// STREAM: pipe UART RX -> socket (called from on_poll).
void stream_uart_to_sock(uint8_t slot, const BridgeTarget *t)
{
    HardwareSerial *s = uart_for(t->unit);
    if (!s)
    {
        return;
    }
    uint8_t buf[PC_BRIDGE_STREAM_CHUNK];
    while (s->available() > 0)
    {
        size_t n = 0;
        while (n < sizeof buf && s->available())
        {
            buf[n++] = (uint8_t)s->read();
        }
        if (n && pc_conn_active(slot))
        {
            pc_conn_send(slot, buf, (u16_t)n);
        }
    }
}

#else // host build: no Serial / SPI / Wire. The codec + rule table are host-tested elsewhere.

void bus_begin(const BridgeTarget *)
{
}
bool bus_txn(const BridgeTarget *, const uint8_t *, uint16_t, uint8_t *, uint16_t)
{
    return false;
}
void stream_sock_to_uart(uint8_t, const BridgeTarget *)
{
}
void stream_uart_to_sock(uint8_t, const BridgeTarget *)
{
}

#endif // ARDUINO

// TRANSACTION: drain complete write-then-read frames out of the slot's RX ring, run each against the bus,
// and send the read bytes back. Peeks a whole frame into a linear scratch so the pure codec stays the one
// owner of the frame format; consumes only once a frame is fully buffered (partial frames wait for more).
void service_txn(uint8_t slot, const BridgeTarget *t)
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
            pc_conn_close(slot); // frame exceeds the configured cap - protocol error
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
        const uint8_t *wd = nullptr;
        if (pc_iface_bridge_txn_parse(frame, need, &pw, &pr, &wd) != need)
        {
            pc_conn_close(slot); // codec disagreed with the header - drop the connection
            return;
        }
        pc_conn_consume(slot, need);
        if (!bus_txn(t, wd, pw, rbuf, pr))
        {
            pc_conn_close(slot); // bus fault
            return;
        }
        if (pr && pc_conn_active(slot))
        {
            pc_conn_send(slot, rbuf, pr);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// PROTO_BRIDGE connection handler.
// ---------------------------------------------------------------------------------------------

void bridge_on_accept(uint8_t slot)
{
    if (!rule_for_slot(slot))
    {
        pc_conn_close(slot); // no rule published for this listener
    }
}

void bridge_on_data(uint8_t slot)
{
    const BridgeRule *r = rule_for_slot(slot);
    if (!r)
    {
        pc_conn_close(slot);
        return;
    }
    if (r->target.mode == BridgeMode::stream)
    {
        stream_sock_to_uart(slot, &r->target);
    }
    else
    {
        service_txn(slot, &r->target);
    }
}

void bridge_on_poll(uint8_t slot)
{
    if (!pc_conn_active(slot))
    {
        return;
    }
    const BridgeRule *r = rule_for_slot(slot);
    if (!r || r->target.mode != BridgeMode::stream)
    {
        return; // transaction mode is request-driven; nothing to pump on poll
    }
    stream_uart_to_sock(slot, &r->target);
}

void bridge_on_close(uint8_t)
{
    // Per-connection is stateless (the rule is re-derived from the listener id each callback), so there is
    // nothing to free; the transport owns the closing slot.
}

const ProtoHandler s_bridge_handler = {bridge_on_accept, bridge_on_data, bridge_on_close, bridge_on_poll};

} // namespace

bool pc_iface_bridge_publish(uint8_t listener_id, uint16_t port, BridgeProto proto, const BridgeTarget *target)
{
    if (!target)
    {
        return false;
    }
    if (!pc_iface_bridge_map(nullptr, port, proto, target)) // store + validate + dedupe in the pure table
    {
        return false;
    }
    const BridgeRule *rule = pc_iface_bridge_find(port, proto);
    if (!rule)
    {
        return false;
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
        return false;
    }
    s_ctx.binds[idx].active = true;
    s_ctx.binds[idx].listener_id = listener_id;
    s_ctx.binds[idx].rule = rule;
    bus_begin(&rule->target);
    if (!s_ctx.registered)
    {
        proto_register(ConnProto::PROTO_BRIDGE, &s_bridge_handler);
        s_ctx.registered = true;
    }
    return true;
}

void pc_iface_bridge_listener_reset(void)
{
    for (int i = 0; i < PC_BRIDGE_MAX_RULES; i++)
    {
        s_ctx.binds[i].active = false;
    }
    pc_iface_bridge_clear();
}

#endif // PC_ENABLE_IFACE_BRIDGE
