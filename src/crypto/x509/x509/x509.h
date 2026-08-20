// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file x509.h
 * @brief RFC 5280 certificates: reading one out of the caller's DER (PROTOCORE_ENABLE_X509).
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
 * chain needs a trust anchor and a clock, so both sit above this. What a certificate SAYS - the
 * algorithm identifiers and @ref X509Cert itself - is crypto/x509/x509_types, because a build
 * that authenticates by raw public key needs those words without needing this parser.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_X509_H
#define PROTOCORE_X509_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_X509

#include "crypto/x509/x509_types/x509_types.h" // X509Cert: what a parse fills in

PROTOCORE_BEGIN_DECLS

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

#endif // PROTOCORE_ENABLE_X509

#endif // PROTOCORE_X509_H
