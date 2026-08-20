// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file x509_verify.h
 * @brief Is this certificate signed by that one, and may that one sign it at all.
 *
 * RFC 5280 sec 6.1.3 (a): a certificate is checked against a working public key, a working issuer
 * name and the current time. This is that check for one link of a chain - the leaf against its
 * issuer - and the per-certificate conditions sec 6.1.4 (k), (l) and (n) put on an issuer before it
 * is allowed to have signed anything.
 *
 * Every check is separate and each reports its own verdict, so a caller that fails one knows which.
 * A link that passes ::X509VerifyNs::link has been checked on all of them; the individual entries
 * exist because a chain walk needs them at different points and a test needs them apart.
 *
 * The signature covers the TBSCertificate's own octets (sec 4.1.1.2), which ::X509Cert::tbs carries
 * unmodified, so nothing is re-encoded on the way to the verifier.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_X509_VERIFY_H
#define PROTOCORE_X509_VERIFY_H

#include "crypto/x509/x509/x509.h" // X509Cert: what a check is given

PROTOCORE_BEGIN_DECLS

/** @brief Why a link was refused. A caller that only needs yes or no reads ::X509VerifyNs::ok. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_X509_OK = 0,              ///< the link holds
    PROTOCORE_X509_ERR_ARGS,            ///< a certificate was not supplied
    PROTOCORE_X509_ERR_ISSUER_NAME,     ///< sec 6.1.3 (a)(4): the issuer name is not the subject above it
    PROTOCORE_X509_ERR_NOT_YET_VALID,   ///< sec 6.1.3 (a)(2): the current time is before notBefore
    PROTOCORE_X509_ERR_EXPIRED,         ///< sec 6.1.3 (a)(2): the current time is after notAfter
    PROTOCORE_X509_ERR_NOT_A_CA,        ///< sec 6.1.4 (k): the issuer has no basicConstraints cA TRUE
    PROTOCORE_X509_ERR_NO_CERT_SIGN,    ///< sec 6.1.4 (n): the issuer's keyUsage omits keyCertSign
    PROTOCORE_X509_ERR_PATH_LEN,        ///< sec 6.1.4 (m): pathLenConstraint does not reach this far
    PROTOCORE_X509_ERR_ALG_UNSUPPORTED, ///< an algorithm this build does not verify
    PROTOCORE_X509_ERR_KEY_MALFORMED,   ///< the issuer's public key did not decode
    PROTOCORE_X509_ERR_SIG_MALFORMED,   ///< the signature did not decode
    PROTOCORE_X509_ERR_BAD_SIGNATURE,   ///< it decoded, and it does not verify
} protocore_x509_status;

/** @brief What a signature check is given: the certificate, and the one whose key signed it. */
typedef struct
{
    const X509Cert *cert;   ///< the certificate being checked
    const X509Cert *issuer; ///< the certificate whose subjectPublicKey signed it
} X509LinkArgs;

/** @brief What a time check is given. */
typedef struct
{
    const X509Cert *cert; ///< the certificate being checked
    uint64_t now;         ///< seconds since the POSIX epoch
} X509TimeArgs;

/** @brief What an issuer check is given: the candidate, and how far down the chain it sits. */
typedef struct
{
    const X509Cert *issuer; ///< the candidate issuer
    uint32_t depth;         ///< certificates below it in the path, 0 for the one that signs a leaf
} X509IssuerArgs;

/** @brief What a message check is given: whose key verifies it, and the bytes it covers. */
typedef struct
{
    const X509Cert *signer;     ///< the certificate whose subjectPublicKey verifies
    protocore_x509_sig_alg alg; ///< the scheme the signature is in
    const uint8_t *msg;         ///< the bytes signed
    size_t msg_len;             ///< how many
    const uint8_t *sig;         ///< the signature over them
    size_t sig_len;             ///< its length
} X509MessageArgs;

/**
 * @brief One link of a certification path.
 *
 * A caller sets the members a call takes, invokes it through ::X509Verify, and reads the outcome
 * off the same handle.
 *
 * @var X509VerifyNs::link_args    the certificate and its issuer
 * @var X509VerifyNs::time_args    the certificate and the current time
 * @var X509VerifyNs::issuer_args  the candidate issuer and its depth
 * @var X509VerifyNs::message_args whose key verifies a message, and the bytes it covers
 * @var X509VerifyNs::work         the bytes a signature check runs out of; the caller's
 * @var X509VerifyNs::ok           a call's true/false outcome
 * @var X509VerifyNs::status       why, when it is false
 * @var X509VerifyNs::signature    the signature over the TBS verifies under the issuer's key
 * @var X509VerifyNs::validity     the current time is inside the certificate's validity period
 * @var X509VerifyNs::may_sign     the issuer is allowed to have signed anything at this depth
 * @var X509VerifyNs::link         all three, and the issuer-name match: one whole link
 * @var X509VerifyNs::message      a signature over arbitrary bytes verifies under a certificate's
 *                                 key, which is what a TLS CertificateVerify is (RFC 8446 sec 4.4.3)
 *
 * No storage member: the caller hands in the bytes a signature check needs.
 */
typedef struct
{
    X509LinkArgs link_args;
    X509TimeArgs time_args;
    X509IssuerArgs issuer_args;
    X509MessageArgs message_args;
    proto_bool ok;
    protocore_x509_status status;
} X509VerifyVars;

/** @brief The operands and the outcome. */
extern X509VerifyVars X509VerifyV;

/** @brief The entries. */
typedef struct
{
    void (*const signature)(uint8_t *restrict work);
    void (*const validity)(uint8_t *restrict work);
    void (*const may_sign)(uint8_t *restrict work);
    void (*const link)(uint8_t *restrict work);
    void (*const message)(uint8_t *restrict work);
} X509VerifyNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in X509VerifyV or a region of the borrow at a fixed offset.
void protocore_x509_verify_signature(uint8_t *restrict work);
void protocore_x509_verify_validity(uint8_t *restrict work);
void protocore_x509_verify_may_sign(uint8_t *restrict work);
void protocore_x509_verify_link(uint8_t *restrict work);
void protocore_x509_verify_message(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `X509Verify.signature(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const X509VerifyNs X509Verify __attribute__((unused)) = {
    .signature = protocore_x509_verify_signature,
    .validity = protocore_x509_verify_validity,
    .may_sign = protocore_x509_verify_may_sign,
    .link = protocore_x509_verify_link,
    .message = protocore_x509_verify_message,
};

/**
 * @brief The PROTOCORE_X509_VERIFY_BORROW bytes a signature check runs out of.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. RSA is what sizes it - a 2048-bit verification works over the modulus.
 *
 * @return the span.
 */
uint8_t *protocore_x509_verify_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_X509_VERIFY_H
