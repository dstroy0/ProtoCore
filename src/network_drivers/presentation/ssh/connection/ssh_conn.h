// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_conn.h
 * @brief Glue between the TCP transport (conn_pool) and the SSH protocol stack.
 *
 * Binds a ConnProto::PROTO_SSH TcpConn slot to an SSH session slot, pumps ring-buffer
 * bytes through the banner exchange and binary-packet layer, and writes the
 * dispatcher's outbound packets back to the socket. This is the integration
 * layer the session loop calls for ConnProto::PROTO_SSH connections.
 */

#ifndef PROTOCORE_SSH_CONN_H
#define PROTOCORE_SSH_CONN_H

#include <stddef.h>
#include <stdint.h>

/** @brief The SSH connection ProtoHandler (accessor; installed by the builtins list, no session dep). */
struct ProtoHandler;

/**
 * @brief The session layer's seam: the four arms a PROTO_SSH slot is turned through, the two an
 * application sends on, and the ProtoHandler the builtins list installs.
 *
 * @var SshProtoNs::setup           One-time setup: install the dispatcher's binary-packet emit callback
 * @var SshProtoNs::accept          Handle a new ConnProto::PROTO_SSH connection on @p conn_slot
 * @var SshProtoNs::rx              Drain @p conn_slot's receive ring buffer through the SSH stack
 * @var SshProtoNs::close           Tear down SSH state for @p conn_slot (disconnect / error)
 * @var SshProtoNs::poll            Per-loop poll hook for an SSH connection (registered as the SSH protocol
 *                                  handler's on_poll). Drives the port-forwarding pump; a no-op when forwarding
 *                                  is compiled out
 * @var SshProtoNs::send            Send application data to the client over an SSH channel
 * @var SshProtoNs::close_channel   Close an SSH channel from the server side: frame CHANNEL_EOF and
 *                                  CHANNEL_CLOSE as two binary packets and write them to the socket
 * @var SshProtoNs::open_forwarded  Open a server-initiated "forwarded-tcpip" channel to the client (ssh -R):
 *                                  build the CHANNEL_OPEN (RFC 4254 §7.2) via the channel codec, frame + send
 *                                  it on @p ssh_slot's socket, and return the new local channel id. The
 *                                  client's CHANNEL_OPEN_CONFIRMATION (or FAILURE) later drives the
 *                                  forward-confirm callback
 * @var SshProtoNs::handler         (see the implementation)
 */
typedef struct
{
    void (*setup)(void);
    void (*accept)(uint8_t conn_slot);
    void (*rx)(uint8_t conn_slot);
    void (*close)(uint8_t conn_slot);
    void (*poll)(uint8_t conn_slot);
    int (*send)(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len);
    int (*close_channel)(uint8_t ssh_slot, uint32_t channel);
    int (*open_forwarded)(uint8_t ssh_slot, const char *conn_addr, uint16_t conn_port, const char *orig_addr,
                          uint16_t orig_port);
    const struct ProtoHandler *(*handler)(void);
} SshProtoNs;

/** @brief The one symbol this module exports. */
extern const SshProtoNs SshProto;

#endif // PROTOCORE_SSH_CONN_H
