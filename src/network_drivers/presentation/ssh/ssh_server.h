// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_server.h
 * @brief SSH message dispatcher - ties the transport, auth, and channel layers
 *        into one server state machine.
 *
 * The dispatcher is transport-agnostic: it consumes decrypted SSH message
 * payloads (as produced by ssh_pkt_recv) and emits response payloads through a
 * callback. The integration layer wires the callback to ssh_pkt_send + the TCP
 * socket, so this module stays free of lwIP and is fully unit-testable.
 *
 * Lifecycle: banner exchange (handled by ssh_transport_recv_banner) → KEXINIT →
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
 * The integration wraps this with ssh_pkt_send() (which frames, encrypts, and
 * MACs the payload once keys are active) and writes the result to the socket.
 */
typedef void (*SshEmitCb)(uint8_t slot, const uint8_t *payload, size_t len);

/** @brief Install the outbound-message callback. */
void pc_ssh_server_set_emit_cb(SshEmitCb cb);

/**
 * @brief Dispatch one decrypted inbound SSH message for slot @p i.
 *
 * Routes by message type and handshake phase, driving the handshake and, once
 * open, the channel protocol. Responses are produced via the emit callback.
 *
 * @param[in] i        SSH slot.
 * @param[in] msg_type First payload byte (SSH message number).
 * @param[in] payload  Full message payload (including @p msg_type at [0]).
 * @param[in] len      Payload length.
 * @return 0 if handled, -1 if the connection must be closed.
 */
int pc_ssh_server_dispatch(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len);

#endif // PROTOCORE_SSH_SERVER_H
