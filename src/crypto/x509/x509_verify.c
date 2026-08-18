// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file x509_verify.c
 * @brief One link of a certification path. See x509_verify.h.
 */

#include "crypto/x509/x509_verify.h"

#include "crypto/asymmetric/ecdsa.h"   // Ecdsa.verify: P-256 over the TBS
#include "crypto/asymmetric/ed25519.h" // Ed25519.verify: likewise
#include "crypto/asymmetric/rsa.h"     // Rsa.verify: PKCS#1 v1.5 over the TBS
#include "mmgr/protomem.h"             // mem.cmp / mem.set
#include "mmgr/secure.h"               // the persistent end a verification's bytes come from
#include "shared/der/der.h"            // the reader the key and the signature are decoded with

PROTOCORE_BEGIN_DECLS

/**
 * @brief What a signature check needs a place to put, before the algorithm sees it.
 *
 * An RSA modulus and exponent arrive as INTEGERs of whatever width they were encoded at, and the
 * verifier takes fixed fields, so each is left-padded into one here. Nothing outlives the call.
 */
typedef struct
{
    uint8_t n[PROTOCORE_RSA_KEY_BYTES]; ///< the modulus, left-padded to the width the verifier takes
    uint8_t e[4];                       ///< the public exponent, likewise
} X509VerifyCtx;

// The caller's borrow, split: this context first, then the region the algorithm works in. RSA sizes
// the second - a verification runs over the modulus - and the ECDSA and Ed25519 paths use a
// fraction of it. One pointer arrives and every region is that pointer plus a compile-time offset,
// so the assert below proves the span covers them before anything runs.
#define X509_VERIFY_OFF_CTX 0u
#define X509_VERIFY_OFF_ALG ((size_t)sizeof(X509VerifyCtx))
static_assert(X509_VERIFY_OFF_ALG + PROTOCORE_RSA_BORROW <= PROTOCORE_X509_VERIFY_BORROW,
              "PROTOCORE_X509_VERIFY_BORROW is short of the context and an RSA verification - raise it in"
              " protocore_config.h, which sums it into its arena");

// The regions, at their offsets in the caller's borrow.
#define X509_VERIFY_CTX(w) ((X509VerifyCtx *)(void *)((w) + X509_VERIFY_OFF_CTX))
#define X509_VERIFY_ALG(w) ((w) + X509_VERIFY_OFF_ALG)

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_X509_VERIFY_BORROW persistent bytes, or null while the pool was short
} X509VerifyOwnCtx;
static X509VerifyOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_x509_verify_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_X509_VERIFY_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

// Report a verdict and the reason for it, so a caller that fails knows which condition it was.
static void verdict(protocore_x509_status st)
{
    X509Verify.status = st;
    X509Verify.ok = (st == PROTOCORE_X509_OK);
}

// ---------------------------------------------------------------------------
// The two shapes a public key and a signature arrive in
// ---------------------------------------------------------------------------

// RFC 8017 A.1.1: RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }.
//
// The modulus is left-padded into a fixed PROTOCORE_RSA_KEY_BYTES field because that is what the
// verifier takes, and the exponent into four octets. A modulus wider than the field is a key this
// build cannot verify rather than one to truncate.
static proto_bool rsa_key_split(const uint8_t *der, size_t len, uint8_t *n_out, uint8_t *e_out)
{

    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = 0;
    Der.enter(NULL); // into RSAPublicKey, landing on modulus
    if (!Der.ok || Der.tlv.tag != PROTOCORE_DER_INTEGER)
    {
        return PROTO_FALSE;
    }
    const uint8_t *m = Der.tlv.content;
    size_t mlen = Der.tlv.len;
    const size_t after_mod = Der.tlv.next;
    // X.690 sec 8.3.2: the leading zero is there to keep the modulus positive, and is not part of it.
    if (mlen > 0u && m[0] == 0x00u)
    {
        m++;
        mlen--;
    }
    if (mlen == 0u || mlen > PROTOCORE_RSA_KEY_BYTES)
    {
        return PROTO_FALSE;
    }
    mem.set(n_out, 0, PROTOCORE_RSA_KEY_BYTES);
    mem.cpy(n_out + (PROTOCORE_RSA_KEY_BYTES - mlen), m, mlen);

    Der.read_args.pos = after_mod;
    Der.read(NULL);
    if (!Der.ok || Der.tlv.tag != PROTOCORE_DER_INTEGER)
    {
        return PROTO_FALSE;
    }
    const uint8_t *e = Der.tlv.content;
    size_t elen = Der.tlv.len;
    if (elen > 0u && e[0] == 0x00u)
    {
        e++;
        elen--;
    }
    if (elen == 0u || elen > 4u)
    {
        return PROTO_FALSE;
    }
    mem.set(e_out, 0, 4u);
    mem.cpy(e_out + (4u - elen), e, elen);
    return PROTO_TRUE;
}

// RFC 3279 sec 2.2.3: an ECDSA signature is SEQUENCE { r INTEGER, s INTEGER }, and the verifier
// takes r || s as two fixed 32-octet big-endian fields. Each is left-padded into its half.
static proto_bool ecdsa_sig_split(const uint8_t *der, size_t len, uint8_t out[PROTOCORE_ECDSA_P256_SIG_LEN])
{

    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = 0;
    Der.enter(NULL); // into the SEQUENCE, landing on r
    if (!Der.ok || Der.tlv.tag != PROTOCORE_DER_INTEGER)
    {
        return PROTO_FALSE;
    }
    mem.set(out, 0, PROTOCORE_ECDSA_P256_SIG_LEN);

    for (size_t half = 0; half < 2u; half++)
    {
        if (half == 1u)
        {
            Der.read(NULL);
            if (!Der.ok || Der.tlv.tag != PROTOCORE_DER_INTEGER)
            {
                return PROTO_FALSE;
            }
        }
        const uint8_t *v = Der.tlv.content;
        size_t vlen = Der.tlv.len;
        if (vlen > 0u && v[0] == 0x00u)
        {
            v++;
            vlen--;
        }
        if (vlen == 0u || vlen > 32u)
        {
            return PROTO_FALSE;
        }
        mem.cpy(out + half * 32u + (32u - vlen), v, vlen);
        if (half == 0u)
        {
            Der.read_args.pos = Der.tlv.next;
        }
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The checks
// ---------------------------------------------------------------------------

// One signature check: @p msg under the key a certificate carries, in @p alg. A certificate's TBS
// under its issuer's key and a TLS CertificateVerify under a leaf's key are the same operation, so
// the algorithm dispatch is written once and both entries below reach it.
static void verify_under(const X509Cert *signer, protocore_x509_sig_alg alg, const uint8_t *msg, size_t msg_len,
                         const uint8_t *sig, size_t sig_len, uint8_t *restrict work)
{
    switch (alg)
    {
    case PROTOCORE_X509_SIG_ED25519:
        // RFC 8410 sec 6: the key is 32 raw octets and the signature 64.
        if (signer->key_alg != PROTOCORE_X509_KEY_ED25519 || signer->key.len != 32u || sig_len != 64u)
        {
            verdict(PROTOCORE_X509_ERR_KEY_MALFORMED);
            return;
        }
        Ed25519.verify_args.pub = signer->key.p;
        Ed25519.verify_args.msg = msg;
        Ed25519.verify_args.msg_len = msg_len;
        Ed25519.verify_args.sig = sig;
        Ed25519.verify(X509_VERIFY_ALG(work));
        verdict(Ed25519.ok ? PROTOCORE_X509_OK : PROTOCORE_X509_ERR_BAD_SIGNATURE);
        return;

    case PROTOCORE_X509_SIG_ECDSA_SHA256: {
        if (signer->key_alg != PROTOCORE_X509_KEY_EC_P256 || signer->key.len != PROTOCORE_ECDSA_P256_PUB_LEN)
        {
            verdict(PROTOCORE_X509_ERR_KEY_MALFORMED);
            return;
        }
        uint8_t rs[PROTOCORE_ECDSA_P256_SIG_LEN];
        if (!ecdsa_sig_split(sig, sig_len, rs))
        {
            verdict(PROTOCORE_X509_ERR_SIG_MALFORMED);
            return;
        }
        Ecdsa.verify_args.pub = signer->key.p;
        Ecdsa.verify_args.msg = msg;
        Ecdsa.verify_args.mlen = msg_len;
        Ecdsa.verify_args.sig = rs;
        Ecdsa.verify(X509_VERIFY_ALG(work));
        verdict(Ecdsa.ok ? PROTOCORE_X509_OK : PROTOCORE_X509_ERR_BAD_SIGNATURE);
        return;
    }

    case PROTOCORE_X509_SIG_RSA_SHA256:
    case PROTOCORE_X509_SIG_RSA_SHA512:
    case PROTOCORE_X509_SIG_RSA_PSS: {
        if (signer->key_alg != PROTOCORE_X509_KEY_RSA)
        {
            verdict(PROTOCORE_X509_ERR_KEY_MALFORMED);
            return;
        }
        uint8_t *n = X509_VERIFY_CTX(work)->n;
        uint8_t *e = X509_VERIFY_CTX(work)->e;
        if (!rsa_key_split(signer->key.p, signer->key.len, n, e))
        {
            verdict(PROTOCORE_X509_ERR_KEY_MALFORMED);
            return;
        }
        if (sig_len != PROTOCORE_RSA_KEY_BYTES)
        {
            verdict(PROTOCORE_X509_ERR_SIG_MALFORMED);
            return;
        }
        Rsa.verify_args.n = n;
        Rsa.verify_args.e = e;
        Rsa.verify_args.msg = msg;
        Rsa.verify_args.msg_len = msg_len;
        Rsa.verify_args.sig = sig;
        Rsa.verify_args.sig_len = sig_len;
        Rsa.verify_args.hash = (alg == PROTOCORE_X509_SIG_RSA_SHA512)  ? PROTOCORE_RSA_HASH_SHA512
                               : (alg == PROTOCORE_X509_SIG_RSA_PSS) ? PROTOCORE_RSA_HASH_PSS_SHA256
                                                                     : PROTOCORE_RSA_HASH_SHA256;
        Rsa.verify(X509_VERIFY_ALG(work));
        verdict(Rsa.ok ? PROTOCORE_X509_OK : PROTOCORE_X509_ERR_BAD_SIGNATURE);
        return;
    }

    default:
        // An algorithm this build does not verify is refused rather than verified under one it
        // does. RSA-PSS and the SHA-384 variants land here until their paths exist.
        verdict(PROTOCORE_X509_ERR_ALG_UNSUPPORTED);
        return;
    }
}

static void x509_signature(uint8_t *restrict work)
{
    if (!work)
    {
        verdict(PROTOCORE_X509_ERR_ARGS);
        return; // the pool was short of this module's borrow
    }
    const X509Cert *cert = X509Verify.link_args.cert;
    const X509Cert *issuer = X509Verify.link_args.issuer;
    if (cert == NULL || issuer == NULL || cert->tbs.p == NULL || cert->sig.p == NULL || issuer->key.p == NULL)
    {
        verdict(PROTOCORE_X509_ERR_ARGS);
        return;
    }
    verify_under(issuer, cert->sig_alg, cert->tbs.p, cert->tbs.len, cert->sig.p, cert->sig.len, work);
}

static void x509_message(uint8_t *restrict work)
{
    if (!work)
    {
        verdict(PROTOCORE_X509_ERR_ARGS);
        return; // the pool was short of this module's borrow
    }
    const X509Cert *signer = X509Verify.message_args.signer;
    const uint8_t *msg = X509Verify.message_args.msg;
    const uint8_t *sig = X509Verify.message_args.sig;
    if (signer == NULL || signer->key.p == NULL || msg == NULL || sig == NULL)
    {
        verdict(PROTOCORE_X509_ERR_ARGS);
        return;
    }
    verify_under(signer, X509Verify.message_args.alg, msg, X509Verify.message_args.msg_len, sig,
                 X509Verify.message_args.sig_len, work);
}

static void x509_validity(uint8_t *restrict work)
{
    (void)work;
    const X509Cert *cert = X509Verify.time_args.cert;
    if (cert == NULL)
    {
        verdict(PROTOCORE_X509_ERR_ARGS);
        return;
    }
    // RFC 5280 sec 6.1.3 (a)(2): the period includes the current time. Both ends are inclusive
    // (sec 4.1.2.5 makes them the first and last instant the certificate is valid).
    const uint64_t now = X509Verify.time_args.now;
    if (now < cert->not_before)
    {
        verdict(PROTOCORE_X509_ERR_NOT_YET_VALID);
        return;
    }
    if (now > cert->not_after)
    {
        verdict(PROTOCORE_X509_ERR_EXPIRED);
        return;
    }
    verdict(PROTOCORE_X509_OK);
}

static void x509_may_sign(uint8_t *restrict work)
{
    (void)work;
    const X509Cert *issuer = X509Verify.issuer_args.issuer;
    if (issuer == NULL)
    {
        verdict(PROTOCORE_X509_ERR_ARGS);
        return;
    }
    // sec 6.1.4 (k): basicConstraints present and cA TRUE. A v1 or v2 certificate carries no
    // extensions at all, and sec 6.1.4 (k) allows rejecting those outright, which is what this
    // does: an issuer that does not say it is a CA is not treated as one.
    if (!issuer->has_bc || !issuer->is_ca)
    {
        verdict(PROTOCORE_X509_ERR_NOT_A_CA);
        return;
    }
    // sec 6.1.4 (n): if keyUsage is present, keyCertSign must be set. Absent, it constrains nothing.
    if (issuer->has_ku && (issuer->key_usage & PROTOCORE_X509_KU_KEY_CERT_SIGN) == 0u)
    {
        verdict(PROTOCORE_X509_ERR_NO_CERT_SIGN);
        return;
    }
    // sec 6.1.4 (m): pathLenConstraint is how many non-self-issued certificates may follow this one
    // in the path. depth is how many are below it, so a constraint under that does not reach.
    if (issuer->has_path_len && issuer->path_len < X509Verify.issuer_args.depth)
    {
        verdict(PROTOCORE_X509_ERR_PATH_LEN);
        return;
    }
    verdict(PROTOCORE_X509_OK);
}

static void x509_link(uint8_t *restrict work)
{
    const X509Cert *cert = X509Verify.link_args.cert;
    const X509Cert *issuer = X509Verify.link_args.issuer;
    if (cert == NULL || issuer == NULL)
    {
        verdict(PROTOCORE_X509_ERR_ARGS);
        return;
    }

    // sec 6.1.3 (a)(4): the issuer name is the subject of the certificate above. Both are kept as
    // they were encoded, so this is the byte comparison the rule asks for.
    if (cert->issuer.len == 0u || cert->issuer.len != issuer->subject.len ||
        mem.cmp(cert->issuer.p, issuer->subject.p, cert->issuer.len) != 0)
    {
        verdict(PROTOCORE_X509_ERR_ISSUER_NAME);
        return;
    }

    x509_validity(work);
    if (!X509Verify.ok)
    {
        return;
    }
    x509_may_sign(work);
    if (!X509Verify.ok)
    {
        return;
    }
    x509_signature(work);
}

// Designated, so a member's position in the struct does not decide what it binds to.
X509VerifyNs X509Verify = {
    .signature = x509_signature,
    .validity = x509_validity,
    .may_sign = x509_may_sign,
    .link = x509_link,
    .message = x509_message,
};

PROTOCORE_END_DECLS
