// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes_cmac.h
 * @brief AES-128-CMAC (RFC 4493 / NIST SP800-38B) one-shot MAC.
 *
 * The CMAC construction over the AES-128 block cipher: derive two subkeys K1/K2 from
 * AES-128(key, 0^128), CBC-MAC the message, and XOR the final block with K1 (message a whole
 * number of blocks) or the 10*-padded last block with K2. SMB 3.x uses it as the message-signing
 * MAC when the negotiated signing algorithm is AES-CMAC (MS-SMB2 §3.1.4.1, dialects 3.0 / 3.0.2 /
 * 3.1.1); it is a general primitive, not SMB-specific.
 *
 * On ESP32/Arduino the AES-128 block runs on the mbedTLS context (hardware AES accelerator); on the
 * native host build it uses the shared table-free software AES (crypto/cipher/aes_block.h). Pure,
 * zero heap. Verified against the RFC 4493 §4 worked examples.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AES_CMAC_H
#define PROTOCORE_AES_CMAC_H

#include <stddef.h>
#include <stdint.h>

/** @brief AES-128-CMAC output length (one AES block). */
#define PC_AES_CMAC_LEN 16

/**
 * @brief Compute the AES-128-CMAC of @p msg under @p key (RFC 4493).
 *
 * @param key      the 16-byte AES-128 key.
 * @param msg      the message (may be null iff @p msg_len is 0).
 * @param msg_len  message length in bytes (0 is valid - the empty-message CMAC).
 * @param mac      receives the 16-byte tag.
 */
void pc_aes_cmac(const uint8_t key[16], const uint8_t *msg, size_t msg_len, uint8_t mac[PC_AES_CMAC_LEN]);

#endif // PROTOCORE_AES_CMAC_H
