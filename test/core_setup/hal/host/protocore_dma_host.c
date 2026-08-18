// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_dma_host.c
 * @brief The one translation unit holding the host DMA seam. See protocore_dma_host.h.
 *
 * The header carries the types, the staging rings and the injection helpers, all of which any
 * number of translation units may include. The four functions mmgr/dma.c dispatches to are
 * defined here alone, so they override its weak defaults without colliding with each other.
 */

#define PROTOCORE_DMA_HOST_IMPL
#include "protocore_dma_host.h"
