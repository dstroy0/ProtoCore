// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_forward.h
 * @brief SSH direct-tcpip port-forwarding owner (the `ssh -L` target side).
 *
 * The forwarding owner that the channel codec's forward seam
 * (SshChannels.set_forward_open_cb / _data_cb) plugs into. The codec parses a
 * direct-tcpip request and routes channel data; this layer does the actual I/O -
 * it opens the outbound TCP connection through the client transport (pc_client)
 * and bridges bytes both ways - so no socket code leaks into the codec. One fixed
 * table maps each forward channel to a client-transport connection; all storage is
 * static (no heap). Compiled only when PC_SSH_PORT_FORWARD is set.
 *
 * Security: any authenticated client can ask the server to connect anywhere (an
 * open proxy / SSRF surface), so forwarding is opt-in twice over - compiled out by
 * default, and inert until the application calls SshForward.begin(). Install a
 * policy callback to restrict the reachable targets.
 */

#ifndef PROTOCORE_SSH_FORWARD_H
#define PROTOCORE_SSH_FORWARD_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_SSH_PORT_FORWARD

/**
 * @brief Allow/deny policy for a forward target. Return true to permit the connect.
 *
 * @p host is NUL-terminated. If no policy is installed every post-authentication
 * forward is permitted (an open proxy for authenticated users) - install one to
 * restrict the reachable host:port set.
 */
typedef proto_bool (*SshForwardPolicyCb)(const char *host, uint16_t port);

/**
 * @brief TCP forwarding (RFC 4254 sec 7): the policy an application supplies, and the pump that moves
 * a forwarded channel.
 *
 * @var SshForwardNs::set_policy_cb  Install the forward-target policy (optional; default permits all)
 * @var SshForwardNs::begin          Enable direct-tcpip forwarding: install the channel forward callbacks
 * @var SshForwardNs::pump           Pump every forward on SSH connection @p ssh_slot: move buffered target
 *                                   bytes to the client (bounded by the channel's peer window) and propagate a
 *                                   close from either side. Called from the SSH connection poll each loop
 * @var SshForwardNs::reset          Tear down all forwards on @p ssh_slot (its SSH connection is closing)
 */
typedef struct
{
    void (*set_policy_cb)(SshForwardPolicyCb cb);
    void (*begin)(void);
    void (*pump)(uint8_t ssh_slot);
    void (*reset)(uint8_t ssh_slot);
} SshForwardNs;

/** @brief The one symbol this module exports. */
extern const SshForwardNs SshForward;

#endif // PC_SSH_PORT_FORWARD

PROTO_END_DECLS

#endif // PROTOCORE_SSH_FORWARD_H
