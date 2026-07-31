// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#if PC_ENABLE_HTTP2

#include <stddef.h>
#include <stdint.h>

/** @brief One dynamic-table entry descriptor (its bytes live in the table's byte ring). */
struct HpackEntry
{
    uint16_t name_len; ///< header name length
    uint16_t val_len;  ///< header value length
    uint16_t ring_pos; ///< start of name||value in the byte ring
};

/**
 * @brief Per-connection HPACK dynamic table (the peer encoder's state, tracked by our decoder).
 * FIFO: newest entry is dynamic index 62, oldest is evicted first. Fixed storage, no heap.
 */
struct HpackDynTable
{
    uint32_t max_size; ///< negotiated maximum size in bytes (RFC 7541 sec 4.2)
    uint32_t used;     ///< current size = sum of (name_len + val_len + 32) over entries
    uint16_t ehead;    ///< descriptor ring: index one past the newest entry
    uint16_t ecount;   ///< number of live entries
    uint16_t rtail;    ///< byte ring: start of the oldest entry's bytes
    uint16_t rused;    ///< byte ring: bytes in use
    HpackEntry ent[PC_HPACK_MAX_ENTRIES];
    char ring[PC_HPACK_TABLE_BYTES];
};

/** @brief Callback invoked for each decoded header; return false to abort the decode. */
typedef bool (*HpackEmitFn)(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len);

/** @brief Initialize a dynamic table to empty, max size @p max_bytes (0 = PC_HPACK_TABLE_BYTES). */
void pc_hpack_dyn_init(HpackDynTable *t, uint32_t max_bytes);

/**
 * @brief Decode an HPACK header block, emitting each (name, value) via @p emit.
 * @param scratch     caller buffer that holds one header's name+value during each emit call.
 * @return true if the whole block decoded cleanly; false on any malformed input or overflow.
 */
bool pc_hpack_decode(HpackDynTable *t, const uint8_t *block, size_t len, char *scratch, size_t scratch_cap,
                     HpackEmitFn emit, void *ctx);

/**
 * @brief Encode one header field into @p out (server side: static-index a full or name match,
 * else literal-without-indexing; strings Huffman-coded when that is not longer).
 * @return bytes written, or 0 on overflow.
 */
size_t pc_hpack_encode_header(uint8_t *out, size_t cap, const char *name, size_t name_len, const char *value,
                              size_t value_len);

// The prefix-integer and Huffman primitives moved to pc_hpack_prim.h (shared with QPACK).

#endif // PC_ENABLE_HTTP2
#endif // PROTOCORE_HPACK_H
