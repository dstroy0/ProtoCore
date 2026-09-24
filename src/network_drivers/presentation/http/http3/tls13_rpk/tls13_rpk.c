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

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

size_t protocore_tls13_rpk_ed25519_spki(uint8_t *work, uint8_t *out, size_t cap, const uint8_t *pub)
{
    (void)work;

    // DER SubjectPublicKeyInfo for id-Ed25519 (RFC 8410 sec 4): a fixed 12-byte prefix - SEQUENCE
    // { SEQUENCE { OID 1.3.101.112 } , BIT STRING (33, 0 unused) } - then the 32-byte public key.
    static const uint8_t PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
    if (cap < PROTOCORE_TLS13_ED25519_SPKI_LEN)
    {
        return 0;
    }
    mem.cpy(out, PREFIX, sizeof(PREFIX));
    mem.cpy(out + sizeof(PREFIX), pub, 32);
    return PROTOCORE_TLS13_ED25519_SPKI_LEN;
}

proto_bool protocore_tls13_rpk_ed25519_from_spki(uint8_t *work, const uint8_t *spki, size_t len, const uint8_t **pub)
{
    (void)work;

    static const uint8_t PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
    if (len != PROTOCORE_TLS13_ED25519_SPKI_LEN || mem.cmp(spki, PREFIX, sizeof(PREFIX)) != 0)
    {
        return PROTO_FALSE;
    }
    *pub = spki + sizeof(PREFIX);
    return PROTO_TRUE;
}

size_t protocore_tls13_rpk_build_certificate(uint8_t *work, uint8_t *out, size_t cap, const uint8_t *ed25519_pub)
{

    uint8_t spki[PROTOCORE_TLS13_ED25519_SPKI_LEN];
    size_t tls13_rpk_n = Tls13Rpk.ed25519_spki(work, spki, sizeof(spki), ed25519_pub);
    if (!tls13_rpk_n)
    {
        return 0;
    }
    Tls13MsgV.build_certificate_args.out = out;
    Tls13MsgV.build_certificate_args.cap = cap;
    Tls13MsgV.build_certificate_args.cert_der = spki;
    Tls13MsgV.build_certificate_args.cert_len = sizeof(spki);
    Tls13Msg.build_certificate(work);
    return Tls13MsgV.n;
}

#endif // PROTOCORE_ENABLE_TLS_RPK
