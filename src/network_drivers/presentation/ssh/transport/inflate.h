// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file inflate.h
 * @brief RFC 1951 inflate, as SSH negotiates it.
 */

#ifndef PROTOCORE_TRANSPORT_INFLATE_H
#define PROTOCORE_TRANSPORT_INFLATE_H

#include "network_drivers/presentation/ssh/common.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_SSH_ZLIB

/** @brief Sliding-window bytes the inflate needs (the full zlib 32 KB window OpenSSH may reference). */
#define SSH_INFLATE_WINDOW 32768u

/** @brief Bytes of un-decoded input the engine carries between packets (the flush-block tail). A
 *  well-behaved peer leaves only a handful; the bound also caps a peer that fails to flush cleanly. */
#define SSH_INFLATE_CARRY 64u

/**
 * @brief Streaming client-to-server DEFLATE decompressor (one per SSH connection).
 *
 * The 32 KB circular @ref window is caller-supplied (it lives in PSRAM alongside the s2c compressor).
 * ssh_inflate_init() binds it and resets the stream; the small carry/bit state is inline.
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
 * @brief Bind a caller-owned 32 KB window to a decompressor and reset it to stream start.
 * @param z       the decompressor to initialize.
 * @param window  back-reference window, >= SSH_INFLATE_WINDOW bytes.
 */
void ssh_inflate_init(SshInflate *z, uint8_t *window);

/**
 * @brief Decompress one inbound packet payload, continuing the session's zlib stream.
 *
 * Consumes the 2-byte zlib header on the first call, then decodes every complete DEFLATE block that
 * @p src (prefixed by any carried tail) makes available, writing the decompressed bytes to @p dst and
 * into the window. The incomplete trailing flush block is carried to the next call.
 *
 * @param z            the decompressor.
 * @param src,src_len  one inbound compressed payload.
 * @param dst,dst_cap  output buffer for the decompressed payload.
 * @param out_len      set to the decompressed length on success (may be 0 if a packet carried only flush bits).
 * @return 0 on success, -1 on a malformed stream, an output overflow, or a carry overflow (peer did not flush).
 */
int ssh_inflate_packet(SshInflate *z, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                       size_t *out_len);

#endif // PROTOCORE_ENABLE_SSH_ZLIB

PROTOCORE_END_DECLS

#endif // PROTOCORE_TRANSPORT_INFLATE_H
