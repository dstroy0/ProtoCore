// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_server.h
 * @brief SSH message dispatcher - ties the transport, auth, and channel layers
 *        into one server state machine.
 *
 * The dispatcher is transport-agnostic: it consumes decrypted SSH message
 * payloads (as produced by SshPacket.recv) and emits response payloads through a
 * callback. The integration layer wires the callback to SshPacket.send + the TCP
 * socket, so this module stays free of lwIP and is fully unit-testable.
 *
 * Lifecycle: banner exchange (handled by SshTransport.recv_banner) → KEXINIT →
 * KEXDH → NEWKEYS → ssh-userauth → connection/channel protocol, with in-session
 * re-keys handled transparently.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_SERVER_H
#define PROTOCORE_SSH_SERVER_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Emit one outbound SSH message payload for slot @p slot.
 *
 * The integration wraps this with SshPacket.send() (which frames, encrypts, and
 * MACs the payload once keys are active) and writes the result to the socket.
 */
typedef void (*SshEmitCb)(uint8_t slot, const uint8_t *payload, size_t len);

/**
 * @brief The server role: the inbound message router, and the callback it writes replies through.
 *
 * @var SshServerNs::set_emit_cb  Install the outbound-message callback
 * @var SshServerNs::dispatch     Dispatch one decrypted inbound SSH message for slot @p i
 */
typedef struct
{
    void (*set_emit_cb)(SshEmitCb cb);
    int (*dispatch)(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len);
} SshServerNs;

/** @brief The one symbol this module exports. */
extern const SshServerNs SshServer;

#endif // PROTOCORE_SSH_SERVER_H
