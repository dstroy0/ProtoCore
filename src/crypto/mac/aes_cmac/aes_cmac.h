// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes_cmac.h
 * @brief AES-128-CMAC (RFC 4493 / NIST SP800-38B) - one-shot MAC.
 *
 * The CMAC construction over the AES-128 block cipher: derive two subkeys K1/K2 from
 * AES-128(key, 0^128), CBC-MAC the message, and XOR the final block with K1 (message a whole
 * number of blocks) or the 10*-padded last block with K2. SMB 3.x uses it as the message-signing
 * MAC when the negotiated signing algorithm is AES-CMAC (MS-SMB2 §3.1.4.1, dialects 3.0 / 3.0.2 /
 * 3.1.1); it is a general primitive, not SMB-specific.
 *
 * The entry below is one surface over both arms: a part with an AES peripheral encrypts a block on
 * it, a part without runs the FIPS 197 rounds.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AES_CMAC_H
#define PROTOCORE_AES_CMAC_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_AES_CMAC

PROTOCORE_BEGIN_DECLS

/** @brief AES-128-CMAC output length (one AES block). */
#define PROTOCORE_AES_CMAC_LEN 16

// PROTOCORE_AES_CMAC_BORROW - the bytes a MAC runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key and message a MAC is taken over. */
typedef struct
{
    const uint8_t *key; ///< the 16-byte AES-128 key
    const uint8_t *msg; ///< the message; null iff msg_len is 0
    size_t msg_len;     ///< message length in bytes; 0 is the empty-message CMAC
    uint8_t *out;       ///< PROTOCORE_AES_CMAC_LEN bytes
} AesCmacMacArgs;

/**
 * @brief AES-128-CMAC (RFC 4493).
 *
 * A caller sets the members the call takes, invokes it through ::AesCmac with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 *   AesCmac.mac_args.key = key;
 *   AesCmac.mac_args.msg = msg;
 *   AesCmac.mac_args.msg_len = msg_len;
 *   AesCmac.mac_args.out = tag;
 *   AesCmac.mac(work);
 *
 * @var AesCmacNs::mac_args  the key and message a MAC is taken over
 * @var AesCmacNs::ok        the call's true/false outcome
 * @var AesCmacNs::mac       derive K1/K2, CBC-MAC the message, write the 16 bytes out
 *
 * @c work is PROTOCORE_AES_CMAC_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. The borrow carries the round-key schedule and both subkeys, so two MACs are two borrows and
 * never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref AesCmacNs::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    AesCmacMacArgs mac_args;
    proto_bool ok;
} AesCmacVars;

/** @brief The operands and the outcome. */
extern AesCmacVars AesCmacV;

/** @brief The entries. */
typedef struct
{
    void (*const mac)(uint8_t *restrict work);
} AesCmacNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in AesCmacV or a region of the borrow at a fixed offset.
void protocore_aes_cmac_mac(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `AesCmac.mac(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const AesCmacNs AesCmac __attribute__((unused)) = {
    .mac = protocore_aes_cmac_mac,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AES_CMAC

#endif // PROTOCORE_AES_CMAC_H
