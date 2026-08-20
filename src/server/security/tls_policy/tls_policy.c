// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_policy.c
 * @brief TLS version negotiation + pinned cipher-suite policy - implementation. See tls_policy.h.
 *
 * Nothing here carves the borrow, holds state between calls or touches a pool. Every operand is the
 * caller's, so the module needs no bytes of its own and defines no context.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TLS_POLICY

#include "server/security/tls_policy/tls_policy.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// The highest version this server supports that the client can also reach; the borrow goes unread.
void protocore_tls_policy_negotiate(uint8_t *restrict work)
{
    (void)work;
    TlsPolicyV.ok = PROTO_FALSE;
    TlsPolicyV.version = 0;
    const uint16_t client_max = TlsPolicyV.negotiate_args.client_max;
    const uint16_t server_min = TlsPolicyV.negotiate_args.server_min;
    const uint16_t server_max = TlsPolicyV.negotiate_args.server_max;
    if (server_min > server_max)
    {
        return;
    }
    if (client_max < server_min)
    {
        return; // the client cannot go as high as we require
    }
    TlsPolicyV.version = client_max < server_max ? client_max : server_max;
    TlsPolicyV.ok = PROTO_TRUE;
}

void protocore_tls_policy_name(uint8_t *restrict work)
{
    (void)work;
    switch (TlsPolicyV.name_args.version)
    {
    case TLS_VERSION_1_2:
        TlsPolicyV.text = "TLS 1.2";
        break;
    case TLS_VERSION_1_3:
        TlsPolicyV.text = "TLS 1.3";
        break;
    default:
        TlsPolicyV.text = "unknown";
        break;
    }
    TlsPolicyV.ok = PROTO_TRUE;
}

// Server preference: walk the pinned list in order, take the first the client also offered.
void protocore_tls_policy_select(uint8_t *restrict work)
{
    (void)work;
    TlsPolicyV.ok = PROTO_FALSE;
    TlsPolicyV.suite = 0;
    const uint16_t *client_offered = TlsPolicyV.select_args.client_offered;
    const uint16_t *server_pinned = TlsPolicyV.select_args.server_pinned;
    if (!client_offered || !server_pinned)
    {
        return;
    }
    for (size_t i = 0; i < TlsPolicyV.select_args.n_server; i++)
    {
        for (size_t j = 0; j < TlsPolicyV.select_args.n_client; j++)
        {
            if (server_pinned[i] == client_offered[j])
            {
                TlsPolicyV.suite = server_pinned[i];
                TlsPolicyV.ok = PROTO_TRUE;
                return;
            }
        }
    }
}

void protocore_tls_policy_is_aead(uint8_t *restrict work)
{
    (void)work;
    switch (TlsPolicyV.aead_args.suite)
    {
    // TLS 1.3 AEAD suites.
    case 0x1301: // TLS_AES_128_GCM_SHA256
    case 0x1302: // TLS_AES_256_GCM_SHA384
    case 0x1303: // TLS_CHACHA20_POLY1305_SHA256
    // TLS 1.2 ECDHE AEAD suites.
    case 0xC02B: // ECDHE_ECDSA_AES_128_GCM_SHA256
    case 0xC02C: // ECDHE_ECDSA_AES_256_GCM_SHA384
    case 0xC02F: // ECDHE_RSA_AES_128_GCM_SHA256
    case 0xC030: // ECDHE_RSA_AES_256_GCM_SHA384
    case 0xCCA8: // ECDHE_RSA_CHACHA20_POLY1305
    case 0xCCA9: // ECDHE_ECDSA_CHACHA20_POLY1305
        TlsPolicyV.aead = PROTO_TRUE;
        break;
    default:
        TlsPolicyV.aead = PROTO_FALSE;
        break;
    }
    TlsPolicyV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
TlsPolicyVars TlsPolicyV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TLS_POLICY
