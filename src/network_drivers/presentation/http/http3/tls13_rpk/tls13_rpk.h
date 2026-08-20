// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls13_rpk.h
 * @brief The RFC 7250 RawPublicKey credential a TLS 1.3 Certificate message carries.
 *
 * A RawPublicKey Certificate carries a bare DER SubjectPublicKeyInfo where an X.509 chain would go
 * (RFC 7250 sec 3). This module is that credential: the Ed25519 SubjectPublicKeyInfo of RFC 8410
 * sec 4 in both directions, and the Certificate message built over it. The messages themselves,
 * the server_certificate_type extension that selects this credential, and the handshake that
 * negotiates it are tls13_msg's.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS13_RPK_H
#define PROTOCORE_TLS13_RPK_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TLS_RPK

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PROTOCORE_TLS13_ED25519_SPKI_LEN 44 ///< DER SubjectPublicKeyInfo for an Ed25519 key (RFC 8410 sec 4)

/** @brief What ed25519_spki takes: out, cap, pub. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *pub; ///< 32 bytes.
} Tls13RpkEd25519SpkiArgs;
/** @brief What ed25519_from_spki takes: spki, len, pub. */
typedef struct
{
    const uint8_t *spki;
    size_t len;
    const uint8_t **pub;
} Tls13RpkEd25519FromSpkiArgs;
/** @brief What build_certificate takes: out, cap, ed25519_pub. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *ed25519_pub; ///< 32 bytes.
} Tls13RpkBuildCertificateArgs;
/**
 * @brief The RFC 7250 RawPublicKey credential a TLS 1.3 Certificate message carries.
 *
 * A caller sets the members a call takes, invokes it through ::Tls13Rpk with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Tls13Rpk.ed25519_spki_args.out = ...;
 *   Tls13Rpk.ed25519_spki_args.cap = ...;
 *   Tls13Rpk.ed25519_spki_args.pub = ...;
 *   Tls13Rpk.ed25519_spki(work);
 *   // Tls13Rpk.n is what the call reports
 *
 * @var Tls13RpkNs::ed25519_spki_args  what ed25519_spki takes: out, cap, pub
 * @var Tls13RpkNs::ed25519_from_spki_args  what ed25519_from_spki takes: spki, len, pub
 * @var Tls13RpkNs::build_certificate_args  what build_certificate takes: out, cap, ed25519_pub
 * @var Tls13RpkNs::ok  a call's true/false outcome
 * @var Tls13RpkNs::n  the count a call reports
 * @var Tls13RpkNs::ed25519_spki  write the 44-byte DER SubjectPublicKeyInfo for the Ed25519 key pub ...
 * @var Tls13RpkNs::ed25519_from_spki  the 32-byte Ed25519 key inside a DER SubjectPublicKeyInfo (RFC 8410 ...
 * @var Tls13RpkNs::build_certificate  build a Certificate message (RFC 8446 sec 4.4.2) carrying an RFC ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Tls13RpkEd25519SpkiArgs ed25519_spki_args;
    Tls13RpkEd25519FromSpkiArgs ed25519_from_spki_args;
    Tls13RpkBuildCertificateArgs build_certificate_args;
    proto_bool ok;
    size_t n;
} Tls13RpkVars;

/** @brief The operands and the outcome. */
extern Tls13RpkVars Tls13RpkV;

/** @brief The entries. */
typedef struct
{
    void (*const ed25519_spki)(uint8_t *restrict work);
    void (*const ed25519_from_spki)(uint8_t *restrict work);
    void (*const build_certificate)(uint8_t *restrict work);
} Tls13RpkNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Tls13RpkV or a region of the borrow at a fixed offset.
void protocore_tls13_rpk_ed25519_spki(uint8_t *restrict work);
void protocore_tls13_rpk_ed25519_from_spki(uint8_t *restrict work);
void protocore_tls13_rpk_build_certificate(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Tls13Rpk.ed25519_spki(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Tls13RpkNs Tls13Rpk __attribute__((unused)) = {
    .ed25519_spki = protocore_tls13_rpk_ed25519_spki,
    .ed25519_from_spki = protocore_tls13_rpk_ed25519_from_spki,
    .build_certificate = protocore_tls13_rpk_build_certificate,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TLS_RPK

#endif // PROTOCORE_TLS13_RPK_H
