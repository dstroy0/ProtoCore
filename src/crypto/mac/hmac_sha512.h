// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha512.h
 * @brief HMAC-SHA2-512 (RFC 2104 + FIPS 198-1) - streaming context and one-shot API.
 *
 * The shared HMAC-SHA512 primitive. Backs the SSH hmac-sha2-512 / hmac-sha2-512-etm@openssh.com
 * integrity algorithms. Built over the @ref Sha512Ns entries, so which arm compresses the inner hash
 * is not visible here. SSH-derived MAC keys are 64 bytes (<= the 128-byte block), so the key is
 * zero-padded, not pre-hashed.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-512.
 *
 * SECURITY NOTE - a MAC must be verified before the covered plaintext is acted upon; that ordering
 * guarantee lives in each protocol's packet layer, not here. These functions are pure crypto.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HMAC_SHA512_H
#define PROTOCORE_HMAC_SHA512_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_HMAC_SHA512

PROTOCORE_BEGIN_DECLS

/** @brief HMAC-SHA2-512 output length in bytes. */
#define PROTOCORE_HMAC_SHA512_LEN 64

// PROTOCORE_HMAC_SHA512_BORROW - the bytes a MAC runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key a MAC is taken with. */
typedef struct
{
    const uint8_t *key; ///< MAC key bytes
    size_t key_len;     ///< key length; > 128 is pre-hashed (RFC 2104), shorter is zero-padded to the block
} HmacSha512KeyArgs;

/** @brief One chunk fed to a running MAC. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} HmacSha512UpdateArgs;

/** @brief Where the finished MAC lands. */
typedef struct
{
    uint8_t *out; ///< PROTOCORE_HMAC_SHA512_LEN bytes
} HmacSha512FinalArgs;

/** @brief The key and message a one-shot MAC is taken over. */
typedef struct
{
    const uint8_t *key;  ///< MAC key bytes
    size_t key_len;      ///< key length
    const uint8_t *data; ///< the message
    size_t len;          ///< its length
    uint8_t *out;        ///< PROTOCORE_HMAC_SHA512_LEN bytes
} HmacSha512MacArgs;

/**
 * @brief HMAC-SHA2-512 (RFC 2104).
 *
 * A caller sets the members a call takes, invokes it through ::HmacSha512 with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 * For a MAC assembled from separate pieces - the SSH packet MAC over
 * (uint32_be(seq_num) || plaintext_packet):
 *
 *   HmacSha512.key_args.key = key;
 *   HmacSha512.key_args.key_len = key_len;
 *   HmacSha512.init(work);
 *   HmacSha512.update_args.data = seq_bytes;
 *   HmacSha512.update_args.len = 4;
 *   HmacSha512.update(work);
 *   HmacSha512.final_args.out = mac_out;
 *   HmacSha512.final(work);
 *
 * @var HmacSha512Ns::key_args      the key a MAC is taken with
 * @var HmacSha512Ns::update_args   one chunk fed to a running MAC
 * @var HmacSha512Ns::final_args    where the finished MAC lands
 * @var HmacSha512Ns::mac_args      the key and message a one-shot MAC is taken over
 * @var HmacSha512Ns::ok            a call's true/false outcome
 * @var HmacSha512Ns::init          start a MAC under @ref HmacSha512Ns::key_args
 * @var HmacSha512Ns::update        feed the running MAC a chunk
 * @var HmacSha512Ns::final         finish, writing the 64 bytes out
 * @var HmacSha512Ns::mac           init, update and final in one call, for a message already whole
 *
 * @c work is PROTOCORE_HMAC_SHA512_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. A connection takes those bytes once for its slot and passes them on every packet.
 *
 * No storage member and no context: a caller sets operands and reads @ref HmacSha512Ns::ok, and that
 * is all the surface there is.
 */
typedef struct
{
    HmacSha512KeyArgs key_args;
    HmacSha512UpdateArgs update_args;
    HmacSha512FinalArgs final_args;
    HmacSha512MacArgs mac_args;

    proto_bool ok;

    void (*const init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const mac)(uint8_t *restrict work);
} HmacSha512Ns;

/** @brief The one symbol this module exports. */
extern HmacSha512Ns HmacSha512;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMAC_SHA512

#endif // PROTOCORE_HMAC_SHA512_H
