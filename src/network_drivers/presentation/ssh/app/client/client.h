// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 */
typedef struct
{
    const uint8_t *seed;
    uint8_t *pub;
    proto_bool ok;
    protocore_ssh_client_state state;
} SshAppClientVars;

/** @brief The operands and the outcome. */
extern SshAppClientVars SshAppClientV;

/** @brief The entries. */
typedef struct
{
    void (*const state_get)(uint8_t *restrict work);
    void (*const up)(uint8_t *restrict work);
    void (*const pubkey)(uint8_t *restrict work);
} SshAppClientNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SshAppClientV or a region of the borrow at a fixed offset.
void protocore_client_state_get(uint8_t *restrict work);
void protocore_client_up(uint8_t *restrict work);
void protocore_client_pubkey(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SshAppClient.state_get(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SshAppClientNs SshAppClient __attribute__((unused)) = {
    .state_get = protocore_client_state_get,
    .up = protocore_client_up,
    .pubkey = protocore_client_pubkey,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_APP_CLIENT_H
