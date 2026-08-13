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
protocore_ssh_client_state protocore_ssh_client_state_get(void);

/** @brief True once authenticated and the remote forward is live. */
proto_bool protocore_ssh_client_up(void);

/**
 * @brief Derive the ssh-ed25519 public key (32 bytes) from a private @p seed.
 *
 * Convenience for provisioning: print/serve this so it can be added to the relay's `authorized_keys`
 * (as `ssh-ed25519 <base64(0x0000000b "ssh-ed25519" 0x00000020 <pub>)>`).
 */
void protocore_ssh_client_pubkey(const uint8_t seed[32], uint8_t pub[32]);

#endif // PROTOCORE_ENABLE_SSH_CLIENT

PROTOCORE_END_DECLS

#endif // PROTOCORE_APP_CLIENT_H
