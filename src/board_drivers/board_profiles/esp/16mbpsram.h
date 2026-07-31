// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file 16mbpsram.h
 * @brief 16 MB PSRAM profile - larger RAM-backed pools than the 8 MB profile.
 *
 * Included before the chip profile (wins for memory-bound sizes). Same PSRAM-resident caveat
 * as 8mbpsram.h: the internal DRAM budget bounds the L1 slot count until an edge-cache
 * PSRAM placement lands. `#ifndef`-guarded, so your -D overrides still win.
 */

#ifndef PROTOCORE_16MBPSRAM_H
#define PROTOCORE_16MBPSRAM_H

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
#define MAX_TLS_CONNS 8
#endif
#ifndef PC_H2_MAX_STREAMS
#define PC_H2_MAX_STREAMS 32
#endif
#ifndef PC_H3_MAX_STREAMS
#define PC_H3_MAX_STREAMS 32
#endif
#ifndef PC_EDGE_CACHE_SLOTS
#define PC_EDGE_CACHE_SLOTS 24
#endif
#ifndef PC_EDGE_BODY_MAX
#define PC_EDGE_BODY_MAX 8192
#endif

#endif // PROTOCORE_16MBPSRAM_H
