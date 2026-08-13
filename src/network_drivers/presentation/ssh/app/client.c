// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.c
 * @brief What an application calls to drive and provision the outbound role.
 */

#include "network_drivers/presentation/ssh/app/client.h"
#include "crypto/asymmetric/ed25519.h" // protocore_ed25519_pubkey(): the provisioning key derivation
#include "mmgr/arena.h"
#include "mmgr/protomem.h"
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/client/client.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/network/network.h"
#include "network_drivers/presentation/ssh/transport/transport.h"
#include "network_drivers/transport/tcp.h"
#include "server/clock/clock.h"
#include "shared_primitives/log.h"

#if PROTOCORE_ENABLE_SSH_CLIENT

// Public API
// ---------------------------------------------------------------------------

protocore_ssh_client_state protocore_ssh_client_state_get(void)
{
    return SshClient.state();
}

proto_bool protocore_ssh_client_up(void)
{
    return SshClient.state() == PROTOCORE_SSH_CLIENT_UP;
}

// Key derivation for provisioning: the seed's public half, without a connection.
void protocore_ssh_client_pubkey(const uint8_t seed[32], uint8_t pub[32])
{
    uint8_t *work = SshClient.crypto_work();
    if (work == NULL)
    {
        mem.zero(pub, 32);
        return;
    }
    protocore_ed25519_pubkey(work, pub, seed);
}

#endif // PROTOCORE_ENABLE_SSH_CLIENT
