// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes256ctr.h
 * @brief AES-256-CTR stream cipher (aes256-ctr, RFC 4344 §4).
 *
 * The mandatory cipher for this SSH implementation. CTR mode turns AES into a stream cipher: each
 * 16-byte counter block is AES-ECB encrypted into a keystream block and the data is XOR'd with it, so
 * encrypt and decrypt are the identical operation. The entries below are one surface over both arms:
 * a part with an AES peripheral runs the block on it, a part without runs the FIPS 197 rounds.
 *
 * The two things that persist across packets are the caller's: the 32-byte key and the 16-byte
 * counter, passed in on every call. The counter advances in place by ceil(len / 16) blocks, so
 * successive calls continue the same stream.
 *
 * COUNTER FORMAT (RFC 4344 §4)
 * The 16-byte counter increments as a big-endian 128-bit integer after each 16-byte keystream block.
 * The initial counter is the IV from the key exchange (RFC 4253 §7.2, labels 'A'/'B').
 *
 * @note The SSH binary packet is always a whole number of cipher blocks, so every call is block-aligned
 *       and the counter alone is sufficient state. A non-block-aligned length is permitted only as the
 *       final call of a stream (any leftover keystream in the last block is discarded, not carried).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AES256CTR_H
#define PROTOCORE_AES256CTR_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_AES256CTR

#include <stddef.h>
#include <stdint.h>

PROTOCORE_BEGIN_DECLS

/** @brief AES-256-CTR key length (bytes). */
#define PROTOCORE_AES256CTR_KEY_LEN 32
/** @brief AES-256-CTR counter/IV block length (bytes). */
#define PROTOCORE_AES256CTR_CTR_LEN 16

// PROTOCORE_AES256CTR_BORROW - the bytes a cipher call runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key, the counter and the buffers one CTR call runs over. */
typedef struct
{
    const uint8_t *key; ///< PROTOCORE_AES256CTR_KEY_LEN bytes
    uint8_t *counter;   ///< PROTOCORE_AES256CTR_CTR_LEN bytes, big-endian, advanced in place
    const uint8_t *in;  ///< the input bytes
    uint8_t *out;       ///< where they land; may equal @c in
    size_t len;         ///< how many
} Aes256CtrCryptArgs;

/** @brief The key, the counter and the four encrypted length bytes a peek reads. */
typedef struct
{
    const uint8_t *key;     ///< PROTOCORE_AES256CTR_KEY_LEN bytes
    const uint8_t *counter; ///< PROTOCORE_AES256CTR_CTR_LEN bytes, read and not advanced
    const uint8_t *enc4;    ///< the 4 encrypted length bytes at the start of the packet
} Aes256CtrGetLengthArgs;

/**
 * @brief AES-256-CTR (RFC 4344 §4).
 *
 * A caller sets the members a call takes, invokes it through ::Aes256Ctr with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 *   Aes256Ctr.crypt_args.key = key;
 *   Aes256Ctr.crypt_args.counter = ctr;
 *   Aes256Ctr.crypt_args.in = pkt;
 *   Aes256Ctr.crypt_args.out = pkt;
 *   Aes256Ctr.crypt_args.len = pkt_len;
 *   Aes256Ctr.crypt(work);
 *
 * @var Aes256CtrNs::crypt_args       the key, the counter and the buffers one CTR call runs over
 * @var Aes256CtrNs::get_length_args  the key, the counter and the four encrypted length bytes a peek reads
 * @var Aes256CtrNs::ok               a call's true/false outcome
 * @var Aes256CtrNs::length           the SSH packet_length the last peek recovered
 * @var Aes256CtrNs::crypt            XOR the CTR keystream over the buffer, advancing the counter
 * @var Aes256CtrNs::get_length       decrypt the 4-byte packet_length prefix without advancing the counter
 *
 * @ref Aes256CtrNs::crypt encrypts and decrypts with the same body, and @c crypt_args.in and
 * @c crypt_args.out may alias, which is the in-place form the SSH packet layer uses.
 *
 * @ref Aes256CtrNs::get_length leaves @c get_length_args.counter where it was, so a receiver learns a
 * packet's length - and thus how many bytes to wait for - before the whole packet has arrived, without
 * consuming counter state.
 *
 * @c work is PROTOCORE_AES256CTR_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. The expanded key schedule lives in those bytes and nowhere else, so it never reaches BSS or the
 * stack. Two ciphers running at once are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref Aes256CtrNs::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Aes256CtrCryptArgs crypt_args;
    Aes256CtrGetLengthArgs get_length_args;

    proto_bool ok;
    uint32_t length;

    void (*const crypt)(uint8_t *restrict work);
    void (*const get_length)(uint8_t *restrict work);
} Aes256CtrNs;

/** @brief The one symbol this module exports. */
extern Aes256CtrNs Aes256Ctr;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AES256CTR

#endif // PROTOCORE_AES256CTR_H
