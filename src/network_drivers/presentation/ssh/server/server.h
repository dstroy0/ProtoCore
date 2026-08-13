// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
const struct ProtoHandler *ssh_protocore_handler(void);

#if PROTOCORE_SSH_PORT_FORWARD
/**
 * @brief The ProtoHandler for sockets accepted on a forwarded listener (RFC 4254 sec 7.2).
 *
 * Listening is this role's (sec 4.1); the sec 7.1 bindings that decide which listeners exist are
 * the connection protocol's. The builtins list installs this handler.
 */
const struct ProtoHandler *ssh_protocore_rfwd_handler(void);

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
int ssh_rfwd_listener_open(uint16_t bind_port);

/** @brief Stop accepting on a handle from ssh_rfwd_listener_open(). Open bridges are unaffected. */
void ssh_rfwd_listener_close(int handle);
#endif

PROTOCORE_END_DECLS

#endif // PROTOCORE_SERVER_SERVER_H
