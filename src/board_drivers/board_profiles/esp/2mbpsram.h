// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file 2mbpsram.h
 * @brief 2 MB PSRAM profile - the smallest external-RAM step (e.g. classic WROVER, some C-series).
 *
 * Included before the chip profile (wins for memory-bound sizes). A modest lift over the no-PSRAM
 * floor; the larger PSRAM profiles scale further. Same PSRAM-resident caveat as 8mbpsram.h: the
 * internal DRAM budget still bounds the L1 slot count until an edge-cache PSRAM placement lands.
 * `#ifndef`-guarded, so your -D overrides still win.
 */

#ifndef PROTOCORE_2MBPSRAM_H
#define PROTOCORE_2MBPSRAM_H

// PSRAM fitted: move the big TLS/HTTP-2/zlib pools off internal DRAM and raise concurrency.
#ifndef PC_TLS_ARENA_IN_PSRAM
#define PC_TLS_ARENA_IN_PSRAM 1
#endif
#ifndef PC_H2_POOL_IN_PSRAM
#define PC_H2_POOL_IN_PSRAM 1
#endif
#ifndef PC_SSH_ZLIB_IN_PSRAM
#define PC_SSH_ZLIB_IN_PSRAM 1
#endif
#ifndef MAX_TLS_CONNS
#define MAX_TLS_CONNS 3
#endif

#endif // PROTOCORE_2MBPSRAM_H
