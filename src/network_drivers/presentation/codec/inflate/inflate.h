// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_WS_DEFLATE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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
} InflateRawArgs;
/**
 * @brief Bounded RFC 1951 DEFLATE decompressor (INFLATE) - no heap.
 *
 * A caller sets the members a call takes, invokes it through ::Inflate with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Inflate.raw_args.src = ...;
 *   Inflate.raw_args.src_len = ...;
 *   Inflate.raw_args.dst = ...;
 *   Inflate.raw_args.dst_cap = ...;
 *   Inflate.raw_args.out_len = ...;
 *   Inflate.raw_args.scratch = ...;
 *   Inflate.raw_args.scratch_len = ...;
 *   Inflate.raw(work);
 *   // Inflate.value is what the call reports
 *
 * @var InflateNs::raw_args  what raw takes: src, src_len, dst, dst_cap, out_len,
 * @var InflateNs::ok  a call's true/false outcome
 * @var InflateNs::value  the value a call reports
 * @var InflateNs::raw  decompress a raw DEFLATE (RFC 1951) stream. dst is also the window, ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    InflateRawArgs raw_args;
    proto_bool ok;
    InflateResult value;
} InflateVars;

/** @brief The operands and the outcome. */
extern InflateVars InflateV;

/** @brief The entries. */
typedef struct
{
    void (*const raw)(uint8_t *restrict work);
} InflateNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in InflateV or a region of the borrow at a fixed offset.
void protocore_inflate_raw(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Inflate.raw(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const InflateNs Inflate __attribute__((unused)) = {
    .raw = protocore_inflate_raw,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WS_DEFLATE

#endif // PROTOCORE_INFLATE_H
