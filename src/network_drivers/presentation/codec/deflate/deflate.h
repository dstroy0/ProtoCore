// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file deflate.h
 * @brief Bounded RFC 1951 DEFLATE compressor (DEFLATE) - no heap.
 *
 * The outbound counterpart to inflate.* : a small, host-testable DEFLATE used by
 * WebSocket permessage-deflate (RFC 7692) to compress server-to-client messages.
 * It emits a single fixed-Huffman block (no dynamic tables to build) with LZ77
 * back-references found over a bounded sliding window, then byte-aligns with an
 * empty stored block and removes the trailing 0x00 0x00 0xff 0xff per
 * RFC 7692 sec 7.2.1 - so the result is a ready-to-frame permessage-deflate
 * payload. The peer's INFLATE re-appends that marker before decompressing (our
 * own RX path does exactly that, see websocket.cpp).
 *
 * Matching reads from the source buffer itself - there is no kept window across
 * messages, which is correct for `no_context_takeover` (the mode the handshake
 * negotiates) and bounds memory: distances never exceed DEFLATE_WINDOW and the
 * only working memory is a caller-supplied scratch (DEFLATE_SCRATCH_SIZE bytes,
 * borrowed from the per-dispatch arena, like inflate).
 *
 * Fixed (not dynamic) Huffman keeps the encoder tiny and deterministic; it never
 * builds an optimal tree, so the ratio is modest, but for the small JSON/text
 * frames this serves it still shrinks the wire while costing no dedicated buffer.
 * If the output would not be smaller than the input the caller simply sends the
 * message uncompressed (the per-message RSV1 flag makes that legal).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DEFLATE_H
#define PROTOCORE_DEFLATE_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_WS_DEFLATE

/**
 * @brief Working-memory bytes deflate_raw() needs (hash chains + code tables).
 *
 * Pass a buffer at least this large as @p scratch. An internal static_assert
 * keeps it honest against the table layout.
 */
#define DEFLATE_SCRATCH_SIZE 4096

/** @brief deflate_raw() return codes (mirror ::InflateResult). */
enum class DeflateResult : int32_t
{
    DEFLATE_OK = 0,            ///< success; *out_len holds the compressed length
    DEFLATE_ERR_OVERFLOW = -2, ///< output would exceed dst_cap (incompressible)
    DEFLATE_ERR_SCRATCH = -3   ///< scratch_len < DEFLATE_SCRATCH_SIZE
};

/**
 * @brief Compress @p src into a raw permessage-deflate payload (RFC 7692).
 *
 * The output is a fixed-Huffman DEFLATE stream with the trailing 0x00 0x00 0xff
 * 0xff marker removed (RFC 7692 sec 7.2.1), i.e. exactly what a compressed
 * WebSocket data frame carries. To decompress, re-append that 4-byte marker and
 * call inflate_raw().
 *
 * @param src,src_len         input bytes to compress.
 * @param dst,dst_cap         output buffer and its capacity.
 * @param out_len             set to the compressed length on success.
 * @param scratch,scratch_len caller working memory (>= DEFLATE_SCRATCH_SIZE).
 * @return DeflateResult::DEFLATE_OK (0) on success, else a negative ::DeflateResult.
 */
DeflateResult deflate_raw(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len,
                          void *scratch, size_t scratch_len);

#endif // PC_ENABLE_WS_DEFLATE
#endif // PROTOCORE_DEFLATE_H
