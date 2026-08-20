// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_crypto.h
 * @brief The two USM transforms: key localization and the privacy cipher (PROTOCORE_ENABLE_SNMP_V3).
 *
 * Exactly what the User-based Security Model needs, and nothing more.
 *
 * **Key localization.** RFC 3414 sec 2.6 defines a localized key as one derived from a user's
 * secret and the authoritative snmpEngineID, so a key that leaks reaches one engine only. RFC 7860
 * sec 5 says that localization for the HMAC-SHA-2 protocols "SHALL be performed according to
 * [RFC3414] using the same SHA-2 hash function as in the HMAC-SHA-2 authentication protocol", and
 * RFC 7860 sec 9.3 states the password-to-key derivation: the password is repeated to a 1,048,576
 * octet string and hashed to digest1, then digest1, snmpEngineID and digest1 again are hashed to
 * the localized key. It is the RFC 3414 password-to-key algorithm with SHA-256 in place of MD5.
 * The same procedure produces the authentication key and the privacy key, each from its own
 * password.
 *
 * **Privacy.** RFC 3826 defines usmAesCfb128Protocol: CFB128-AES-128. The cipher is AES (FIPS 197,
 * not an RFC) and the mode is CFB with a 128-bit feedback segment (NIST SP 800-38A, not an RFC).
 * RFC 3826 sec 3.1.2.1 states the 16-octet IV: snmpEngineBoots big-endian, then snmpEngineTime
 * big-endian, then the 8-octet msgPrivacyParameters salt. The privacy key is the first 16 octets
 * of the localized key.
 *
 * CFB is a stream mode, so the output length equals the input length and no padding is added, and
 * decryption is the same construction with the feedback taken from the ciphertext. The transform
 * is safe in place. SHA-256 and HMAC-SHA-256 come from the shared hash and MAC modules. This
 * software AES is not constant time.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SNMP_CRYPTO_H
#define PROTOCORE_SNMP_CRYPTO_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SNMP_V3

PROTOCORE_BEGIN_DECLS

/** @brief Localized-key length: the SHA-256 digest size (RFC 7860 sec 9.3). */
#define SNMP_USM_KEY_LEN 32

/** @brief RFC 7860 sec 9.3: what a password-to-key derivation reads, and where the key lands. */
typedef struct
{
    const char *password;     ///< the user's authentication or privacy password
    const uint8_t *engine_id; ///< the authoritative snmpEngineID (RFC 3414 sec 2.6)
    size_t engine_id_len;     ///< how many octets
    uint8_t *out;             ///< where ::SNMP_USM_KEY_LEN localized-key octets land
} SnmpUsmKeyArgs;
/** @brief RFC 3826 sec 3.1.2.1: what the privacy transform reads and where it writes. */
typedef struct
{
    const uint8_t *key; ///< the 16-octet AES key: the first 16 octets of the localized privacy key
    const uint8_t *iv;  ///< the 16-octet IV: snmpEngineBoots, snmpEngineTime, msgPrivacyParameters
    const uint8_t *in;  ///< the octets to transform
    uint8_t *out;       ///< where they land; may be @c in
    size_t len;         ///< how many
    proto_bool encrypt; ///< encrypt, otherwise decrypt with the feedback taken from the ciphertext
} SnmpUsmPrivArgs;
/**
 * @brief The USM transforms (RFC 3414 sec 2.6, RFC 7860 sec 9.3, RFC 3826 sec 3.1.2.1).
 *
 * A caller sets the members a call takes, invokes it through ::SnmpCrypto, and reads the outcome
 * off the same handle.
 *
 * No storage member: both transforms read their inputs and write the caller's destination, so the
 * module holds no key and no state between calls.
 *
 * @var SnmpCryptoNs::work          the caller's scratch region the hash borrows
 * @var SnmpCryptoNs::key           what a password-to-key derivation reads, and where its key lands
 * @var SnmpCryptoNs::priv          what the privacy transform reads and where it writes
 * @var SnmpCryptoNs::ok            a call's true/false outcome
 * @var SnmpCryptoNs::localize_key  derive the engine-localized key from a password
 * @var SnmpCryptoNs::aes_cfb128    run CFB128-AES-128 over @c priv.in into @c priv.out
 */
typedef struct
{
    uint8_t *work;        ///< PROTOCORE_HMAC_SHA256_BORROW octets, aligned for uint32_t, alive across the call
    SnmpUsmKeyArgs key;   ///< what a derivation reads and writes
    SnmpUsmPrivArgs priv; ///< what the privacy transform reads and writes
    proto_bool ok;
} SnmpCryptoVars;

/** @brief The operands and the outcome. */
extern SnmpCryptoVars SnmpCryptoV;

/** @brief The entries. */
typedef struct
{
    void (*const localize_key)(uint8_t *restrict work);
    void (*const aes_cfb128)(uint8_t *restrict work);
} SnmpCryptoNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SnmpCryptoV or a region of the borrow at a fixed offset.
void protocore_snmp_crypto_localize_key(uint8_t *restrict work);
void protocore_snmp_crypto_aes_cfb128(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SnmpCrypto.localize_key(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SnmpCryptoNs SnmpCrypto __attribute__((unused)) = {
    .localize_key = protocore_snmp_crypto_localize_key,
    .aes_cfb128 = protocore_snmp_crypto_aes_cfb128,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNMP_V3

#endif // PROTOCORE_SNMP_CRYPTO_H
