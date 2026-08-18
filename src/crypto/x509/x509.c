// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file x509.c
 * @brief RFC 5280 certificates, read out of the caller's DER. See x509.h.
 */

#include "crypto/x509/x509.h"

#include "mmgr/protomem.h"  // mem.set: the view is cleared before a parse fills it
#include "shared/der/der.h" // Der: the one reader

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// The object identifiers this profile knows, each as its encoded content octets
// ---------------------------------------------------------------------------

// RFC 8017 A.1: pkcs-1 is 1.2.840.113549.1.1, and each algorithm is a leaf under it.
static const uint8_t OID_RSA_ENCRYPTION[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01}; // {pkcs-1 1}
static const uint8_t OID_RSASSA_PSS[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0A};     // {pkcs-1 10}
static const uint8_t OID_RSA_SHA256[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B};     // {pkcs-1 11}
static const uint8_t OID_RSA_SHA384[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0C};     // {pkcs-1 12}
static const uint8_t OID_RSA_SHA512[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0D};     // {pkcs-1 13}

// RFC 5480 sec 2.1.1: id-ecPublicKey 1.2.840.10045.2.1, secp256r1 1.2.840.10045.3.1.7,
// ecdsa-with-SHA256 1.2.840.10045.4.3.2, ecdsa-with-SHA384 ...4.3.3.
static const uint8_t OID_EC_PUBLIC_KEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
static const uint8_t OID_SECP256R1[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
static const uint8_t OID_ECDSA_SHA256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
static const uint8_t OID_ECDSA_SHA384[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03};

// RFC 8410 sec 3: id-Ed25519 1.3.101.112, used as both a key and a signature algorithm.
static const uint8_t OID_ED25519[] = {0x2B, 0x65, 0x70};

// RFC 5280 sec 4.2.1: id-ce is 2.5.29, and each extension is a leaf under it.
static const uint8_t OID_KEY_USAGE[] = {0x55, 0x1D, 0x0F};          // { id-ce 15 }
static const uint8_t OID_SUBJECT_ALT_NAME[] = {0x55, 0x1D, 0x11};   // { id-ce 17 }
static const uint8_t OID_BASIC_CONSTRAINTS[] = {0x55, 0x1D, 0x13};  // { id-ce 19 }

// ---------------------------------------------------------------------------
// Reading, one field at a time
// ---------------------------------------------------------------------------

// One value at @p pos in @p der. The reader is stateless, so every step names its own bytes.
static proto_bool at(const uint8_t *der, size_t len, size_t pos)
{
    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = pos;
    Der.read(NULL);
    return Der.ok;
}

// Whether the OID value at @p pos is @p oid.
static proto_bool oid_is(const uint8_t *der, size_t len, size_t pos, const uint8_t *oid, size_t oid_len)
{
    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = pos;
    Der.oid_args.oid = oid;
    Der.oid_args.oid_len = oid_len;
    Der.oid_eq(NULL);
    return Der.ok;
}

// The AlgorithmIdentifier at @p pos as one of the signature algorithms this profile verifies
// (RFC 5280 sec 4.1.1.2). An algorithm this build cannot check reads as UNKNOWN rather than as
// something close to it, so a caller refuses rather than verifying under the wrong scheme.
static protocore_x509_sig_alg sig_alg_at(const uint8_t *der, size_t len, size_t pos)
{
    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = pos;
    Der.enter(NULL); // AlgorithmIdentifier ::= SEQUENCE { algorithm OBJECT IDENTIFIER, parameters }
    if (!Der.ok)
    {
        return PROTOCORE_X509_SIG_UNKNOWN;
    }
    // enter leaves the position on the value it stepped to, so the OID is named by that rather than
    // by backing up over its header - which would assume the header's width.
    const size_t alg = Der.read_args.pos;
    if (oid_is(der, len, alg, OID_RSA_SHA256, sizeof(OID_RSA_SHA256)))
    {
        return PROTOCORE_X509_SIG_RSA_SHA256;
    }
    if (oid_is(der, len, alg, OID_RSA_SHA384, sizeof(OID_RSA_SHA384)))
    {
        return PROTOCORE_X509_SIG_RSA_SHA384;
    }
    if (oid_is(der, len, alg, OID_RSA_SHA512, sizeof(OID_RSA_SHA512)))
    {
        return PROTOCORE_X509_SIG_RSA_SHA512;
    }
    if (oid_is(der, len, alg, OID_RSASSA_PSS, sizeof(OID_RSASSA_PSS)))
    {
        return PROTOCORE_X509_SIG_RSA_PSS;
    }
    if (oid_is(der, len, alg, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)))
    {
        return PROTOCORE_X509_SIG_ECDSA_SHA256;
    }
    if (oid_is(der, len, alg, OID_ECDSA_SHA384, sizeof(OID_ECDSA_SHA384)))
    {
        return PROTOCORE_X509_SIG_ECDSA_SHA384;
    }
    if (oid_is(der, len, alg, OID_ED25519, sizeof(OID_ED25519)))
    {
        return PROTOCORE_X509_SIG_ED25519;
    }
    return PROTOCORE_X509_SIG_UNKNOWN;
}

// SubjectPublicKeyInfo (sec 4.1.2.7): the algorithm, and the key octets under it.
static proto_bool spki_read(const uint8_t *der, size_t len, size_t pos, X509Cert *out)
{
    if (!at(der, len, pos))
    {
        return PROTO_FALSE;
    }
    out->spki.p = der + pos;
    out->spki.len = Der.tlv.next - pos;
    const size_t inner = (size_t)(Der.tlv.content - der);

    // AlgorithmIdentifier first.
    if (!at(der, len, inner))
    {
        return PROTO_FALSE;
    }
    const size_t alg_seq = inner;
    const size_t after_alg = Der.tlv.next;
    const size_t alg_oid = (size_t)(Der.tlv.content - der);

    if (oid_is(der, len, alg_oid, OID_ED25519, sizeof(OID_ED25519)))
    {
        out->key_alg = PROTOCORE_X509_KEY_ED25519;
    }
    else if (oid_is(der, len, alg_oid, OID_RSA_ENCRYPTION, sizeof(OID_RSA_ENCRYPTION)))
    {
        out->key_alg = PROTOCORE_X509_KEY_RSA;
    }
    else if (oid_is(der, len, alg_oid, OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY)))
    {
        // RFC 5480 sec 2.1.1: for id-ecPublicKey the parameters name the curve, and a key on a
        // curve this build does not implement is not an EC key it can use.
        if (!at(der, len, alg_oid))
        {
            return PROTO_FALSE;
        }
        const size_t params = Der.tlv.next;
        out->key_alg = (params < alg_seq + out->spki.len &&
                        oid_is(der, len, params, OID_SECP256R1, sizeof(OID_SECP256R1)))
                           ? PROTOCORE_X509_KEY_EC_P256
                           : PROTOCORE_X509_KEY_UNKNOWN;
    }
    else
    {
        out->key_alg = PROTOCORE_X509_KEY_UNKNOWN;
    }

    // subjectPublicKey BIT STRING.
    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = after_alg;
    Der.bitstring(NULL);
    if (!Der.ok)
    {
        return PROTO_FALSE;
    }
    out->key.p = Der.tlv.content;
    out->key.len = Der.tlv.len;
    return PROTO_TRUE;
}

// One Extension (sec 4.2): { extnID OID, critical BOOLEAN DEFAULT FALSE, extnValue OCTET STRING }.
static proto_bool extension_read(const uint8_t *der, size_t len, size_t pos, X509Cert *out)
{
    if (!at(der, len, pos))
    {
        return PROTO_FALSE;
    }
    const size_t end = Der.tlv.next;
    size_t p = (size_t)(Der.tlv.content - der);

    if (!at(der, len, p))
    {
        return PROTO_FALSE;
    }
    const size_t oid_pos = p;
    p = Der.tlv.next;

    // critical is DEFAULT FALSE, so it is present only when TRUE (X.690 sec 11.5).
    if (p < end && at(der, len, p) && Der.tlv.tag == PROTOCORE_DER_BOOLEAN)
    {
        p = Der.tlv.next;
    }

    if (!at(der, len, p) || Der.tlv.tag != PROTOCORE_DER_OCTET_STRING)
    {
        return PROTO_FALSE;
    }
    const uint8_t *val = Der.tlv.content;
    const size_t val_len = Der.tlv.len;
    const size_t val_pos = (size_t)(val - der);

    if (oid_is(der, len, oid_pos, OID_BASIC_CONSTRAINTS, sizeof(OID_BASIC_CONSTRAINTS)))
    {
        // sec 4.2.1.9: BasicConstraints ::= SEQUENCE { cA BOOLEAN DEFAULT FALSE, pathLen INTEGER OPTIONAL }
        out->has_bc = PROTO_TRUE;
        if (!at(der, len, val_pos) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
        {
            return PROTO_FALSE;
        }
        const size_t bc_end = Der.tlv.next;
        size_t q = (size_t)(Der.tlv.content - der);
        if (q < bc_end && at(der, len, q) && Der.tlv.tag == PROTOCORE_DER_BOOLEAN)
        {
            // X.690 sec 11.1: in DER, TRUE is all ones. Any other non-zero is a second encoding.
            if (Der.tlv.len != 1u || (Der.tlv.content[0] != 0x00u && Der.tlv.content[0] != 0xFFu))
            {
                return PROTO_FALSE;
            }
            out->is_ca = (Der.tlv.content[0] == 0xFFu);
            q = Der.tlv.next;
        }
        if (q < bc_end)
        {
            Der.read_args.buf = der;
            Der.read_args.len = len;
            Der.read_args.pos = q;
            Der.uint(NULL);
            if (!Der.ok || Der.u64 > 0xFFFFFFFFULL)
            {
                return PROTO_FALSE;
            }
            out->path_len = (uint32_t)Der.u64;
            out->has_path_len = PROTO_TRUE;
        }
    }
    else if (oid_is(der, len, oid_pos, OID_KEY_USAGE, sizeof(OID_KEY_USAGE)))
    {
        // sec 4.2.1.3: KeyUsage ::= BIT STRING, bit 0 the most significant of the first octet.
        out->has_ku = PROTO_TRUE;
        if (!at(der, len, val_pos) || Der.tlv.tag != PROTOCORE_DER_BIT_STRING || Der.tlv.len < 2u)
        {
            return PROTO_FALSE;
        }
        const uint8_t unused = Der.tlv.content[0];
        if (unused > 7u)
        {
            return PROTO_FALSE;
        }
        uint16_t bits = 0;
        for (size_t i = 1; i < Der.tlv.len && i <= 2u; i++)
        {
            for (uint8_t b = 0; b < 8u; b++)
            {
                const size_t index = (i - 1u) * 8u + b;
                if (Der.tlv.content[i] & (uint8_t)(0x80u >> b))
                {
                    bits |= (uint16_t)(1u << index);
                }
            }
        }
        out->key_usage = bits;
    }
    else if (oid_is(der, len, oid_pos, OID_SUBJECT_ALT_NAME, sizeof(OID_SUBJECT_ALT_NAME)))
    {
        // sec 4.2.1.6: the value is a GeneralNames SEQUENCE. Kept whole and walked by a match.
        out->san.p = val;
        out->san.len = val_len;
    }
    return PROTO_TRUE;
}

static void x509_parse(uint8_t *restrict work)
{
    (void)work;
    X509.ok = PROTO_FALSE;
    mem.set(&X509.cert, 0, sizeof(X509.cert));

    const uint8_t *der = X509.parse_args.der;
    const size_t len = X509.parse_args.len;
    if (der == NULL || len == 0u)
    {
        return;
    }
    X509Cert *c = &X509.cert;

    // Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signatureValue } (sec 4.1)
    if (!at(der, len, 0) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
    {
        return;
    }
    const size_t cert_end = Der.tlv.next;
    size_t p = (size_t)(Der.tlv.content - der);

    // tbsCertificate: kept whole, because that is what the signature covers (sec 4.1.1.2).
    if (!at(der, len, p) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
    {
        return;
    }
    c->tbs.p = der + p;
    c->tbs.len = Der.tlv.next - p;
    const size_t tbs_end = Der.tlv.next;
    size_t t = (size_t)(Der.tlv.content - der);

    // version [0] EXPLICIT Version DEFAULT v1 (sec 4.1.2.1)
    if (!at(der, len, t))
    {
        return;
    }
    if (Der.tlv.tag == PROTOCORE_DER_CONTEXT_CONSTRUCTED(0))
    {
        const size_t after = Der.tlv.next;
        Der.read_args.buf = der;
        Der.read_args.len = len;
        Der.read_args.pos = (size_t)(Der.tlv.content - der);
        Der.uint(NULL);
        if (!Der.ok || Der.u64 > 2u)
        {
            return;
        }
        c->version = (uint8_t)Der.u64;
        t = after;
    }
    else
    {
        c->version = 0; // absent means v1
    }

    // serialNumber (sec 4.1.2.2): up to 20 octets, so it is kept as it was encoded rather than
    // read into a number.
    if (!at(der, len, t) || Der.tlv.tag != PROTOCORE_DER_INTEGER)
    {
        return;
    }
    c->serial.p = Der.tlv.content;
    c->serial.len = Der.tlv.len;
    t = Der.tlv.next;

    // signature: the algorithm inside the TBS. sec 4.1.1.2 requires it to equal the outer one, and
    // the caller compares them - a mismatch is a signature substitution.
    const protocore_x509_sig_alg tbs_sig = sig_alg_at(der, len, t);
    if (!at(der, len, t))
    {
        return;
    }
    t = Der.tlv.next;

    // issuer Name: kept encoded, because a chain matches it against a subject byte for byte.
    if (!at(der, len, t) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
    {
        return;
    }
    c->issuer.p = der + t;
    c->issuer.len = Der.tlv.next - t;
    t = Der.tlv.next;

    // validity ::= SEQUENCE { notBefore Time, notAfter Time } (sec 4.1.2.5)
    if (!at(der, len, t) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
    {
        return;
    }
    const size_t val_end = Der.tlv.next;
    size_t v = (size_t)(Der.tlv.content - der);
    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = v;
    Der.time(NULL);
    if (!Der.ok)
    {
        return;
    }
    c->not_before = Der.u64;
    v = Der.tlv.next;
    Der.read_args.pos = v;
    Der.time(NULL);
    if (!Der.ok)
    {
        return;
    }
    c->not_after = Der.u64;
    t = val_end;

    // subject Name, likewise kept encoded.
    if (!at(der, len, t) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
    {
        return;
    }
    c->subject.p = der + t;
    c->subject.len = Der.tlv.next - t;
    t = Der.tlv.next;

    if (!spki_read(der, len, t, c))
    {
        return;
    }
    if (!at(der, len, t))
    {
        return;
    }
    t = Der.tlv.next;

    // The optional trailing fields: issuerUniqueID [1], subjectUniqueID [2], extensions [3].
    while (t < tbs_end)
    {
        if (!at(der, len, t))
        {
            return;
        }
        const uint8_t tag = Der.tlv.tag;
        const size_t next = Der.tlv.next;
        if (tag == PROTOCORE_DER_CONTEXT_CONSTRUCTED(3))
        {
            // extensions [3] EXPLICIT Extensions, and Extensions is a SEQUENCE OF Extension.
            if (!at(der, len, (size_t)(Der.tlv.content - der)) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
            {
                return;
            }
            const size_t ext_end = Der.tlv.next;
            size_t e = (size_t)(Der.tlv.content - der);
            while (e < ext_end)
            {
                if (!extension_read(der, len, e, c))
                {
                    return;
                }
                if (!at(der, len, e))
                {
                    return;
                }
                e = Der.tlv.next;
            }
        }
        t = next;
    }

    // signatureAlgorithm, then signatureValue.
    const protocore_x509_sig_alg outer = sig_alg_at(der, len, tbs_end);
    if (!at(der, len, tbs_end))
    {
        return;
    }
    // sec 4.1.1.2: the two MUST hold the same algorithm. They differ only when someone re-wrapped a
    // signed body under a scheme it was not signed with.
    if (outer != tbs_sig)
    {
        return;
    }
    c->sig_alg = outer;
    const size_t sig_pos = Der.tlv.next;
    if (sig_pos >= cert_end)
    {
        return;
    }
    Der.read_args.buf = der;
    Der.read_args.len = len;
    Der.read_args.pos = sig_pos;
    Der.bitstring(NULL);
    if (!Der.ok)
    {
        return;
    }
    c->sig.p = Der.tlv.content;
    c->sig.len = Der.tlv.len;

    X509.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// RFC 6125 sec 6.4: does this certificate speak for the name asked for
// ---------------------------------------------------------------------------

static uint8_t lower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

// One presented dNSName against the reference name, case-insensitively (sec 6.4.1), with the
// wildcard rules of sec 6.4.3.
static proto_bool dns_match(const uint8_t *pres, size_t pn, const char *ref, size_t rn)
{
    if (pn == 0u || rn == 0u)
    {
        return PROTO_FALSE;
    }
    // A presented name is a domain name; an embedded NUL is an attempt to end it early for a
    // consumer that reads it as a C string.
    for (size_t i = 0; i < pn; i++)
    {
        if (pres[i] == 0u)
        {
            return PROTO_FALSE;
        }
    }

    size_t star = pn;
    for (size_t i = 0; i < pn; i++)
    {
        if (pres[i] == '*')
        {
            if (star != pn)
            {
                return PROTO_FALSE; // more than one wildcard is not a form this matches
            }
            star = i;
        }
    }

    if (star == pn) // no wildcard: an exact, case-insensitive comparison
    {
        if (pn != rn)
        {
            return PROTO_FALSE;
        }
        for (size_t i = 0; i < pn; i++)
        {
            if (lower(pres[i]) != lower((uint8_t)ref[i]))
            {
                return PROTO_FALSE;
            }
        }
        return PROTO_TRUE;
    }

    // sec 6.4.3 rule 1: the wildcard is in the left-most label and nowhere else.
    size_t first_dot = pn;
    for (size_t i = 0; i < pn; i++)
    {
        if (pres[i] == '.')
        {
            first_dot = i;
            break;
        }
    }
    if (star > first_dot)
    {
        return PROTO_FALSE; // bar.*.example.net
    }
    if (first_dot == pn)
    {
        return PROTO_FALSE; // a wildcard with no domain under it matches everything; refuse it
    }
    // A wildcard must leave at least two labels behind it, or *.com would match every .com name.
    size_t dots = 0;
    for (size_t i = first_dot; i < pn; i++)
    {
        if (pres[i] == '.')
        {
            dots++;
        }
    }
    if (dots < 2u)
    {
        return PROTO_FALSE;
    }

    // sec 6.4.3 rule 2: the wildcard label matches exactly one label of the reference name, so the
    // reference's left-most label is compared against the presented one and the rests must be equal.
    size_t ref_dot = rn;
    for (size_t i = 0; i < rn; i++)
    {
        if (ref[i] == '.')
        {
            ref_dot = i;
            break;
        }
    }
    if (ref_dot == rn)
    {
        return PROTO_FALSE; // the reference has no label under it to match the domain part
    }
    const size_t pres_rest = pn - first_dot;
    const size_t ref_rest = rn - ref_dot;
    if (pres_rest != ref_rest)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < pres_rest; i++)
    {
        if (lower(pres[first_dot + i]) != lower((uint8_t)ref[ref_dot + i]))
        {
            return PROTO_FALSE;
        }
    }

    // sec 6.4.3 rule 3: the wildcard may be part of the label, so the text either side of it must
    // bracket the reference's left-most label.
    const size_t pre_len = star;
    const size_t post_len = first_dot - star - 1u;
    if (pre_len + post_len > ref_dot)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < pre_len; i++)
    {
        if (lower(pres[i]) != lower((uint8_t)ref[i]))
        {
            return PROTO_FALSE;
        }
    }
    for (size_t i = 0; i < post_len; i++)
    {
        if (lower(pres[star + 1u + i]) != lower((uint8_t)ref[ref_dot - post_len + i]))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

static void x509_name_match(uint8_t *restrict work)
{
    (void)work;
    X509.ok = PROTO_FALSE;

    const X509Cert *c = X509.match_args.cert;
    const char *host = X509.match_args.host;
    if (c == NULL || host == NULL)
    {
        return;
    }
    size_t rn = X509.match_args.host_len;
    if (rn == 0u)
    {
        while (host[rn] != '\0')
        {
            rn++;
        }
    }
    if (rn == 0u)
    {
        return;
    }
    // A trailing dot is the same name; the presented identifiers never carry one.
    if (host[rn - 1u] == '.')
    {
        rn--;
    }

    // RFC 6125 sec 6.4.4: with a DNS-ID present the common name MUST NOT be consulted, and this
    // reads only DNS-IDs - so a certificate with no subjectAltName speaks for no name at all.
    if (c->san.p == NULL || c->san.len == 0u)
    {
        return;
    }

    // GeneralNames ::= SEQUENCE OF GeneralName; dNSName is [2] IA5String (sec 4.2.1.6).
    const uint8_t *der = c->san.p;
    const size_t len = c->san.len;
    if (!at(der, len, 0) || Der.tlv.tag != PROTOCORE_DER_SEQUENCE)
    {
        return;
    }
    const size_t end = Der.tlv.next;
    size_t p = (size_t)(Der.tlv.content - der);
    while (p < end)
    {
        if (!at(der, len, p))
        {
            return;
        }
        if (Der.tlv.tag == PROTOCORE_DER_CONTEXT(2) && dns_match(Der.tlv.content, Der.tlv.len, host, rn))
        {
            X509.ok = PROTO_TRUE;
            return;
        }
        p = Der.tlv.next;
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
X509Ns X509 = {
    .parse = x509_parse,
    .name_match = x509_name_match,
};

PROTOCORE_END_DECLS
