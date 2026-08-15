// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dma.c
 * @brief DMA peripheral ingest / egress - implementation.
 *
 * The front end validates and dispatches to the weak protocore_dma_hw_* hooks a driver overrides.
 * The host build overrides them too (core_setup/hal/host/protocore_dma_host.h), so the arm that runs on
 * silicon is the arm the tests drive.
 */

#include "mmgr/dma.h"

#if PROTOCORE_ENABLE_DMA

// The driver stamps a completion, so the clock is its dependency rather than this front end's.
__attribute__((weak)) proto_bool protocore_dma_hw_open(const protocore_dma_config *cfg)
{
    (void)cfg;
    return PROTO_FALSE;
}
__attribute__((weak)) proto_bool protocore_dma_hw_tx_submit(uint8_t ch, const uint8_t *buf, uint16_t len)
{
    (void)ch;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}
__attribute__((weak)) void protocore_dma_hw_close(uint8_t ch)
{
    (void)ch;
}
__attribute__((weak)) void protocore_dma_hw_poll(void)
{
}

proto_bool protocore_dma_open(const protocore_dma_config *cfg)
{
    if (!cfg || !cfg->on_complete || cfg->channel >= PROTOCORE_DMA_CHANNELS)
    {
        return PROTO_FALSE;
    }
    return protocore_dma_hw_open(cfg);
}

proto_bool protocore_dma_tx_submit(uint8_t ch, const uint8_t *buf, uint16_t len)
{
    if (ch >= PROTOCORE_DMA_CHANNELS || !buf || len == 0 || len > PROTOCORE_DMA_BUF_SIZE)
    {
        return PROTO_FALSE;
    }
    return protocore_dma_hw_tx_submit(ch, buf, len);
}

void protocore_dma_close(uint8_t ch)
{
    if (ch < PROTOCORE_DMA_CHANNELS)
    {
        protocore_dma_hw_close(ch);
    }
}

void protocore_dma_poll(void)
{
    protocore_dma_hw_poll();
}

#endif // PROTOCORE_ENABLE_DMA
