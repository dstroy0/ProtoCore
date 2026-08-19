// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zlib.h
 * @brief RFC 1950 / 1951 deflate, as SSH negotiates it.
 */

#ifndef PROTOCORE_TRANSPORT_ZLIB_H
#define PROTOCORE_TRANSPORT_ZLIB_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_ZLIB

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Hash-table buckets for the LZ77 3-byte match search (2^bits). */
#define SSH_ZLIB_HASH_BITS 13
#define SSH_ZLIB_HASH_SIZE (1 << SSH_ZLIB_HASH_BITS)

/** @brief Work buffer capacity the compressor needs: window history + one input payload. */
#define SSH_ZLIB_WORK_SIZE ((size_t)PROTOCORE_SSH_ZLIB_WINDOW + (size_t)PROTOCORE_SSH_ZLIB_MAX_IN)

/**
 * @brief Streaming server-to-client DEFLATE compressor (one per SSH connection).
 *
 * The window (history) lives at the front of @ref work; @ref hist bytes are valid. Hash chains
 * (@ref head / @ref prev) are rebuilt over the history each packet, so a slid buffer needs no chain
 * fix-up. All pointers are caller-owned; ssh_deflate_init() wires them and seeds the fixed tables.
 */
typedef struct
{
    uint8_t *work;          ///< history+input work buffer, capacity SSH_ZLIB_WORK_SIZE.
    uint16_t *head;         ///< hash bucket heads, SSH_ZLIB_HASH_SIZE entries.
    uint16_t *prev;         ///< hash chain (absolute-position indexed), SSH_ZLIB_WORK_SIZE entries.
    uint16_t *ll_code;      ///< fixed literal/length Huffman codes (bit-reversed), 288 entries.
    uint8_t *ll_len;        ///< their bit lengths, 288 entries.
    uint16_t *d_code;       ///< fixed distance Huffman codes (bit-reversed), 30 entries.
    uint8_t *d_len;         ///< their bit lengths, 30 entries.
    size_t hist;            ///< bytes of history currently at the front of @ref work.
    proto_bool header_sent; ///< true once the leading 2-byte zlib header has been emitted.
} SshDeflate;

/**
 * @brief Worst-case compressed size for @p src_len input (header + block overhead + sync marker).
 *
 * Callers size @p dst with this. Fixed-Huffman can expand incompressible data slightly; the bound
 * covers the 2-byte header, per-byte worst case, end-of-block, and the 4-byte sync marker.
 */
static inline size_t ssh_deflate_bound(size_t src_len)
{
    return 2 + src_len + (src_len >> 3) + 32;
}

/** @brief What init takes: z, win, head, prev, ll_code, ll_len, ... */
typedef struct
{
    SshDeflate *z;  ///< the compressor to initialize
    uint8_t *win;   ///< history+input buffer, >= SSH_ZLIB_WORK_SIZE bytes
    uint16_t *head; ///< hash heads, SSH_ZLIB_HASH_SIZE uint16 entries
    uint16_t *prev; ///< hash chain, SSH_ZLIB_WORK_SIZE uint16 entries
    uint16_t *ll_code;
    uint8_t *ll_len;
    uint16_t *d_code;
    uint8_t *d_len;
} ZlibInitArgs;

/** @brief What packet takes: z, src, src_len, dst, dst_cap, out_len. */
typedef struct
{
    SshDeflate *z; ///< the compressor
    const uint8_t *src;
    size_t src_len;
    uint8_t *dst;
    size_t dst_cap;
    size_t *out_len; ///< set to the compressed length on success
} ZlibPacketArgs;

/**
 * @brief RFC 1950 / 1951 deflate, as SSH negotiates it.
 *
 * A caller sets the members a call takes, invokes it through ::Zlib with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Zlib.init_args.z = ...;
 *   Zlib.init_args.win = ...;
 *   Zlib.init_args.head = ...;
 *   Zlib.init_args.prev = ...;
 *   Zlib.init_args.ll_code = ...;
 *   Zlib.init_args.ll_len = ...;
 *   Zlib.init_args.d_code = ...;
 *   Zlib.init_args.d_len = ...;
 *   Zlib.init(work);
 *
 * @var ZlibNs::init_args  what init takes: z, win, head, prev, ll_code, ll_len,
 * @var ZlibNs::packet_args  what packet takes: z, src, src_len, dst, dst_cap, out_len
 * @var ZlibNs::ok  a call's true/false outcome
 * @var ZlibNs::n  0 on success, -1 on bad input length or output overflow
 * @var ZlibNs::init  bind caller memory to a compressor and reset it to stream start
 * @var ZlibNs::packet  compress one packet payload, continuing the session's zlib stream. ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ZlibInitArgs init_args;
    ZlibPacketArgs packet_args;

    proto_bool ok;
    int n;

    void (*const init)(uint8_t *restrict work);
    void (*const packet)(uint8_t *restrict work);
} ZlibNs;

/** @brief The one symbol this module exports. */
extern ZlibNs Zlib;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_ZLIB

#endif // PROTOCORE_TRANSPORT_ZLIB_H
