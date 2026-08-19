// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chachapoly.h
 * @brief chacha20-poly1305@openssh.com AEAD cipher (OpenSSH PROTOCOL.chacha20poly1305).
 *
 * OpenSSH's authenticated cipher for the SSH binary packet. The 512-bit key is split into two
 * 256-bit ChaCha20 keys: K_main = key[0..32] encrypts the packet payload, K_header = key[32..64]
 * encrypts the 4-byte packet-length field separately (so a receiver can size the packet before it
 * has the whole thing). The nonce for both is the packet sequence number as a big-endian uint64.
 *
 *   - Poly1305 key = first 32 bytes of ChaCha20(K_main, seqnr, counter 0)
 *   - encrypted length  = ChaCha20(K_header, seqnr, counter 0) XOR length
 *   - encrypted payload = ChaCha20(K_main,   seqnr, counter 1) XOR payload
 *   - tag = Poly1305(encrypted_length || encrypted_payload)  (16 bytes, appended)
 *
 * On decrypt the tag is verified (constant-time) before any plaintext is produced. Pure, no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CHACHAPOLY_H
#define PROTOCORE_CHACHAPOLY_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool and the widths

#if PROTOCORE_ENABLE_CHACHAPOLY

PROTOCORE_BEGIN_DECLS

#define PROTOCORE_CHACHAPOLY_KEY_LEN 64 ///< two 256-bit ChaCha20 keys
#define PROTOCORE_CHACHAPOLY_TAG_LEN 16 ///< Poly1305 tag
#define PROTOCORE_CHACHAPOLY_AAD_LEN 4  ///< the encrypted packet-length field

// PROTOCORE_CHACHAPOLY_BORROW - the bytes one packet operation runs out of - is stated in
// protocore_config.h, which sums it into the secure arena. A caller takes them once and passes the
// pointer to every call.

/** @brief The encrypted length field a length peek reads. */
typedef struct
{
    const uint8_t *key;     ///< PROTOCORE_CHACHAPOLY_KEY_LEN bytes
    const uint8_t *enc_len; ///< PROTOCORE_CHACHAPOLY_AAD_LEN encrypted length bytes
    uint32_t seqnr;         ///< packet sequence number, the ChaCha nonce
} ChachaPolyLengthArgs;

/** @brief The packet an encryption covers. */
typedef struct
{
    const uint8_t *key;   ///< PROTOCORE_CHACHAPOLY_KEY_LEN bytes
    const uint8_t *src;   ///< plaintext: 4-byte packet length (big-endian) || payload_len payload bytes
    uint8_t *dest;        ///< encrypted length (4) || encrypted payload (payload_len) || tag (16); may alias src
    uint32_t seqnr;       ///< packet sequence number, the ChaCha nonce
    uint32_t payload_len; ///< payload bytes following the length field
} ChachaPolyEncryptArgs;

/** @brief The packet a decryption verifies. */
typedef struct
{
    const uint8_t *key;   ///< PROTOCORE_CHACHAPOLY_KEY_LEN bytes
    const uint8_t *src;   ///< ciphertext: encrypted length (4) || encrypted payload (payload_len) || tag (16)
    uint8_t *dest;        ///< plaintext length (4) || plaintext payload (payload_len); may alias src
    uint32_t seqnr;       ///< packet sequence number, the ChaCha nonce
    uint32_t payload_len; ///< payload bytes following the length field
} ChachaPolyDecryptArgs;

/**
 * @brief chacha20-poly1305@openssh.com (OpenSSH PROTOCOL.chacha20poly1305).
 *
 * A caller sets the members a call takes, invokes it through ::ChachaPoly with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never named here.
 *
 *   ChachaPoly.length_args.key = key;
 *   ChachaPoly.length_args.enc_len = pkt;
 *   ChachaPoly.length_args.seqnr = seqnr;
 *   ChachaPoly.get_length(work);
 *   // ChachaPoly.length now holds the packet_length
 *
 * @var ChachaPolyNs::length_args   the encrypted length field a length peek reads
 * @var ChachaPolyNs::encrypt_args  the packet an encryption covers
 * @var ChachaPolyNs::decrypt_args  the packet a decryption verifies
 * @var ChachaPolyNs::ok            a call's true/false outcome; false on a null pointer, and on a tag mismatch
 * @var ChachaPolyNs::length        the SSH packet_length the last peek recovered: bytes after the length
 *                                  field, excluding the tag
 * @var ChachaPolyNs::get_length    decrypt the 4-byte length field to size the packet before reading its body
 * @var ChachaPolyNs::encrypt       encrypt and authenticate one packet, tag appended
 * @var ChachaPolyNs::decrypt       verify the tag, then decrypt; no plaintext is produced unless it verified
 *
 * @c work is PROTOCORE_CHACHAPOLY_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and the pool
 * wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That is what keeps the
 * one-time Poly1305 key derived per packet from outliving the caller. A connection takes those bytes once for
 * its slot and passes them on every packet.
 *
 * No storage member and no context: a caller sets operands and reads @ref ChachaPolyNs::ok, and that is all the
 * surface there is.
 */
typedef struct
{
    ChachaPolyLengthArgs length_args;
    ChachaPolyEncryptArgs encrypt_args;
    ChachaPolyDecryptArgs decrypt_args;

    proto_bool ok;
    uint32_t length;

    void (*const get_length)(uint8_t *restrict work);
    void (*const encrypt)(uint8_t *restrict work);
    void (*const decrypt)(uint8_t *restrict work);
} ChachaPolyNs;

/** @brief The one symbol this module exports. */
extern ChachaPolyNs ChachaPoly;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CHACHAPOLY

#endif // PROTOCORE_CHACHAPOLY_H
