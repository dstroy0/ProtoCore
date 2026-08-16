// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hpack.h
 * @brief HPACK header compression for HTTP/2 (RFC 7541).
 *
 * HPACK encodes an HTTP header list into a compact byte block and back. It combines a fixed
 * static table (61 common header entries), a per-connection FIFO dynamic table, prefix-integer
 * and length-prefixed string coding, and a canonical Huffman code (RFC 7541 Appendix B) for
 * string literals. All tables are generated verbatim from the RFC.
 *
 * This codec is pure and host-tested (against the RFC 7541 Appendix C worked examples). The
 * decoder resolves indexed fields, literals (with / without / never indexed), and dynamic-table
 * size updates, maintaining the dynamic table with FIFO eviction. The encoder (server side) uses
 * static-table indexing and literal-without-indexing with Huffman-coded strings, so it needs no
 * dynamic-table state of its own. Zero heap; the dynamic table is a fixed byte ring.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HPACK_H
#define PROTOCORE_HPACK_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_HTTP2

PROTOCORE_BEGIN_DECLS

/** @brief Callback invoked for each decoded header; return false to abort the decode. */
typedef proto_bool (*HpackEmitFn)(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len);

/** @brief RFC 7541 sec 4.2: the size a table is initialised to. */
typedef struct
{
    uint32_t max_bytes; ///< the negotiated maximum, 0 = PROTOCORE_HPACK_TABLE_BYTES
} HpackInitArgs;

/** @brief RFC 7541 sec 3: the block a decode walks, and where each field is handed on. */
typedef struct
{
    const uint8_t *block; ///< the header block
    size_t len;           ///< how many bytes
    char *scratch;        ///< holds one header's name+value during each emit
    size_t scratch_cap;   ///< how much room it has
    HpackEmitFn emit;     ///< run for each decoded field
    void *ctx;            ///< passed to emit
} HpackDecodeArgs;

/** @brief RFC 7541 sec 6: the field an encode emits. */
typedef struct
{
    uint8_t *out;      ///< where the field is written
    size_t cap;        ///< how much room it has
    const char *name;  ///< the field name
    size_t name_len;   ///< how many bytes
    const char *value; ///< the field value
    size_t value_len;  ///< how many bytes
} HpackEncodeArgs;

/**
 * @brief HPACK (RFC 7541): the peer encoder's dynamic table, and one field at a time.
 *
 * A caller sets the members a call takes, invokes it through ::Hpack, and reads the outcome off the
 * same handle.
 *
 * @var HpackNs::init_args    the size a table is initialised to
 * @var HpackNs::decode_args  the block a decode walks
 * @var HpackNs::encode_args  the field an encode emits
 * @var HpackNs::ok           whether the whole block decoded cleanly
 * @var HpackNs::n            bytes an encode wrote, or 0 on overflow
 * @var HpackNs::dyn_init      empty the table and set its maximum
 * @var HpackNs::decode        decode a block, emitting each field
 * @var HpackNs::encode_header encode one field
 *
 * Every entry takes the borrow the table lives in; ::PROTOCORE_HPACK_BORROW is how many bytes that
 * is. The encode reads no table, so it takes the borrow and ignores it.
 */
typedef struct
{
    HpackInitArgs init_args;     ///< the members ::HpackNs::dyn_init takes
    HpackDecodeArgs decode_args; ///< the members ::HpackNs::decode takes
    HpackEncodeArgs encode_args; ///< the members ::HpackNs::encode_header takes

    proto_bool ok; ///< whether the whole block decoded cleanly
    size_t n;      ///< bytes an encode wrote, or 0 on overflow

    void (*const dyn_init)(uint8_t *restrict work);
    void (*const decode)(uint8_t *restrict work);
    void (*const encode_header)(uint8_t *restrict work);
} HpackNs;

/** @brief The one symbol this module exports. */
extern HpackNs Hpack;

// The prefix-integer and Huffman primitives moved to protocore_hpack_prim.h (shared with QPACK).

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP2

#endif // PROTOCORE_HPACK_H
