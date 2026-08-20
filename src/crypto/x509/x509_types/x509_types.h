// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file x509_types.h
 * @brief What a certificate SAYS, as types: the algorithm identifiers and the parsed view.
 *
 * Vocabulary, not behaviour - no state, no entries, nothing to link. It is a module of its own
 * because two paths need the words and only one needs the parser: a TLS connection that
 * authenticates by RFC 7250 raw public key still carries the peer's key in an @ref X509Cert and
 * still names a signature scheme as a @ref protocore_x509_sig_alg, while the RFC 5280 DER
 * parsing behind PROTOCORE_ENABLE_X509 is exactly what such a build does not compile.
 *
 * A parsed certificate is a VIEW, not a copy: every @ref X509Bytes here points into the caller's
 * own encoding, which has to outlive the view.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_X509_TYPES_H
#define PROTOCORE_X509_TYPES_H

#include "protocore_config.h" // the entry point: the widths

PROTOCORE_BEGIN_DECLS

/** @brief The signature algorithms this profile reads (RFC 5280 sec 4.1.1.2). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_X509_SIG_UNKNOWN = 0,  ///< an algorithm this build does not verify
    PROTOCORE_X509_SIG_RSA_SHA256,   ///< sha256WithRSAEncryption, {pkcs-1 11} (RFC 8017 A.2.4)
    PROTOCORE_X509_SIG_RSA_SHA384,   ///< sha384WithRSAEncryption, {pkcs-1 12}
    PROTOCORE_X509_SIG_RSA_SHA512,   ///< sha512WithRSAEncryption, {pkcs-1 13}
    PROTOCORE_X509_SIG_RSA_PSS,      ///< id-RSASSA-PSS, {pkcs-1 10}
    PROTOCORE_X509_SIG_ECDSA_SHA256, ///< ecdsa-with-SHA256, 1.2.840.10045.4.3.2 (RFC 5480 sec 2.1.1)
    PROTOCORE_X509_SIG_ECDSA_SHA384, ///< ecdsa-with-SHA384, 1.2.840.10045.4.3.3
    PROTOCORE_X509_SIG_ED25519,      ///< id-Ed25519, 1.3.101.112 (RFC 8410 sec 3)
} protocore_x509_sig_alg;

/** @brief The public key algorithms this profile reads (RFC 5280 sec 4.1.2.7). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_X509_KEY_UNKNOWN = 0, ///< an algorithm this build cannot use
    PROTOCORE_X509_KEY_RSA,         ///< rsaEncryption, {pkcs-1 1}
    PROTOCORE_X509_KEY_EC_P256,     ///< id-ecPublicKey over secp256r1 (RFC 5480 sec 2.1.1)
    PROTOCORE_X509_KEY_ED25519,     ///< id-Ed25519 (RFC 8410 sec 4)
} protocore_x509_key_alg;

/** @name RFC 5280 sec 4.2.1.3 KeyUsage bits, in the order the BIT STRING numbers them.
 *  @{ */
#define PROTOCORE_X509_KU_DIGITAL_SIGNATURE 0x0001u
#define PROTOCORE_X509_KU_NON_REPUDIATION 0x0002u
#define PROTOCORE_X509_KU_KEY_ENCIPHERMENT 0x0004u
#define PROTOCORE_X509_KU_DATA_ENCIPHERMENT 0x0008u
#define PROTOCORE_X509_KU_KEY_AGREEMENT 0x0010u
#define PROTOCORE_X509_KU_KEY_CERT_SIGN 0x0020u
#define PROTOCORE_X509_KU_CRL_SIGN 0x0040u
/** @} */

/** @brief A run of the caller's bytes: where a field is, and how much of it there is. */
typedef struct
{
    const uint8_t *p; ///< into the caller's encoding
    size_t len;       ///< how many octets
} X509Bytes;

/**
 * @brief One certificate, as a view over the DER it was read from.
 *
 * @var X509Cert::tbs        the TBSCertificate's own octets, which the signature covers
 * @var X509Cert::serial     serialNumber's content, as it was encoded (sec 4.1.2.2 allows 20 octets)
 * @var X509Cert::issuer     the issuer Name, whole and encoded: a chain matches it against a
 *                           subject byte for byte, so it is not decoded
 * @var X509Cert::subject    the subject Name, likewise
 * @var X509Cert::spki       the SubjectPublicKeyInfo's own octets, whole
 * @var X509Cert::key        subjectPublicKey's octets, past the BIT STRING's unused-bits count
 * @var X509Cert::sig        signatureValue's octets, likewise
 * @var X509Cert::san        the subjectAltName extension's value, or empty when absent
 * @var X509Cert::not_before validity's start, seconds since the POSIX epoch (sec 4.1.2.5)
 * @var X509Cert::not_after  validity's end, likewise
 * @var X509Cert::sig_alg    what signed it
 * @var X509Cert::key_alg    what its public key is
 * @var X509Cert::key_usage  the KeyUsage bits, or 0 when the extension is absent
 * @var X509Cert::path_len   basicConstraints pathLenConstraint, when stated
 * @var X509Cert::version    0, 1 or 2 for v1, v2, v3 (sec 4.1.2.1)
 * @var X509Cert::is_ca      basicConstraints cA (sec 4.2.1.9)
 * @var X509Cert::has_bc     the basicConstraints extension was present at all
 * @var X509Cert::has_ku     the keyUsage extension was present at all
 * @var X509Cert::has_path_len  pathLenConstraint was stated
 */
typedef struct
{
    X509Bytes tbs;
    X509Bytes serial;
    X509Bytes issuer;
    X509Bytes subject;
    X509Bytes spki;
    X509Bytes key;
    X509Bytes sig;
    X509Bytes san;

    uint64_t not_before;
    uint64_t not_after;

    protocore_x509_sig_alg sig_alg;
    protocore_x509_key_alg key_alg;
    uint16_t key_usage;
    uint32_t path_len;
    uint8_t version;
    proto_bool is_ca;
    proto_bool has_bc;
    proto_bool has_ku;
    proto_bool has_path_len;
} X509Cert;

PROTOCORE_END_DECLS

#endif // PROTOCORE_X509_TYPES_H
