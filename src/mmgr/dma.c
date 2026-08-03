// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dma.c
 * @brief DMA peripheral ingest / egress - implementation.
 *
 * PC_DMA_SIMULATE (default) runs an in-memory model of the peripheral: an ingress
 * staging ring feeds the ping-pong RX buffers, egress DMA drains the TX buffer into a
 * capture ring, and a loopback channel routes its own TX back into its RX. pc_dma_poll()
 * advances that engine and fires the completion callbacks - so the whole pipeline is
 * host- and device-testable with no physical loopback. When the flag is 0, the front end
 * dispatches to the weak pc_dma_hw_* hooks a real silicon driver overrides.
 */

#include "mmgr/dma.h"

#if PC_ENABLE_DMA

#include <string.h> // memcpy

#if PROTOCORE_HOT
#include "server/clock/clock.h" // pc_millis(), pc_micros()
#endif

static uint32_t dma_now()
{
#if PROTOCORE_HOT
    return pc_millis();
#else
    return 0; // host builds have no clock dependency; t_ms is informational
#endif
}
static uint32_t dma_now_us()
{
#if PROTOCORE_HOT
    return pc_micros();
#else
    return 0; // host builds have no clock dependency; t_us is informational
#endif
}

#if PC_DMA_SIMULATE

// Ingress/egress staging holds a few buffers' worth so a single feed can span more than
// one RX transfer (exercising the ping-pong flip) and several TX submits can accumulate
// before capture.
#define DMA_STAGE_CAP (PC_DMA_BUF_SIZE * 3)

// Fixed-capacity byte FIFO (no heap): the simulator's ingress and egress staging.
typedef struct
{
    uint8_t buf[DMA_STAGE_CAP];
    uint16_t head; // read cursor
    uint16_t len;  // bytes queued

    void reset()
    {
        head;
        len;
    }
    uint16_t space() const
    {
        return (uint16_t)(DMA_STAGE_CAP - len);
    }
    // Append n bytes; fail-closed (append nothing) if they would not all fit.
    proto_bool push(const uint8_t *p, uint16_t n)
    {
        if (n > space())
        {
            return PROTO_FALSE;
        }
        for (uint16_t i = 0; i < n; i++)
        {
            buf[(head + len) % DMA_STAGE_CAP] = p[i];
            len++;
        }
        return PROTO_TRUE;
    }
    // Pop up to max bytes into out; returns how many.
    uint16_t pop(uint8_t *out, uint16_t max)
    {
        uint16_t n = (len < max) ? len : max;
        for (uint16_t i = 0; i < n; i++)
        {
            out[i] = buf[head];
            head = (head + 1) % DMA_STAGE_CAP;
            len--;
        }
        return n;
    }
} byte_ring;

typedef struct
{
    uint8_t rx_buf[2][PC_DMA_BUF_SIZE]; // ping-pong RX
    uint8_t tx_buf[PC_DMA_BUF_SIZE];    // egress staging
    byte_ring ingress;                  // sim: bytes arriving on the RX line
    byte_ring egress;                   // sim: bytes transmitted via egress DMA
    pc_dma_cb cb;
    void *ctx;
    uint16_t rx_fill;  // bytes in the active RX buffer since the last completion
    uint16_t tx_len;   // bytes pending egress (0 = idle)
    uint16_t seq;      // completion sequence
    uint8_t rx_active; // which ping-pong buffer the engine is filling
    pc_dma_periph periph;
    proto_bool loopback;
    proto_bool tx_busy;
    proto_bool open;
} dma_channel;

// All DMA simulator state, owned by one instance (internal linkage): the channel table,
// so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    dma_channel ch[PC_DMA_CHANNELS];
} DmaCtx;
static DmaCtx s_dma;

static void emit(dma_channel &c, uint8_t id, pc_dma_dir dir, const uint8_t *data, uint16_t len)
{
    pc_dma_event ev;
    ev.data = data;
    ev.t_ms = dma_now();
    ev.t_us = dma_now_us();
    ev.len = len;
    ev.seq = c.seq++;
    ev.channel = id;
    ev.periph = c.periph;
    ev.dir = dir;
    ev._pad = 0;
    if (c.cb) // GCOVR_EXCL_BR_LINE  cb is guaranteed non-null while a channel is open:
              // pc_dma_open rejects a null on_complete, and emit() only runs via pump(),
              // which pc_dma_poll() only calls for channels with open == true.
    {
        c.cb(&ev, c.ctx);
    }
}

// Complete whatever is queued for the channel: drain egress (TX), route loopback back
// into ingress, then feed ingress into the ping-pong RX buffers, emitting one event per
// full buffer and a final partial event (models the UART idle-line flush) so every poll
// delivers all fed bytes.
static void pump(dma_channel &c, uint8_t id)
{
    if (c.tx_busy)
    {
        if (c.loopback)
        {
            c.ingress.push(c.tx_buf, c.tx_len); // internal TX->RX jumper
        }
        c.egress.push(c.tx_buf, c.tx_len); // capture (best-effort)
        uint16_t sent = c.tx_len;
        c.tx_busy = PROTO_FALSE;
        c.tx_len = 0;
        emit(c, id, PC_DMA_TX, NULL, sent);
    }

    while (c.ingress.len > 0)
    {
        uint16_t room = (uint16_t)(PC_DMA_BUF_SIZE - c.rx_fill);
        uint16_t got = c.ingress.pop(c.rx_buf[c.rx_active] + c.rx_fill, room);
        c.rx_fill += got;
        if (c.rx_fill == PC_DMA_BUF_SIZE) // buffer full -> complete + ping-pong flip
        {
            emit(c, id, PC_DMA_RX, c.rx_buf[c.rx_active], PC_DMA_BUF_SIZE);
            c.rx_active ^= 1;
            c.rx_fill = 0;
        }
    }
    if (c.rx_fill > 0) // idle-line flush of the trailing partial buffer
    {
        emit(c, id, PC_DMA_RX, c.rx_buf[c.rx_active], c.rx_fill);
        c.rx_active ^= 1;
        c.rx_fill = 0;
    }
}

proto_bool pc_dma_open(const pc_dma_config *cfg)
{
    if (!cfg || !cfg->on_complete || cfg->channel >= PC_DMA_CHANNELS)
    {
        return PROTO_FALSE;
    }
    dma_channel &c = s_dma.ch[cfg->channel];
    if (c.open)
    {
        return PROTO_FALSE;
    }
    c.ingress.reset();
    c.egress.reset();
    c.cb = cfg->on_complete;
    c.ctx = cfg->ctx;
    c.rx_fill = 0;
    c.tx_len = 0;
    c.seq = 0;
    c.rx_active = 0;
    c.periph = cfg->periph;
    c.loopback = cfg->loopback;
    c.tx_busy = PROTO_FALSE;
    c.open = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool pc_dma_tx_submit(uint8_t ch, const uint8_t *buf, uint16_t len)
{
    if (ch >= PC_DMA_CHANNELS || !buf || len == 0 || len > PC_DMA_BUF_SIZE)
    {
        return PROTO_FALSE;
    }
    dma_channel &c = s_dma.ch[ch];
    if (!c.open || c.tx_busy) // one transfer in flight at a time (fail-closed)
    {
        return PROTO_FALSE;
    }
    memcpy(c.tx_buf, buf, len);
    c.tx_len = len;
    c.tx_busy = PROTO_TRUE;
    return PROTO_TRUE;
}

void pc_dma_close(uint8_t ch)
{
    if (ch >= PC_DMA_CHANNELS)
    {
        return;
    }
    s_dma.ch[ch].open = PROTO_FALSE;
}

void pc_dma_poll(void)
{
    for (uint8_t i = 0; i < PC_DMA_CHANNELS; i++)
    {
        if (s_dma.ch[i].open)
        {
            pump(s_dma.ch[i], i);
        }
    }
}

proto_bool pc_dma_sim_feed(uint8_t ch, const uint8_t *bytes, uint16_t len)
{
    if (ch >= PC_DMA_CHANNELS || !bytes)
    {
        return PROTO_FALSE;
    }
    dma_channel &c = s_dma.ch[ch];
    if (!c.open)
    {
        return PROTO_FALSE;
    }
    return c.ingress.push(bytes, len);
}

uint16_t pc_dma_sim_capture(uint8_t ch, uint8_t *out, uint16_t max)
{
    if (ch >= PC_DMA_CHANNELS || !out)
    {
        return 0;
    }
    dma_channel &c = s_dma.ch[ch];
    if (!c.open)
    {
        return 0;
    }
    return c.egress.pop(out, max);
}

#else // real silicon backend: dispatch to weak hooks a driver overrides (fail-closed).

extern "C"
{
    __attribute__((weak)) proto_bool pc_dma_hw_open(const pc_dma_config *cfg)
    {
        (void)cfg;
        return PROTO_FALSE;
    }
    __attribute__((weak)) proto_bool pc_dma_hw_tx_submit(uint8_t ch, const uint8_t *buf, uint16_t len)
    {
        (void)ch;
        (void)buf;
        (void)len;
        return PROTO_FALSE;
    }
    __attribute__((weak)) void pc_dma_hw_close(uint8_t ch)
    {
        (void)ch;
    }
    __attribute__((weak)) void pc_dma_hw_poll(void)
    {
    }
}

proto_bool pc_dma_open(const pc_dma_config *cfg)
{
    if (!cfg || !cfg->on_complete || cfg->channel >= PC_DMA_CHANNELS)
    {
        return PROTO_FALSE;
    }
    return pc_dma_hw_open(cfg);
}

proto_bool pc_dma_tx_submit(uint8_t ch, const uint8_t *buf, uint16_t len)
{
    if (ch >= PC_DMA_CHANNELS || !buf || len == 0 || len > PC_DMA_BUF_SIZE)
    {
        return PROTO_FALSE;
    }
    return pc_dma_hw_tx_submit(ch, buf, len);
}

void pc_dma_close(uint8_t ch)
{
    if (ch < PC_DMA_CHANNELS)
    {
        pc_dma_hw_close(ch);
    }
}

void pc_dma_poll(void)
{
    pc_dma_hw_poll();
}

#endif // PC_DMA_SIMULATE

#endif // PC_ENABLE_DMA
