// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_DEFLATE_H
#define PROTOCORE_DEFLATE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_DEFLATE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/**
 * @brief Working-memory bytes deflate_raw() needs (hash chains + code tables).
 *
 * Pass a buffer at least this large as @p scratch. An internal static_assert
 * keeps it honest against the table layout.
 */
#define DEFLATE_SCRATCH_SIZE 4096

/** @brief deflate_raw() return codes (mirror ::InflateResult). */
typedef enum PROTO_ENUM_PACKED
{
    DEFLATE_OK = 0,            ///< success; *out_len holds the compressed length
    DEFLATE_ERR_OVERFLOW = -2, ///< output would exceed dst_cap (incompressible)
    DEFLATE_ERR_SCRATCH = -3   ///< scratch_len < DEFLATE_SCRATCH_SIZE
} DeflateResult;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    DeflateResult (*raw)(uint8_t *, const uint8_t *, size_t, uint8_t *, size_t, size_t *, void *, size_t);
} DeflateNs;
PROTOCORE_NS_LAYOUT(DeflateNs, raw);

/**
 * @brief Compress src into a raw permessage-deflate payload (RFC 7692): a .
 * @param work PROTOCORE_DEFLATE_BORROW bytes the caller took. Not held past the call.
 * @param src Src
 * @param src_len Src len
 * @param dst Dst
 * @param dst_cap Dst cap
 * @param out_len Out len
 * @param scratch Scratch
 * @param scratch_len Scratch len
 * @return The DeflateResult.
 */
DeflateResult protocore_deflate_raw(uint8_t *work, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                                    size_t *out_len, void *scratch, size_t scratch_len);

/** @brief Module namespace. */
PROTOCORE_NS DeflateNs Deflate PROTOCORE_UNUSED = {.raw = protocore_deflate_raw};

PROTOCORE_END_DECLS

#endif // PROTOCORE_DEFLATE_H
