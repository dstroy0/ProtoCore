// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h"
#include "shared/log/log.h"

#if PROTOCORE_ENABLE_SSH_CLIENT

// Public API
// ---------------------------------------------------------------------------

/**
 * @brief The application face of the SSH client: what a sketch asks about the forward.
 *
 * @var SshAppClientInternal::ns  the handle a caller reads a call's outcome off
 */
struct SshAppClientInternal
{
    SshAppClientNs *ns;
};

static struct SshAppClientInternal s_app = {.ns = &SshAppClient};

static void state_get(struct SshAppClientInternal *restrict ctx)
{
    SshClient.state(SshClient.internal);
    ctx->ns->state = SshClient.state_of;
}

static void up(struct SshAppClientInternal *restrict ctx)
{
    SshClient.state(SshClient.internal);
    ctx->ns->ok = SshClient.state_of == PROTOCORE_SSH_CLIENT_UP;
}

// Key derivation for provisioning: the seed's public half, without a connection.
static void pubkey(struct SshAppClientInternal *restrict ctx)
{
    const uint8_t *seed = ctx->ns->seed;
    uint8_t *pub = ctx->ns->pub;
    SshClient.crypto_work(SshClient.internal);
    uint8_t *work = SshClient.work;
    if (work == NULL)
    {
        mem.zero(pub, 32);
        return;
    }
    protocore_ed25519_pubkey(work, pub, seed);
}

#else

/** @brief The application face - what SshAppClientNs points at, with no client to ask about. */
struct SshAppClientInternal
{
    SshAppClientNs *ns;
};

static struct SshAppClientInternal s_app = {.ns = &SshAppClient};

static void state_get(struct SshAppClientInternal *restrict ctx)
{
    ctx->ns->state = PROTOCORE_SSH_CLIENT_IDLE;
}

static void up(struct SshAppClientInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}

static void pubkey(struct SshAppClientInternal *restrict ctx)
{
    mem.zero(ctx->ns->pub, 32);
}

#endif // PROTOCORE_ENABLE_SSH_CLIENT

// Designated, so a member's position in the struct does not decide what it binds to.
SshAppClientNs SshAppClient = {.state_get = state_get, .up = up, .pubkey = pubkey, .internal = &s_app};
