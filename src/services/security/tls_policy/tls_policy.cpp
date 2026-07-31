// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_policy.cpp
 * @brief TLS version negotiation + pinned cipher-suite policy (see tls_policy.h).
 */

#include "services/security/tls_policy/tls_policy.h"

#if PC_ENABLE_TLS_POLICY

uint16_t pc_tls_negotiate_version(uint16_t client_max, uint16_t server_min, uint16_t server_max)
{
    if (server_min > server_max)
    {
        return 0;
    }
    if (client_max < server_min)
    {
        return 0; // the client cannot go as high as we require
    }
    uint16_t chosen = client_max < server_max ? client_max : server_max;
    return chosen;
}

const char *pc_tls_version_name(uint16_t version)
{
    switch (version)
    {
    case TLS_VERSION_1_2:
        return "TLS 1.2";
    case TLS_VERSION_1_3:
        return "TLS 1.3";
    default:
        return "unknown";
    }
}

uint16_t pc_tls_select_cipher(const uint16_t *client_offered, size_t n_client, const uint16_t *server_pinned,
                              size_t n_server)
{
    if (!client_offered || !server_pinned)
    {
        return 0;
    }
    // Server preference: walk the pinned list in order, take the first the client also offered.
    for (size_t i = 0; i < n_server; i++)
    {
        for (size_t j = 0; j < n_client; j++)
        {
            if (server_pinned[i] == client_offered[j])
            {
                return server_pinned[i];
            }
        }
    }
    return 0;
}

bool pc_tls_is_aead(uint16_t suite)
{
    switch (suite)
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
        return true;
    default:
        return false;
    }
}

#endif // PC_ENABLE_TLS_POLICY
