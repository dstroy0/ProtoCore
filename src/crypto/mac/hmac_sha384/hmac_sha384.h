// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmac_sha384.h
 * @brief HMAC-SHA2-384 (RFC 2104 + FIPS 198-1) - streaming context and one-shot API.
 *
 * The shared HMAC-SHA384 primitive. Backs the HKDF the TLS 1.3 SHA-384 cipher suites run their key
 * schedule on, and the Finished MAC under them. Built over the @ref Sha384Ns entries, so which arm
 * compresses the inner hash is not visible here. TLS 1.3 keys a Finished MAC with a 48-byte secret
 * (<= the 128-byte block), so the key is zero-padded, not pre-hashed.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-384. The block
 * is SHA-512's 128 octets, not the 48-octet digest, so the pads are 128 wide (RFC 4231 sec 2 tabulates
 * the published HMAC-SHA-384 vectors on that block).
 *
 * SECURITY NOTE - a MAC must be verified before the covered plaintext is acted upon; that ordering
 * guarantee lives in each protocol's packet layer, not here. These functions are pure crypto.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HMAC_SHA384_H
#define PROTOCORE_HMAC_SHA384_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_HMAC_SHA384

PROTOCORE_BEGIN_DECLS

/** @brief HMAC-SHA2-384 output length in bytes. */
#define PROTOCORE_HMAC_SHA384_LEN 48

// PROTOCORE_HMAC_SHA384_BORROW - the bytes a MAC runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key a MAC is taken with. */
typedef struct
{
    const uint8_t *key; ///< MAC key bytes
    size_t key_len;     ///< key length; > 128 is pre-hashed (RFC 2104), shorter is zero-padded to the block
} HmacSha384KeyArgs;

/** @brief One chunk fed to a running MAC. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} HmacSha384UpdateArgs;

/** @brief Where the finished MAC lands. */
typedef struct
{
    uint8_t *out; ///< PROTOCORE_HMAC_SHA384_LEN bytes
} HmacSha384FinalArgs;

/** @brief The key and message a one-shot MAC is taken over. */
typedef struct
{
    const uint8_t *key;  ///< MAC key bytes
    size_t key_len;      ///< key length
    const uint8_t *data; ///< the message
    size_t len;          ///< its length
    uint8_t *out;        ///< PROTOCORE_HMAC_SHA384_LEN bytes
} HmacSha384MacArgs;

/**
 * @brief HMAC-SHA2-384 (RFC 2104).
 *
 * A caller sets the members a call takes, invokes it through ::HmacSha384 with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 * For a MAC assembled from separate pieces - an HKDF-Expand block over (T(i-1) || info || i):
 *
 *   HmacSha384.key_args.key = prk;
 *   HmacSha384.key_args.key_len = prk_len;
 *   HmacSha384.init(work);
 *   HmacSha384.update_args.data = info;
 *   HmacSha384.update_args.len = info_len;
 *   HmacSha384.update(work);
 *   HmacSha384.final_args.out = block;
 *   HmacSha384.final(work);
 *
 * @var HmacSha384Ns::key_args      the key a MAC is taken with
 * @var HmacSha384Ns::update_args   one chunk fed to a running MAC
 * @var HmacSha384Ns::final_args    where the finished MAC lands
 * @var HmacSha384Ns::mac_args      the key and message a one-shot MAC is taken over
 * @var HmacSha384Ns::ok            a call's true/false outcome
 * @var HmacSha384Ns::init          start a MAC under @ref HmacSha384Ns::key_args
 * @var HmacSha384Ns::update        feed the running MAC a chunk
 * @var HmacSha384Ns::final         finish, writing the 48 bytes out
 * @var HmacSha384Ns::mac           init, update and final in one call, for a message already whole
 *
 * @c work is PROTOCORE_HMAC_SHA384_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. A connection takes those bytes once for its slot and passes them on every record.
 *
 * No storage member and no context: a caller sets operands and reads @ref HmacSha384Ns::ok, and that
 * is all the surface there is.
 */
typedef struct
{
    HmacSha384KeyArgs key_args;
    HmacSha384UpdateArgs update_args;
    HmacSha384FinalArgs final_args;
    HmacSha384MacArgs mac_args;
    proto_bool ok;
} HmacSha384Vars;

/** @brief The operands and the outcome. */
extern HmacSha384Vars HmacSha384V;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const mac)(uint8_t *restrict work);
} HmacSha384Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HmacSha384V or a region of the borrow at a fixed offset.
void protocore_hmac_sha384_init(uint8_t *restrict work);
void protocore_hmac_sha384_update(uint8_t *restrict work);
void protocore_hmac_sha384_final(uint8_t *restrict work);
void protocore_hmac_sha384_mac(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HmacSha384.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HmacSha384Ns HmacSha384 __attribute__((unused)) = {
    .init = protocore_hmac_sha384_init,
    .update = protocore_hmac_sha384_update,
    .final = protocore_hmac_sha384_final,
    .mac = protocore_hmac_sha384_mac,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMAC_SHA384

#endif // PROTOCORE_HMAC_SHA384_H
