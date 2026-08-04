// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_zlib.h
 * @brief SSH server-to-client compression: a context-takeover DEFLATE stream (no heap).
 *
 * SSH `zlib` / `zlib@openssh.com` (RFC 4253 sec 6.2) compress the packet payload with a single zlib
 * stream per direction that is kept alive for the whole session - a persistent sliding window carried
 * across packets ("context takeover"), sync-flushed at each packet boundary. This is the OPPOSITE of
 * the WebSocket permessage-deflate codec (presentation/deflate), which is stateless per message
 * (`no_context_takeover`); the two cannot share an instance.
 *
 * This file is the SERVER->CLIENT (deflate) half. It emits a zlib stream (RFC 1950 2-byte header once,
 * then RFC 1951 fixed-Huffman blocks) with a Z_SYNC_FLUSH boundary after every packet: the block is
 * byte-aligned and the empty stored block `00 00 ff ff` is KEPT on the wire (unlike permessage-deflate,
 * which strips it). A standard zlib `inflate()` - as in OpenSSH - decodes it. The CLIENT->SERVER
 * (inflate) half lives in ssh_inflate.h: OpenSSH compresses its outbound with Z_PARTIAL_FLUSH, so that
 * side is a resumable, context-takeover inflate carrying bit state + the window across packets.
 *
 * All state and buffers are caller-supplied; the codec allocates nothing. See ssh_zlib.cpp for the
 * work-buffer / hash-table sizing helpers (SSH_ZLIB_* macros).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_ZLIB_H
#define PROTOCORE_SSH_ZLIB_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SSH_ZLIB

/** @brief Hash-table buckets for the LZ77 3-byte match search (2^bits). */
#define SSH_ZLIB_HASH_BITS 13
#define SSH_ZLIB_HASH_SIZE (1 << SSH_ZLIB_HASH_BITS)

/** @brief Work buffer capacity the compressor needs: window history + one input payload. */
#define SSH_ZLIB_WORK_SIZE ((size_t)PC_SSH_ZLIB_WINDOW + (size_t)PC_SSH_ZLIB_MAX_IN)

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
 * @brief Bind caller memory to a compressor and reset it to stream start.
 *
 * @param z        the compressor to initialize.
 * @param work     work buffer, >= SSH_ZLIB_WORK_SIZE bytes.
 * @param head     hash heads, SSH_ZLIB_HASH_SIZE uint16 entries.
 * @param prev     hash chain, SSH_ZLIB_WORK_SIZE uint16 entries.
 * @param ll_code,ll_len,d_code,d_len  fixed-Huffman tables (288/288/30/30 entries); seeded here.
 */
void ssh_deflate_init(SshDeflate *z, uint8_t *work, uint16_t *head, uint16_t *prev, uint16_t *ll_code, uint8_t *ll_len,
                      uint16_t *d_code, uint8_t *d_len);

/**
 * @brief Compress one packet payload, continuing the session's zlib stream.
 *
 * Emits the 2-byte zlib header on the first call, then a fixed-Huffman block for @p src followed by a
 * Z_SYNC_FLUSH boundary (`00 00 ff ff`, kept on the wire). Back-references may reach into the
 * persistent window (prior packets), then the window slides to keep the last PC_SSH_ZLIB_WINDOW
 * bytes for the next call.
 *
 * @param z            the compressor.
 * @param src,src_len  uncompressed payload (src_len <= PC_SSH_ZLIB_MAX_IN).
 * @param dst,dst_cap  output buffer for the on-wire compressed payload.
 * @param out_len      set to the compressed length on success.
 * @return 0 on success, -1 on bad input length or output overflow.
 */
int ssh_deflate_packet(SshDeflate *z, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                       size_t *out_len);

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

#endif // PC_ENABLE_SSH_ZLIB

PROTO_END_DECLS

#endif // PROTOCORE_SSH_ZLIB_H
