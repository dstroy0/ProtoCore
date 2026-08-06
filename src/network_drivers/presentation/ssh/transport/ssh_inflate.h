// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_inflate.h
 * @brief SSH client-to-server decompression: a resumable, context-takeover INFLATE (no heap).
 *
 * The complement of ssh_zlib (server-to-client DEFLATE). SSH `zlib` / `zlib@openssh.com`
 * (RFC 4253 sec 6.2) keep one zlib stream alive per direction for the whole session - a persistent
 * 32 KB sliding window carried across packets ("context takeover"), flushed at each packet boundary.
 * OpenSSH compresses its outbound (our inbound, client-to-server) traffic with `Z_PARTIAL_FLUSH`,
 * which ends each packet at a DEFLATE block boundary but NOT on a byte boundary: the last bits of a
 * packet spill into the next packet's first byte. Decoding it therefore needs a *resumable* inflate
 * that carries the bit position and the window across `SshInflater.packet()` calls.
 *
 * The engine keeps that state small by only ever decoding *complete* DEFLATE blocks: after each feed
 * it retains the few un-decoded tail bytes (the incomplete flush block) plus the bit offset into the
 * first of them, and re-decodes from that boundary when the next packet arrives - so there is no
 * mid-block Huffman-table or symbol state to persist. Back-references read from a caller-supplied
 * 32768-byte circular window (SSH_INFLATE_WINDOW), which holds up to 32 KB of prior output so a match
 * can reach into earlier packets. All state and buffers are caller-owned; the codec allocates nothing.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_INFLATE_H
#define PROTOCORE_SSH_INFLATE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SSH_ZLIB

/** @brief Sliding-window bytes the inflate needs (the full zlib 32 KB window OpenSSH may reference). */
#define SSH_INFLATE_WINDOW 32768u

/** @brief Bytes of un-decoded input the engine carries between packets (the flush-block tail). A
 *  well-behaved peer leaves only a handful; the bound also caps a peer that fails to flush cleanly. */
#define SSH_INFLATE_CARRY 64u

/**
 * @brief Streaming client-to-server DEFLATE decompressor (one per SSH connection).
 *
 * The 32 KB circular @ref window is caller-supplied (it lives in PSRAM alongside the s2c compressor).
 * SshInflater.init() binds it and resets the stream; the small carry/bit state is inline.
 */
typedef struct
{
    uint8_t *window;                  ///< 32 KB circular back-reference window (SSH_INFLATE_WINDOW bytes).
    uint32_t wpos;                    ///< next write position in @ref window (0..SSH_INFLATE_WINDOW-1).
    uint32_t whist;                   ///< bytes of valid history in @ref window (caps at SSH_INFLATE_WINDOW).
    uint8_t carry[SSH_INFLATE_CARRY]; ///< un-decoded tail bytes from the previous packet (flush block).
    uint8_t carry_len;                ///< number of valid bytes in @ref carry.
    uint8_t bit_off;                  ///< bits already consumed from carry[0] at the last block boundary (0..7).
    proto_bool header_seen;           ///< true once the leading 2-byte RFC 1950 zlib header was consumed.
} SshInflate;

/**
 * @brief The receive half of zlib packet compression. SshInflate is the stream state; this runs it.
 *
 * @var SshInflateNs::init    Bind a caller-owned 32 KB window to a decompressor and reset it to stream start
 * @var SshInflateNs::packet  Decompress one inbound packet payload, continuing the session's zlib stream
 */
typedef struct
{
    void (*init)(SshInflate *z, uint8_t *window);
    int (*packet)(SshInflate *z, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len);
} SshInflateNs;

/** @brief The one symbol this module exports. */
extern const SshInflateNs SshInflater;

#endif // PC_ENABLE_SSH_ZLIB

PROTO_END_DECLS

#endif // PROTOCORE_SSH_INFLATE_H
