// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file 8mbpsram.h
 * @brief 8 MB PSRAM profile - scale the RAM-backed pools up past the chip's internal SRAM.
 *
 * Included before the chip profile, so these win for the memory-bound sizes. Note: growing
 * the L1 slot count only pays off once the cache actually lives in PSRAM; until an edge-cache
 * PSRAM-resident placement lands, the internal DRAM budget still bounds these, so the bumps
 * here stay conservative. `#ifndef`-guarded, so your -D overrides still win.
 */

#ifndef PROTOCORE_8MBPSRAM_H
#define PROTOCORE_8MBPSRAM_H

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
#define MAX_TLS_CONNS 6
#endif
#ifndef PC_H2_MAX_STREAMS
#define PC_H2_MAX_STREAMS 16
#endif
#ifndef PC_H3_MAX_STREAMS
#define PC_H3_MAX_STREAMS 16
#endif
#ifndef PC_EDGE_CACHE_SLOTS
#define PC_EDGE_CACHE_SLOTS 16
#endif
#ifndef PC_EDGE_BODY_MAX
#define PC_EDGE_BODY_MAX 8192
#endif

#endif // PROTOCORE_8MBPSRAM_H
