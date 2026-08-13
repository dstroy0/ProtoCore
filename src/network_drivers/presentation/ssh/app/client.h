// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.h
 * @brief What an application calls to drive and provision the outbound role.
 *
 * The client engine is client/client.c; these are the entry points above it - where the connection
 * has got to, and the ssh-ed25519 public half of a seed for RFC 4252 sec 7 provisioning. Nothing
 * here is one of the three components of RFC 4251 sec 1.
 */

#ifndef PROTOCORE_APP_CLIENT_H
#define PROTOCORE_APP_CLIENT_H

#include "network_drivers/presentation/ssh/client/client.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_SSH_CLIENT

/** @brief Current lifecycle state. */

/** @brief True once authenticated and the remote forward is live. */

/**
 * @brief Derive the ssh-ed25519 public key (32 bytes) from a private @p seed.
 *
 * Convenience for provisioning: print/serve this so it can be added to the relay's `authorized_keys`
 * (as `ssh-ed25519 <base64(0x0000000b "ssh-ed25519" 0x00000020 <pub>)>`).
 */

#endif // PROTOCORE_ENABLE_SSH_CLIENT

/** @brief The application face's own state and the calls that reach it, described only in client.c. */
struct SshAppClientInternal;

/**
 * @brief What a sketch asks about the SSH forward.
 *
 * A caller sets the members a call takes, invokes it through ::SshAppClient, and reads the outcome
 * off the same handle.
 *
 * @var SshAppClientNs::seed      the 32-byte signing seed a key derivation starts from
 * @var SshAppClientNs::pub       where its public half is written
 * @var SshAppClientNs::ok        the forward is up
 * @var SshAppClientNs::state     the phase the forward is in
 * @var SshAppClientNs::state_get read that phase
 * @var SshAppClientNs::up        whether it is up
 * @var SshAppClientNs::pubkey    the seed's public half, without a connection
 * @var SshAppClientNs::internal  the face's state and the calls that reach it
 */
typedef struct
{
    const uint8_t *seed;
    uint8_t *pub;

    proto_bool ok;
    protocore_ssh_client_state state;

    void (*state_get)(struct SshAppClientInternal *ctx);
    void (*up)(struct SshAppClientInternal *ctx);
    void (*pubkey)(struct SshAppClientInternal *ctx);

    struct SshAppClientInternal *internal;
} SshAppClientNs;

/** @brief The one symbol this module exports. */
extern SshAppClientNs SshAppClient;

PROTOCORE_END_DECLS

#endif // PROTOCORE_APP_CLIENT_H
