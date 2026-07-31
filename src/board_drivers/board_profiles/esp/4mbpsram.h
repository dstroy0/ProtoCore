// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file 4mbpsram.h
 * @brief 4 MB PSRAM profile - between the 2 MB and 8 MB steps.
 *
 * Included before the chip profile (wins for memory-bound sizes). Same PSRAM-resident caveat as
 * 8mbpsram.h. `#ifndef`-guarded, so your -D overrides still win.
 */

#ifndef PROTOCORE_4MBPSRAM_H
#define PROTOCORE_4MBPSRAM_H

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
#define MAX_TLS_CONNS 4
#endif
#ifndef PC_H2_MAX_STREAMS
#define PC_H2_MAX_STREAMS 16
#endif
#ifndef PC_H3_MAX_STREAMS
#define PC_H3_MAX_STREAMS 16
#endif

#endif // PROTOCORE_4MBPSRAM_H
