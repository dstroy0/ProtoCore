// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls13_rpk.c
 * @brief The RFC 7250 RawPublicKey credential (see tls13_rpk.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TLS_RPK

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/presentation/http/http3/tls13_rpk/tls13_rpk.h"

PROTOCORE_BEGIN_DECLS

static uint8_t tls13_msg_work[16]; // the borrow an entry takes; Tls13Msg never reads it

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void tls13_rpk_ed25519_spki(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13Rpk.ed25519_spki_args.out;
    size_t cap = Tls13Rpk.ed25519_spki_args.cap;
    const uint8_t *pub = Tls13Rpk.ed25519_spki_args.pub;

    // DER SubjectPublicKeyInfo for id-Ed25519 (RFC 8410 sec 4): a fixed 12-byte prefix - SEQUENCE
    // { SEQUENCE { OID 1.3.101.112 } , BIT STRING (33, 0 unused) } - then the 32-byte public key.
    static const uint8_t PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
    if (cap < PROTOCORE_TLS13_ED25519_SPKI_LEN)
    {
        Tls13Rpk.n = 0;
        return;
    }
    mem.cpy(out, PREFIX, sizeof(PREFIX));
    mem.cpy(out + sizeof(PREFIX), pub, 32);
    Tls13Rpk.n = PROTOCORE_TLS13_ED25519_SPKI_LEN;
}

static void tls13_rpk_ed25519_from_spki(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *spki = Tls13Rpk.ed25519_from_spki_args.spki;
    size_t len = Tls13Rpk.ed25519_from_spki_args.len;
    const uint8_t **pub = Tls13Rpk.ed25519_from_spki_args.pub;

    static const uint8_t PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
    if (len != PROTOCORE_TLS13_ED25519_SPKI_LEN || mem.cmp(spki, PREFIX, sizeof(PREFIX)) != 0)
    {
        Tls13Rpk.ok = PROTO_FALSE;
        return;
    }
    *pub = spki + sizeof(PREFIX);
    Tls13Rpk.ok = PROTO_TRUE;
}

static void tls13_rpk_build_certificate(uint8_t *restrict work)
{
    uint8_t *out = Tls13Rpk.build_certificate_args.out;
    size_t cap = Tls13Rpk.build_certificate_args.cap;
    const uint8_t *ed25519_pub = Tls13Rpk.build_certificate_args.ed25519_pub;

    uint8_t spki[PROTOCORE_TLS13_ED25519_SPKI_LEN];
    Tls13Rpk.ed25519_spki_args.out = spki;
    Tls13Rpk.ed25519_spki_args.cap = sizeof(spki);
    Tls13Rpk.ed25519_spki_args.pub = ed25519_pub;
    tls13_rpk_ed25519_spki(work);
    if (!Tls13Rpk.n)
    {
        Tls13Rpk.n = 0;
        return;
    }
    Tls13Msg.build_certificate_args.out = out;
    Tls13Msg.build_certificate_args.cap = cap;
    Tls13Msg.build_certificate_args.cert_der = spki;
    Tls13Msg.build_certificate_args.cert_len = sizeof(spki);
    Tls13Msg.build_certificate(tls13_msg_work);
    Tls13Rpk.n = Tls13Msg.n;
}

Tls13RpkNs Tls13Rpk = {
    .ed25519_spki = tls13_rpk_ed25519_spki,
    .ed25519_from_spki = tls13_rpk_ed25519_from_spki,
    .build_certificate = tls13_rpk_build_certificate,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TLS_RPK
