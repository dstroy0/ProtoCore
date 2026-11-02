// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host DMA driver: the silicon backend, implemented for the test build.
//
// mmgr/dma.c dispatches every transfer to the weak protocore_dma_hw_* hooks a real driver overrides.
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
// The engine advances only in mmgr_praet_hw_poll(), so a test decides when a transfer completes and
// the completion callback runs where the ISR would put it.

#ifndef PROTOCORE_PROTOCORE_DMA_HOST_H
#define PROTOCORE_PROTOCORE_DMA_HOST_H

#include "memoria_anularis/memoria_anularis.h"
#include "memoriam_praetereo/memoriam_praetereo.h"
#include <stdint.h>
#include <string.h>

#if PROTOCORE_ENABLE_DMA

// What an event's direction and a channel's peripheral mean on this seam.
enum
{
    PROTOCORE_DMA_RX = 0,
    PROTOCORE_DMA_TX = 1,
};
enum
{
    PROTOCORE_DMA_UART = 0,
    PROTOCORE_DMA_I2C = 1,
    PROTOCORE_DMA_SPI = 2,
};

// Several buffers' worth, so one feed spans more than one transfer and exercises the ping-pong
// flip, and several submits accumulate before a capture. A ring capacity is a power of two, which
// is what makes the index a mask.
#define PROTOCORE_DMA_HOST_STAGE (PROTOCORE_DMA_BUF_SIZE * 4u)

static_assert(MMGR_RING_POW2(PROTOCORE_DMA_HOST_STAGE),
              "PROTOCORE_DMA_BUF_SIZE must be a power of two: the staging ring indexes with a mask");

typedef struct
{
    uint8_t buf[PROTOCORE_DMA_HOST_STAGE];
    _Atomic size_t head;
    _Atomic size_t tail;
} PcDmaStage;

typedef struct
{
    proto_bool open;
    uint8_t peripheral;
    embed_bool loopback;
    const PraetCallbackCfg *on_complete;

    PcDmaStage rx; ///< injected, waiting to complete as ingress
    PcDmaStage tx; ///< transmitted, waiting to be read back

    uint16_t tx_pending;                    ///< submitted, not yet completed
    uint8_t buf[2][PROTOCORE_DMA_BUF_SIZE]; ///< the ping-pong pair an ingress completion points at
    uint8_t bank;                           ///< the half of the pair the next completion fills
    uint16_t seq;
} PcDmaHostCh;

// One definition per name across every TU that includes this, kept by the linker: selectany on
// PE/COFF, weak on ELF.
#ifndef PROTOCORE_HOST_SHARED
#if defined(_WIN32)
#define PROTOCORE_HOST_SHARED __declspec(selectany)
#else
#define PROTOCORE_HOST_SHARED __attribute__((weak))
#endif
#endif

PROTOCORE_HOST_SHARED PcDmaHostCh g_protocore_dma[PROTOCORE_DMA_CHANNELS];

// A transfer that does not fit is refused whole rather than truncated: a partial one would put
// bytes on the line no sender ever submitted. This is the ring's own lossless-backpressure rule.
static inline proto_bool protocore_dma_stage_put(PcDmaStage *s, const uint8_t *src, uint16_t n)
{
    const size_t tail = PROTO_ATOMIC_LOAD(&s->tail);
    size_t h = PROTO_ATOMIC_LOAD(&s->head);
    const size_t room = (PROTOCORE_DMA_HOST_STAGE - 1u) - MMGR_RING_WRAP(h - tail, PROTOCORE_DMA_HOST_STAGE);
    if (src == NULL || n == 0 || n > room)
    {
        return PROTO_FALSE;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        s->buf[h] = src[i];
        h = MMGR_RING_WRAP(h + 1u, PROTOCORE_DMA_HOST_STAGE);
    }
    PROTO_ATOMIC_STORE(&s->head, h);
    return PROTO_TRUE;
}

static inline uint16_t protocore_dma_stage_get(PcDmaStage *s, uint8_t *out, uint16_t max)
{
    const size_t h = PROTO_ATOMIC_LOAD(&s->head);
    size_t t = PROTO_ATOMIC_LOAD(&s->tail);
    uint16_t n = 0;
    while (n < max && t != h)
    {
        out[n] = s->buf[t];
        n++;
        t = MMGR_RING_WRAP(t + 1u, PROTOCORE_DMA_HOST_STAGE);
    }
    PROTO_ATOMIC_STORE(&s->tail, t);
    return n;
}

static inline void protocore_dma_host_complete(PcDmaHostCh *c, uint8_t ch, uint8_t direction, const uint8_t *data,
                                               uint16_t len)
{
    mmgr_praet_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data = data;
    ev.bytes = len;
    ev.sequence = c->seq;
    ev.channel = ch;
    ev.peripheral = c->peripheral;
    ev.direction = direction;
    c->seq = (uint16_t)(c->seq + 1u);
    c->on_complete->callback(&ev, c->on_complete->user);
}

// --- what a test drives it with ---------------------------------------------

/** @brief Inject @p len bytes as if they arrived on @p ch's RX line. */
static inline proto_bool protocore_dma_host_feed(uint8_t ch, const uint8_t *bytes, uint16_t len)
{
    if (ch >= PROTOCORE_DMA_CHANNELS || !g_protocore_dma[ch].open)
    {
        return PROTO_FALSE;
    }
    return protocore_dma_stage_put(&g_protocore_dma[ch].rx, bytes, len);
}

/** @brief Read back up to @p max bytes @p ch transmitted. */
static inline uint16_t protocore_dma_host_capture(uint8_t ch, uint8_t *out, uint16_t max)
{
    if (ch >= PROTOCORE_DMA_CHANNELS || out == NULL || !g_protocore_dma[ch].open)
    {
        return 0;
    }
    return protocore_dma_stage_get(&g_protocore_dma[ch].tx, out, max);
}

/** @brief Forget every channel, so one case cannot inherit another's staging. */
static inline void protocore_dma_host_reset(void)
{
    memset(g_protocore_dma, 0, sizeof(g_protocore_dma));
}

// --- the seam memoriam_praetereo.c dispatches to ----------------------------

// Declared here and defined once, in protocore_dma_host.c. They cannot be defined in this header:
// Unity's runner generator copies a suite's includes into unity_runner.c, so a header definition
// lands in two translation units and the link fails on all four. They also cannot be weak, because
// memoriam_praetereo.c's defaults already are and the two would tie instead of one winning. The
// declarations are memoriam_praetereo.h's own.
#ifdef PROTOCORE_DMA_HOST_IMPL
embed_bool mmgr_praet_hw_open(const PraetCfg *args)
{
    if (args == NULL || args->channel >= PROTOCORE_DMA_CHANNELS || g_protocore_dma[args->channel].open)
    {
        return PROTO_FALSE;
    }
    PcDmaHostCh *c = &g_protocore_dma[args->channel];
    memset(c, 0, sizeof(*c));
    c->peripheral = args->peripheral;
    c->loopback = args->loopback;
    c->on_complete = args->on_complete;
    c->open = PROTO_TRUE;
    return PROTO_TRUE;
}

embed_bool mmgr_praet_hw_tx_submit(const PraetTransferCfg *args)
{
    const uint8_t ch = args->channel;
    if (ch >= PROTOCORE_DMA_CHANNELS || !g_protocore_dma[ch].open)
    {
        return PROTO_FALSE;
    }
    PcDmaHostCh *c = &g_protocore_dma[ch];
    // One transfer in flight per channel: a descriptor still loaded takes no second submit, and
    // the caller waits for the TX-complete event.
    if (c->tx_pending > 0)
    {
        return PROTO_FALSE;
    }
    if (!protocore_dma_stage_put(&c->tx, args->buf, args->bytes))
    {
        return PROTO_FALSE;
    }
    c->tx_pending = args->bytes;
    return PROTO_TRUE;
}

void mmgr_praet_hw_close(const PraetTransferCfg *args)
{
    if (args->channel < PROTOCORE_DMA_CHANNELS)
    {
        memset(&g_protocore_dma[args->channel], 0, sizeof(g_protocore_dma[args->channel]));
    }
}

void mmgr_praet_hw_poll(const PraetCfg *args)
{
    (void)args; // the simulated engine advances every open channel on one poll
    for (uint8_t ch = 0; ch < PROTOCORE_DMA_CHANNELS; ch++)
    {
        PcDmaHostCh *c = &g_protocore_dma[ch];
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
            protocore_dma_host_complete(c, ch, PROTOCORE_DMA_TX, NULL, n);
            if (c->loopback)
            {
                uint8_t moved[PROTOCORE_DMA_HOST_STAGE];
                uint16_t got = protocore_dma_stage_get(&c->tx, moved, n);
                (void)protocore_dma_stage_put(&c->rx, moved, got);
            }
        }

        // One transfer per poll, so a feed larger than the buffer flips through it. The completed
        // half stays intact while the engine fills the other, which is what the pair is for: the
        // callback reads its bytes after the next transfer has already been armed.
        uint16_t n = protocore_dma_stage_get(&c->rx, c->buf[c->bank], PROTOCORE_DMA_BUF_SIZE);
        if (n > 0)
        {
            protocore_dma_host_complete(c, ch, PROTOCORE_DMA_RX, c->buf[c->bank], n);
            c->bank ^= 1u;
        }
    }
}
#endif // PROTOCORE_DMA_HOST_IMPL

#endif // PROTOCORE_ENABLE_DMA

#endif // PROTOCORE_PROTOCORE_DMA_HOST_H
