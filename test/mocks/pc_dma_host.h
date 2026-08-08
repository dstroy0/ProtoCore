// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host DMA driver: the silicon backend, implemented for the test build.
//
// mmgr/dma.c dispatches every transfer to the weak pc_dma_hw_* hooks a real driver overrides.
// This is that driver. Defining them here strong makes the linker take these, so the dispatch arm
// the target takes is the arm that runs on the host, rather than a second in-library engine the
// target never executes.
//
// Staging runs on mmgr/ring.h rather than a private FIFO. A peripheral driver is exactly the
// caller that ring exists for, so if it cannot back one the fault is the ring's, and that shows up
// here instead of on silicon.
//
// Include from exactly one translation unit per test binary: the hook definitions are strong, so
// two includers would collide.
//
// The engine advances only in pc_dma_hw_poll(), so a test decides when a transfer completes and
// the completion callback runs where the ISR would put it.

#ifndef PROTOCORE_PC_DMA_HOST_H
#define PROTOCORE_PC_DMA_HOST_H

#include "mmgr/dma.h"
#include "mmgr/ring.h"
#include <stdint.h>
#include <string.h>

#if PC_ENABLE_DMA

// Several buffers' worth, so one feed spans more than one transfer and exercises the ping-pong
// flip, and several submits accumulate before a capture. A ring capacity is a power of two, which
// is what makes the index a mask.
#define PC_DMA_HOST_STAGE (PC_DMA_BUF_SIZE * 4u)

static_assert(PC_RING_POW2(PC_DMA_HOST_STAGE),
              "PC_DMA_BUF_SIZE must be a power of two: the staging ring indexes with a mask");

typedef struct
{
    uint8_t buf[PC_DMA_HOST_STAGE];
    _Atomic size_t head;
    _Atomic size_t tail;
} PcDmaStage;

typedef struct
{
    proto_bool open;
    pc_dma_config cfg;

    PcDmaStage rx; ///< injected, waiting to complete as ingress
    PcDmaStage tx; ///< transmitted, waiting to be read back

    uint16_t tx_pending;             ///< submitted, not yet completed
    uint8_t buf[2][PC_DMA_BUF_SIZE]; ///< the ping-pong pair an ingress completion points at
    uint8_t bank;                    ///< the half of the pair the next completion fills
    uint16_t seq;
} PcDmaHostCh;

__attribute__((weak)) PcDmaHostCh g_pc_dma[PC_DMA_CHANNELS];

// A transfer that does not fit is refused whole rather than truncated: a partial one would put
// bytes on the line no sender ever submitted. This is the ring's own lossless-backpressure rule.
static inline proto_bool pc_dma_stage_put(PcDmaStage *s, const uint8_t *src, uint16_t n)
{
    if (src == NULL || n == 0 || n > pc_ring_free(&s->head, &s->tail, PC_DMA_HOST_STAGE))
    {
        return PROTO_FALSE;
    }
    size_t h = PROTO_ATOMIC_LOAD(&s->head);
    h = pc_ring_write_span(s->buf, PC_DMA_HOST_STAGE, h, src, n);
    PROTO_ATOMIC_STORE(&s->head, h);
    return PROTO_TRUE;
}

static inline uint16_t pc_dma_stage_get(PcDmaStage *s, uint8_t *out, uint16_t max)
{
    return (uint16_t)pc_ring_read(s->buf, PC_DMA_HOST_STAGE, &s->head, &s->tail, out, max);
}

// --- the seam mmgr/dma.c dispatches to --------------------------------------

proto_bool pc_dma_hw_open(const pc_dma_config *cfg)
{
    if (cfg == NULL || cfg->channel >= PC_DMA_CHANNELS || g_pc_dma[cfg->channel].open)
    {
        return PROTO_FALSE;
    }
    PcDmaHostCh *c = &g_pc_dma[cfg->channel];
    memset(c, 0, sizeof(*c));
    c->cfg = *cfg;
    c->open = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool pc_dma_hw_tx_submit(uint8_t ch, const uint8_t *buf, uint16_t len)
{
    if (ch >= PC_DMA_CHANNELS || !g_pc_dma[ch].open)
    {
        return PROTO_FALSE;
    }
    PcDmaHostCh *c = &g_pc_dma[ch];
    // One transfer in flight per channel: a descriptor still loaded takes no second submit, and
    // the caller waits for the TX-complete event (mmgr/dma.h).
    if (c->tx_pending > 0)
    {
        return PROTO_FALSE;
    }
    if (!pc_dma_stage_put(&c->tx, buf, len))
    {
        return PROTO_FALSE;
    }
    c->tx_pending = len;
    return PROTO_TRUE;
}

void pc_dma_hw_close(uint8_t ch)
{
    if (ch < PC_DMA_CHANNELS)
    {
        memset(&g_pc_dma[ch], 0, sizeof(g_pc_dma[ch]));
    }
}

static inline void pc_dma_host_complete(PcDmaHostCh *c, uint8_t ch, pc_dma_dir dir, const uint8_t *data, uint16_t len)
{
    pc_dma_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data = data;
    ev.len = len;
    ev.seq = c->seq;
    ev.channel = ch;
    ev.periph = c->cfg.periph;
    ev.dir = dir;
    c->seq = (uint16_t)(c->seq + 1u);
    c->cfg.on_complete(&ev, c->cfg.ctx);
}

void pc_dma_hw_poll(void)
{
    for (uint8_t ch = 0; ch < PC_DMA_CHANNELS; ch++)
    {
        PcDmaHostCh *c = &g_pc_dma[ch];
        if (!c->open)
        {
            continue;
        }

        // Egress completes first, so a loopback channel's own bytes arrive as ingress in the same
        // poll, which is what a wired loopback does.
        if (c->tx_pending > 0)
        {
            uint16_t n = c->tx_pending;
            c->tx_pending = 0;
            pc_dma_host_complete(c, ch, PC_DMA_TX, NULL, n);
            if (c->cfg.loopback)
            {
                uint8_t moved[PC_DMA_HOST_STAGE];
                uint16_t got = pc_dma_stage_get(&c->tx, moved, n);
                (void)pc_dma_stage_put(&c->rx, moved, got);
            }
        }

        // One transfer per poll, so a feed larger than the buffer flips through it. The completed
        // half stays intact while the engine fills the other, which is what the pair is for: the
        // callback reads its bytes after the next transfer has already been armed.
        uint16_t n = pc_dma_stage_get(&c->rx, c->buf[c->bank], PC_DMA_BUF_SIZE);
        if (n > 0)
        {
            pc_dma_host_complete(c, ch, PC_DMA_RX, c->buf[c->bank], n);
            c->bank ^= 1u;
        }
    }
}

// --- what a test drives it with ---------------------------------------------

/** @brief Inject @p len bytes as if they arrived on @p ch's RX line. */
static inline proto_bool pc_dma_host_feed(uint8_t ch, const uint8_t *bytes, uint16_t len)
{
    if (ch >= PC_DMA_CHANNELS || !g_pc_dma[ch].open)
    {
        return PROTO_FALSE;
    }
    return pc_dma_stage_put(&g_pc_dma[ch].rx, bytes, len);
}

/** @brief Read back up to @p max bytes @p ch transmitted. */
static inline uint16_t pc_dma_host_capture(uint8_t ch, uint8_t *out, uint16_t max)
{
    if (ch >= PC_DMA_CHANNELS || out == NULL || !g_pc_dma[ch].open)
    {
        return 0;
    }
    return pc_dma_stage_get(&g_pc_dma[ch].tx, out, max);
}

/** @brief Forget every channel, so one case cannot inherit another's staging. */
static inline void pc_dma_host_reset(void)
{
    memset(g_pc_dma, 0, sizeof(g_pc_dma));
}

#endif // PC_ENABLE_DMA

#endif // PROTOCORE_PC_DMA_HOST_H
