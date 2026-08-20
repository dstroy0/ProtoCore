// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file kdf.h
 * @brief SP800-108 counter-mode key derivation (HMAC-SHA256 PRF).
 *
 * The shared NIST SP800-108 §5.1 counter-mode KDF. SMB 3.x uses it to derive its signing and
 * encryption keys (MS-SMB2 §3.1.4.2); the caller assembles the fixed input, keeping this independent
 * of any protocol's label/context choices. Verified against the NIST CAVP KBKDF (KDFCTR) vectors.
 * Built over the @ref HmacSha256Ns entries, so which arm compresses the PRF is not visible here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_KDF_H
#define PROTOCORE_KDF_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool and the widths

#if PROTOCORE_ENABLE_KDF

PROTOCORE_BEGIN_DECLS

// PROTOCORE_KDF_BORROW - the bytes a derivation runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key, the fixed input, and where the derived bytes land. */
typedef struct
{
    const uint8_t *ki;    ///< the key-derivation key (e.g. the SMB 3.x session key)
    size_t ki_len;        ///< its length in bytes
    const uint8_t *fixed; ///< the fixed input, `Label || 0x00 || Context || [L]`
    size_t fixed_len;     ///< its length in bytes
    uint8_t *out;         ///< receives out_len derived bytes
    size_t out_len;       ///< number of output bytes, >= 1; the caller encodes L = out_len * 8 into fixed
} KdfCtrArgs;

/**
 * @brief SP800-108 KDF in counter mode with HMAC-SHA256 as the PRF (NIST SP800-108 §5.1; r = 32-bit
 *        counter placed before the fixed input).
 *
 * K(i) = HMAC-SHA256(Ki, [i]_32be || fixed); the blocks are concatenated for i = 1, 2, ... and the
 * result truncated to @ref KdfCtrArgs::out_len bytes.
 *
 * A caller sets the members the call takes, invokes it through ::Kdf with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Kdf.ctr_args.ki = session_key;
 *   Kdf.ctr_args.ki_len = 16;
 *   Kdf.ctr_args.fixed = fixed;
 *   Kdf.ctr_args.fixed_len = n;
 *   Kdf.ctr_args.out = out_key;
 *   Kdf.ctr_args.out_len = 16;
 *   Kdf.ctr_hmac_sha256(work);
 *
 * @var KdfNs::ctr_args         the key, the fixed input, and where the derived bytes land
 * @var KdfNs::ok               a call's true/false outcome; false on a null pointer or out_len == 0
 * @var KdfNs::ctr_hmac_sha256  derive out_len bytes, counter mode, HMAC-SHA256 PRF
 *
 * @c work is PROTOCORE_KDF_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That
 * is what keeps K(i), which is derived from Ki, from outliving the caller.
 *
 * No storage member and no context: a caller sets operands and reads @ref KdfNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    KdfCtrArgs ctr_args;
    proto_bool ok;
} KdfVars;

/** @brief The operands and the outcome. */
extern KdfVars KdfV;

/** @brief The entries. */
typedef struct
{
    void (*const ctr_hmac_sha256)(uint8_t *restrict work);
} KdfNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in KdfV or a region of the borrow at a fixed offset.
void protocore_kdf_ctr_hmac_sha256(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Kdf.ctr_hmac_sha256(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const KdfNs Kdf __attribute__((unused)) = {
    .ctr_hmac_sha256 = protocore_kdf_ctr_hmac_sha256,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_KDF

#endif // PROTOCORE_KDF_H
