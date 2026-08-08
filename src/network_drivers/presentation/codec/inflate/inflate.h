// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file inflate.h
 * @brief Bounded RFC 1951 DEFLATE decompressor (INFLATE) - no heap.
 *
 * A small, host-testable INFLATE used by WebSocket permessage-deflate
 * (RFC 7692). It decompresses a *raw* DEFLATE stream (no zlib/gzip wrapper) into
 * a caller buffer. LZ77 back-references read from the output buffer itself, so
 * there is no separate sliding window - the output buffer *is* the window. That
 * is correct for permessage-deflate's `no_context_takeover` mode (each message
 * is independent) and bounds memory: a decompressed message must fit @p dst_cap.
 *
 * The only working memory is a Huffman-table scratch the caller supplies
 * (INFLATE_SCRATCH_SIZE bytes); the WebSocket layer borrows it from the
 * per-dispatch scratch arena, so INFLATE costs no dedicated buffer.
 *
 * Decoding terminates at a final block (BFINAL) or at a clean end-of-input on a
 * block boundary - so it accepts a permessage-deflate payload, which carries no
 * final block (the caller appends the 0x00 0x00 0xff 0xff marker per
 * RFC 7692 §7.2.2 before calling).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_INFLATE_H
#define PROTOCORE_INFLATE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_WS_DEFLATE

/**
 * @brief Working-memory bytes inflate_raw() needs for its Huffman tables.
 *
 * Pass a buffer at least this large as @p scratch. (Sized for the worst-case
 * dynamic-block tables; an internal static_assert keeps it honest.)
 */
#define INFLATE_SCRATCH_SIZE 1536

/** @brief inflate_raw() return codes. */
typedef enum PROTO_ENUM_PACKED
{
    INFLATE_OK = 0,             ///< success; *out_len holds the decompressed length
    INFLATE_ERR_MALFORMED = -1, ///< invalid / truncated DEFLATE stream
    INFLATE_ERR_OVERFLOW = -2,  ///< output would exceed dst_cap
    INFLATE_ERR_SCRATCH = -3    ///< scratch_len < INFLATE_SCRATCH_SIZE
} InflateResult;

/**
 * @brief The one call, and the module's only symbol.
 *
 * @var InflateNs::raw  Decompress a raw DEFLATE (RFC 1951) stream. @p dst is also the window, so a
 *                    decompressed message must fit @p dst_cap.
 *                    @p out_len takes the length on success and @p scratch is caller working memory
 *                    of at least INFLATE_SCRATCH_SIZE bytes. INFLATE_OK (0), else a negative ::InflateResult
 */
typedef struct
{
    InflateResult (*raw)(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len,
                         void *scratch, size_t scratch_len);
} InflateNs;

/** @brief The one symbol this module exports. */
extern const InflateNs Inflate;

#endif // PC_ENABLE_WS_DEFLATE

PROTO_END_DECLS

#endif // PROTOCORE_INFLATE_H
