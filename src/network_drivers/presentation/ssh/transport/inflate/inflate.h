// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file inflate.h
 * @brief RFC 1951 inflate, as SSH negotiates it.
 */

#ifndef PROTOCORE_TRANSPORT_INFLATE_H
#define PROTOCORE_TRANSPORT_INFLATE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_ZLIB

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What init takes: z, window. */
typedef struct
{
    SshInflate *z;   ///< the decompressor to initialize
    uint8_t *window; ///< back-reference window, >= SSH_INFLATE_WINDOW bytes
} InflateInitArgs;

/** @brief What packet takes: z, src, src_len, dst, dst_cap, out_len. */
typedef struct
{
    SshInflate *z; ///< the decompressor
    const uint8_t *src;
    size_t src_len;
    uint8_t *dst;
    size_t dst_cap;
    size_t *out_len; ///< set to the decompressed length on success (may be 0 if a packet carried only flush bits)
} InflatePacketArgs;

/**
 * @brief RFC 1951 inflate, as SSH negotiates it.
 *
 * A caller sets the members a call takes, invokes it through ::Inflate with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Inflate.init_args.z = ...;
 *   Inflate.init_args.window = ...;
 *   Inflate.init(work);
 *
 * @var InflateNs::init_args  what init takes: z, window
 * @var InflateNs::packet_args  what packet takes: z, src, src_len, dst, dst_cap, out_len
 * @var InflateNs::ok  a call's true/false outcome
 * @var InflateNs::n  0 on success, -1 on a malformed stream, an output overflow, or a ...
 * @var InflateNs::init  bind a caller-owned 32 KB window to a decompressor and reset it to ...
 * @var InflateNs::packet  decompress one inbound packet payload, continuing the session's ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    InflateInitArgs init_args;
    InflatePacketArgs packet_args;
    proto_bool ok;
    int n;
} InflateVars;

/** @brief The operands and the outcome. */
extern InflateVars InflateV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const packet)(uint8_t *restrict work);
} InflateNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in InflateV or a region of the borrow at a fixed offset.
void protocore_inflate_init(uint8_t *restrict work);
void protocore_inflate_packet(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Inflate.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const InflateNs Inflate __attribute__((unused)) = {
    .init = protocore_inflate_init,
    .packet = protocore_inflate_packet,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_ZLIB

#endif // PROTOCORE_TRANSPORT_INFLATE_H
