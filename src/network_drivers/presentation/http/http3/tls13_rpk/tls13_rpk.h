// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_TLS13_RPK_H
#define PROTOCORE_TLS13_RPK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_TLS13_RPK_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

#define PROTOCORE_TLS13_ED25519_SPKI_LEN 44 ///< DER SubjectPublicKeyInfo for an Ed25519 key (RFC 8410 sec 4)

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*ed25519_spki)(uint8_t *restrict, uint8_t *, size_t, const uint8_t *);
    proto_bool (*ed25519_from_spki)(uint8_t *restrict, const uint8_t *, size_t, const uint8_t **);
    size_t (*build_certificate)(uint8_t *restrict, uint8_t *, size_t, const uint8_t *);
} Tls13RpkNs;
PROTOCORE_NS_LAYOUT(Tls13RpkNs, ed25519_spki, ed25519_from_spki, build_certificate);

/**
 * @brief Write the 44-byte DER SubjectPublicKeyInfo for the Ed25519 key pub .
 * @param work PROTOCORE_TLS13_RPK_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param pub 32 bytes
 * @return The size_t.
 */
size_t protocore_tls13_rpk_ed25519_spki(uint8_t *restrict work, uint8_t *out, size_t cap, const uint8_t *pub);
/**
 * @brief The 32-byte Ed25519 key inside a DER SubjectPublicKeyInfo (RFC 8410 .
 * @param work PROTOCORE_TLS13_RPK_BORROW bytes the caller took. Not held past the call.
 * @param spki Spki
 * @param len Len
 * @param pub Pub
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_tls13_rpk_ed25519_from_spki(uint8_t *restrict work, const uint8_t *spki, size_t len,
                                                 const uint8_t **pub);
/**
 * @brief Build a Certificate message (RFC 8446 sec 4.4.2) carrying an RFC .
 * @param work PROTOCORE_TLS13_RPK_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param ed25519_pub 32 bytes
 * @return The size_t.
 */
size_t protocore_tls13_rpk_build_certificate(uint8_t *restrict work, uint8_t *out, size_t cap,
                                             const uint8_t *ed25519_pub);

/** @brief Module namespace. */
PROTOCORE_NS Tls13RpkNs Tls13Rpk PROTOCORE_UNUSED = {.ed25519_spki = protocore_tls13_rpk_ed25519_spki,
                                                     .ed25519_from_spki = protocore_tls13_rpk_ed25519_from_spki,
                                                     .build_certificate = protocore_tls13_rpk_build_certificate};

PROTOCORE_END_DECLS

#endif // PROTOCORE_TLS13_RPK_H
