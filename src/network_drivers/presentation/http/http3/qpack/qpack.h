// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file qpack.h
 * @brief QPACK field-section compression for HTTP/3 (RFC 9204).
 *
 * QPACK is HTTP/3's header compression. It reuses RFC 7541's prefix-integer coding and Huffman
 * code (shared here via protocore_hpack_prim.h) and adds a 99-entry static table, an encoded field-section
 * prefix, and its own field-line representations.
 *
 * This codec is static-table-only and needs no per-connection state: the encoder emits indexed /
 * literal representations against the static table (never inserting into a dynamic table), and it
 * advertises SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0, so a conformant peer's encoder never sends a
 * dynamic-table reference. The decoder therefore rejects (returns false) any representation that
 * references the dynamic table or a non-zero Required Insert Count. Pure, zero heap, host-tested
 * against the RFC 9204 Appendix B.1 worked example.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QPACK_H
#define PROTOCORE_QPACK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Callback invoked for each decoded header; return false to abort the decode. */
typedef proto_bool (*QpackEmitFn)(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len);

/** @brief What encode_prefix takes: out, cap. */
typedef struct
{
    uint8_t *out;
    size_t cap;
} QpackEncodePrefixArgs;

/** @brief What encode_header takes: out, cap, name, name_len, value, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const char *name;
    size_t name_len;
    const char *value;
    size_t value_len;
} QpackEncodeHeaderArgs;

/** @brief What decode takes: block, len, scratch, scratch_cap, emit, ... */
typedef struct
{
    const uint8_t *block;
    size_t len;
    char *scratch; ///< caller buffer holding one header's name+value during each emit call
    size_t scratch_cap;
    QpackEmitFn emit;
    void *ctx;
} QpackDecodeArgs;

/**
 * @brief QPACK field-section compression for HTTP/3 (RFC 9204).
 *
 * A caller sets the members a call takes, invokes it through ::Qpack with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Qpack.encode_prefix_args.out = ...;
 *   Qpack.encode_prefix_args.cap = ...;
 *   Qpack.encode_prefix(work);
 *   // Qpack.n is what the call reports
 *
 * @var QpackNs::encode_prefix_args  what encode_prefix takes: out, cap
 * @var QpackNs::encode_header_args  what encode_header takes: out, cap, name, name_len, value,
 * @var QpackNs::decode_args  what decode takes: block, len, scratch, scratch_cap, emit,
 * @var QpackNs::ok  true if the section decoded cleanly; false on malformed input, ...
 * @var QpackNs::n  bytes written (2), or 0 if cap < 2
 * @var QpackNs::encode_prefix  write the encoded field-section prefix for a static-only section. ...
 * @var QpackNs::encode_header  encode one header field (server side): a full static match -> ...
 * @var QpackNs::decode  decode a whole QPACK field section (prefix + representations), ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    QpackEncodePrefixArgs encode_prefix_args;
    QpackEncodeHeaderArgs encode_header_args;
    QpackDecodeArgs decode_args;
    proto_bool ok;
    size_t n;
} QpackVars;

/** @brief The operands and the outcome. */
extern QpackVars QpackV;

/** @brief The entries. */
typedef struct
{
    void (*const encode_prefix)(uint8_t *restrict work);
    void (*const encode_header)(uint8_t *restrict work);
    void (*const decode)(uint8_t *restrict work);
} QpackNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in QpackV or a region of the borrow at a fixed offset.
void protocore_qpack_encode_prefix(uint8_t *restrict work);
void protocore_qpack_encode_header(uint8_t *restrict work);
void protocore_qpack_decode(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Qpack.encode_prefix(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const QpackNs Qpack __attribute__((unused)) = {
    .encode_prefix = protocore_qpack_encode_prefix,
    .encode_header = protocore_qpack_encode_header,
    .decode = protocore_qpack_decode,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QPACK_H
