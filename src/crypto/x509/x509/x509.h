// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file x509.h
 * @brief RFC 5280 certificates: what one says, read out of the caller's DER.
 *
 * A parse is a view, not a copy. Every field this reports is a pointer into the caller's own bytes
 * and a length, so a certificate costs nothing but the struct below and the encoding it points at,
 * and the encoding has to outlive the view.
 *
 * The TBSCertificate's own bytes are reported too, because that is what the signature covers
 * (sec 4.1.1.2): a verifier hashes exactly those octets, so they are handed back exactly as they
 * arrived rather than re-encoded from the parsed fields.
 *
 * This module reads and matches. It verifies nothing: a signature needs the key algorithms, and a
 * chain needs a trust anchor and a clock, so both sit above this.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_X509_H
#define PROTOCORE_X509_H

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

/** @brief The encoding a parse reads. */
typedef struct
{
    const uint8_t *der; ///< one Certificate, DER
    size_t len;         ///< how many octets
} X509ParseArgs;

/** @brief What a name match judges: the presented certificate, and the name asked for. */
typedef struct
{
    const X509Cert *cert; ///< the certificate presented
    const char *host;     ///< the name the caller asked for, NUL terminated
    size_t host_len;      ///< its length, or 0 to measure it
} X509MatchArgs;

/**
 * @brief Certificates: read one, and judge whether it speaks for a name.
 *
 * A caller sets the members a call takes, invokes it through ::X509, and reads the outcome off the
 * same handle.
 *
 * @var X509Ns::parse_args  the encoding a parse reads
 * @var X509Ns::match_args  the certificate and the name a match judges
 * @var X509Ns::cert        what a parse found
 * @var X509Ns::ok          a call's true/false outcome
 * @var X509Ns::parse       read one Certificate (sec 4.1). Refuses anything it cannot represent
 *                          rather than reporting a partial view
 * @var X509Ns::name_match  whether the certificate speaks for @c match_args.host, by RFC 6125
 *                          sec 6.4: the subjectAltName dNSName entries only, never the subject
 *                          common name
 *
 * No storage member: a parse works in the caller's encoding and reports on this handle.
 */
typedef struct
{
    X509ParseArgs parse_args;
    X509MatchArgs match_args;
    X509Cert cert;
    proto_bool ok;
} X509Vars;

/** @brief The operands and the outcome. */
extern X509Vars X509V;

/** @brief The entries. */
typedef struct
{
    void (*const parse)(uint8_t *restrict work);
    void (*const name_match)(uint8_t *restrict work);
} X509Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in X509V or a region of the borrow at a fixed offset.
void protocore_x509_parse(uint8_t *restrict work);
void protocore_x509_name_match(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `X509.parse(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const X509Ns X509 __attribute__((unused)) = {
    .parse = protocore_x509_parse,
    .name_match = protocore_x509_name_match,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_X509_H
