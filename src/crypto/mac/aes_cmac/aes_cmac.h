// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_AES_CMAC_H
#define PROTOCORE_AES_CMAC_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is PROTOCORE_AES_CMAC_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. The borrow carries the round-key schedule and both subkeys, so two MACs are two borrows and
 * never collide.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief AES-128-CMAC output length (one AES block). */
#define PROTOCORE_AES_CMAC_LEN 16

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*mac)(uint8_t *restrict, const uint8_t *, const uint8_t *, size_t, uint8_t *);
} AesCmacNs;
PROTOCORE_NS_LAYOUT(AesCmacNs, mac);

/**
 * @brief Derive K1/K2, CBC-MAC the message, write the 16 bytes out.
 * @param work PROTOCORE_AES_CMAC_BORROW bytes the caller took. Not held past the call.
 * @param key the 16-byte AES-128 key
 * @param msg the message; null iff msg_len is 0
 * @param msg_len message length in bytes; 0 is the empty-message CMAC
 * @param out PROTOCORE_AES_CMAC_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_aes_cmac_mac(uint8_t *restrict work, const uint8_t *key, const uint8_t *msg, size_t msg_len,
                                  uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS AesCmacNs AesCmac PROTOCORE_UNUSED = {.mac = protocore_aes_cmac_mac};

PROTOCORE_END_DECLS

#endif // PROTOCORE_AES_CMAC_H
