// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_WS_DEFLATE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What raw takes: src, src_len, dst, dst_cap, out_len, ... */
typedef struct
{
    const uint8_t *src;
    size_t src_len;
    uint8_t *dst;
    size_t dst_cap;
    size_t *out_len;
    void *scratch;
    size_t scratch_len;
} DeflateRawArgs;
/**
 * @brief Bounded RFC 1951 DEFLATE compressor (DEFLATE) - no heap.
 *
 * A caller sets the members a call takes, invokes it through ::Deflate with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Deflate.raw_args.src = ...;
 *   Deflate.raw_args.src_len = ...;
 *   Deflate.raw_args.dst = ...;
 *   Deflate.raw_args.dst_cap = ...;
 *   Deflate.raw_args.out_len = ...;
 *   Deflate.raw_args.scratch = ...;
 *   Deflate.raw_args.scratch_len = ...;
 *   Deflate.raw(work);
 *   // Deflate.value is what the call reports
 *
 * @var DeflateNs::raw_args  what raw takes: src, src_len, dst, dst_cap, out_len,
 * @var DeflateNs::ok  a call's true/false outcome
 * @var DeflateNs::value  the value a call reports
 * @var DeflateNs::raw  compress src into a raw permessage-deflate payload (RFC 7692): a ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    DeflateRawArgs raw_args;
    proto_bool ok;
    DeflateResult value;
} DeflateVars;

/** @brief The operands and the outcome. */
extern DeflateVars DeflateV;

/** @brief The entries. */
typedef struct
{
    void (*const raw)(uint8_t *restrict work);
} DeflateNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DeflateV or a region of the borrow at a fixed offset.
void protocore_deflate_raw(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Deflate.raw(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DeflateNs Deflate __attribute__((unused)) = {
    .raw = protocore_deflate_raw,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WS_DEFLATE

#endif // PROTOCORE_DEFLATE_H
