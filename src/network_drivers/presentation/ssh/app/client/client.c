// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.c
 * @brief What an application calls to drive and provision the outbound role.
 */

#include "network_drivers/presentation/ssh/app/client/client.h"
#include "crypto/asymmetric/ed25519/ed25519.h" // Ed25519.pubkey: the provisioning key derivation
#include "mmgr/arena/arena.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h"
#include "network_drivers/presentation/ssh/client/client.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/network/network.h"
#include "network_drivers/presentation/ssh/transport/transport/transport.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h"
#include "shared/log/log.h"

#if PROTOCORE_ENABLE_SSH_CLIENT

// Public API
// ---------------------------------------------------------------------------

static void state_get(uint8_t *restrict work)
{
    (void)work;
    SshClient.state(protocore_ssh_client_span());
    SshAppClient.state = SshClient.state_of;
}

static void up(uint8_t *restrict work)
{
    (void)work;
    SshClient.state(protocore_ssh_client_span());
    SshAppClient.ok = SshClient.state_of == PROTOCORE_SSH_CLIENT_UP;
}

// Key derivation for provisioning: the seed's public half, without a connection.
static void pubkey(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *seed = SshAppClient.seed;
    uint8_t *pub = SshAppClient.pub;
    SshClient.crypto_work(protocore_ssh_client_span());
    uint8_t *crypto_work = SshClient.work;
    if (crypto_work == NULL)
    {
        mem.zero(pub, 32);
        return;
    }
    Ed25519.pubkey_args.seed = seed;
    Ed25519.pubkey_args.pub = pub;
    Ed25519.pubkey(crypto_work);
}

#else

static void state_get(uint8_t *restrict work)
{
    (void)work;
    SshAppClient.state = PROTOCORE_SSH_CLIENT_IDLE;
}

static void up(uint8_t *restrict work)
{
    (void)work;
    SshAppClient.ok = PROTO_FALSE;
}

static void pubkey(uint8_t *restrict work)
{
    (void)work;
    mem.zero(SshAppClient.pub, 32);
}

#endif // PROTOCORE_ENABLE_SSH_CLIENT

// Designated, so a member's position in the struct does not decide what it binds to.
SshAppClientNs SshAppClient = {.state_get = state_get, .up = up, .pubkey = pubkey};
