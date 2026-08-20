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
 */
typedef struct
{
    uint16_t bind_port;
    int handle;
    int i32;
    const struct ProtoHandler *handler;
} SshServerVars;

/** @brief The operands and the outcome. */
extern SshServerVars SshServerV;

/** @brief The entries. */
typedef struct
{
    void (*const rfwd_listener_open)(uint8_t *restrict work);
    void (*const rfwd_listener_close)(uint8_t *restrict work);
    void (*const proto_handler)(uint8_t *restrict work);
    void (*const rfwd_proto_handler)(uint8_t *restrict work);
} SshServerNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SshServerV or a region of the borrow at a fixed offset.
void protocore_server_rfwd_listener_open(uint8_t *restrict work);
void protocore_server_rfwd_listener_close(uint8_t *restrict work);
void protocore_server_proto_handler(uint8_t *restrict work);
void protocore_server_rfwd_proto_handler(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SshServer.rfwd_listener_open(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SshServerNs SshServer __attribute__((unused)) = {
    .rfwd_listener_open = protocore_server_rfwd_listener_open,
    .rfwd_listener_close = protocore_server_rfwd_listener_close,
    .proto_handler = protocore_server_proto_handler,
    .rfwd_proto_handler = protocore_server_rfwd_proto_handler,
};

/**
 * @brief The PROTOCORE_SSH_SERVER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_ssh_server_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_SERVER_SERVER_H
