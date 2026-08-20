// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha256.h
 * @brief HMAC-SHA2-256 (RFC 2104 + FIPS 198-1) - streaming context and one-shot API.
 *
 * The shared keyed-MAC primitive for the whole library: SSH binary-packet MAC (RFC 4253 §6.4), the
 * TLS 1.3 / QUIC / DTLS HKDF PRF, SNMPv3 usmHMACSHAAuthProtocol, JWT HS256, CSRF tokens, and SMB 2.x
 * message signing / the SP800-108 KDF. Built over the @ref Sha256Ns entries, so which arm compresses
 * the inner hash is not visible here.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-256.
 *
 * SECURITY NOTE - a MAC must be verified before the covered plaintext is acted upon; that ordering
 * guarantee lives in each protocol's packet layer, not here. These functions are pure crypto.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HMAC_SHA256_H
#define PROTOCORE_HMAC_SHA256_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_HMAC_SHA256

PROTOCORE_BEGIN_DECLS

/** @brief HMAC-SHA2-256 output length in bytes. */
#define PROTOCORE_HMAC_SHA256_LEN 32

// PROTOCORE_HMAC_SHA256_BORROW - the bytes a MAC runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key a MAC is taken with. */
typedef struct
{
    const uint8_t *key; ///< MAC key bytes
    size_t key_len;     ///< key length; > 64 is pre-hashed (RFC 2104), shorter is zero-padded to the block
} HmacSha256KeyArgs;
/** @brief One chunk fed to a running MAC. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} HmacSha256UpdateArgs;
/** @brief Where the finished MAC lands. */
typedef struct
{
    uint8_t *out; ///< PROTOCORE_HMAC_SHA256_LEN bytes
} HmacSha256FinalArgs;
/** @brief The key and message a one-shot MAC is taken over. */
typedef struct
{
    const uint8_t *key;  ///< MAC key bytes
    size_t key_len;      ///< key length
    const uint8_t *data; ///< the message
    size_t len;          ///< its length
    uint8_t *out;        ///< PROTOCORE_HMAC_SHA256_LEN bytes
} HmacSha256MacArgs;
/**
 * @brief HMAC-SHA2-256 (RFC 2104).
 *
 * A caller sets the members a call takes, invokes it through ::HmacSha256 with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 * For a MAC assembled from separate pieces - the SSH packet MAC over
 * (uint32_be(seq_num) || plaintext_packet):
 *
 *   HmacSha256.key_args.key = key;
 *   HmacSha256.key_args.key_len = key_len;
 *   HmacSha256.init(work);
 *   HmacSha256.update_args.data = seq_bytes;
 *   HmacSha256.update_args.len = 4;
 *   HmacSha256.update(work);
 *   HmacSha256.final_args.out = mac_out;
 *   HmacSha256.final(work);
 *
 * @var HmacSha256Ns::key_args      the key a MAC is taken with
 * @var HmacSha256Ns::update_args   one chunk fed to a running MAC
 * @var HmacSha256Ns::final_args    where the finished MAC lands
 * @var HmacSha256Ns::mac_args      the key and message a one-shot MAC is taken over
 * @var HmacSha256Ns::ok            a call's true/false outcome
 * @var HmacSha256Ns::init          start a MAC under @ref HmacSha256Ns::key_args
 * @var HmacSha256Ns::update        feed the running MAC a chunk
 * @var HmacSha256Ns::final         finish, writing the 32 bytes out
 * @var HmacSha256Ns::mac           init, update and final in one call, for a message already whole
 *
 * @c work is PROTOCORE_HMAC_SHA256_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. A connection takes those bytes once for its slot and passes them on every packet.
 *
 * No storage member and no context: a caller sets operands and reads @ref HmacSha256Ns::ok, and that
 * is all the surface there is.
 */
typedef struct
{
    HmacSha256KeyArgs key_args;
    HmacSha256UpdateArgs update_args;
    HmacSha256FinalArgs final_args;
    HmacSha256MacArgs mac_args;
    proto_bool ok;
} HmacSha256Vars;

/** @brief The operands and the outcome. */
extern HmacSha256Vars HmacSha256V;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const mac)(uint8_t *restrict work);
} HmacSha256Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HmacSha256V or a region of the borrow at a fixed offset.
void protocore_hmac_sha256_init(uint8_t *restrict work);
void protocore_hmac_sha256_update(uint8_t *restrict work);
void protocore_hmac_sha256_final(uint8_t *restrict work);
void protocore_hmac_sha256_mac(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HmacSha256.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HmacSha256Ns HmacSha256 __attribute__((unused)) = {
    .init = protocore_hmac_sha256_init,
    .update = protocore_hmac_sha256_update,
    .final = protocore_hmac_sha256_final,
    .mac = protocore_hmac_sha256_mac,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMAC_SHA256

#endif // PROTOCORE_HMAC_SHA256_H
