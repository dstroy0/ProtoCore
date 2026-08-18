// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.h
 * @brief The server engine: the handler the session loop installs.
 */

#ifndef PROTOCORE_SERVER_SERVER_H
#define PROTOCORE_SERVER_SERVER_H

#include "network_drivers/presentation/ssh/common.h"


PROTOCORE_BEGIN_DECLS

struct ProtoHandler;

/** @brief The SSH ProtoHandler the builtins list installs. */

#if PROTOCORE_SSH_PORT_FORWARD
/**
 * @brief The ProtoHandler for sockets accepted on a forwarded listener (RFC 4254 sec 7.2).
 *
 * Listening is this role's (sec 4.1); the sec 7.1 bindings that decide which listeners exist are
 * the connection protocol's. The builtins list installs this handler.
 */

/**
 * @brief Start listening on @p bind_port for a sec 7.1 remote forward.
 *
 * "The 'address to bind' and 'port number to bind' specify the IP address... and port on which
 * connections for forwarding are to be accepted." Which bindings exist is the connection protocol's
 * decision; the socket that accepts on one is this role's, so the pool it comes from is here.
 *
 * @return An opaque handle for ssh_rfwd_listener_close() and protocore_ssh_forward_binding(),
 *         or -1 when there is no listener capacity or the port could not be bound.
 */

/** @brief Stop accepting on a handle from ssh_rfwd_listener_open(). Open bridges are unaffected. */
#endif

/** @brief The listening role's own state and the calls that reach it, described only in server.c. */
struct SshServerInternal;

/**
 * @brief The SSH server role (RFC 4253 sec 4.1): the side that accepts a connection.
 *
 * A caller sets the members a call takes, invokes it through ::SshServer, and reads the outcome off
 * the same handle.
 *
 * @var SshServerNs::bind_port  the port a remote forward (RFC 4254 sec 7.1) binds
 * @var SshServerNs::handle     the listener a close releases
 * @var SshServerNs::i32        the listener an open reports, or < 0
 * @var SshServerNs::handler    the ProtoHandler a lookup reports
 * @var SshServerNs::rfwd_listener_open   bind a port for an accepted remote forward
 * @var SshServerNs::rfwd_listener_close  release one
 * @var SshServerNs::proto_handler        the dispatch seam an SSH slot is driven through
 * @var SshServerNs::rfwd_proto_handler   the same for a forwarded slot
 * @var SshServerNs::internal   the role's state and the calls that reach it
 */
typedef struct
{
    uint16_t bind_port;
    int handle;

    int i32;
    const struct ProtoHandler *handler;

    void (*rfwd_listener_open)(struct SshServerInternal *ctx);
    void (*rfwd_listener_close)(struct SshServerInternal *ctx);
    void (*proto_handler)(struct SshServerInternal *ctx);
    void (*rfwd_proto_handler)(struct SshServerInternal *ctx);

    struct SshServerInternal *internal;
} SshServerNs;

/** @brief The one symbol this module exports. */
extern SshServerNs SshServer;

PROTOCORE_END_DECLS

#endif // PROTOCORE_SERVER_SERVER_H
