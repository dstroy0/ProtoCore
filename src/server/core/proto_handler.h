// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proto_handler.h
 * @brief Core server - per-protocol connection handler dispatch table.
 *
 * Every application protocol (HTTP, Telnet, SSH, and optional services such as
 * MQTT or Modbus) registers one ProtoHandler. The server tick (server_tick)
 * routes each connection event - and the main loop (handle())
 * polls each active slot - through this table by ProtoConn, so a new protocol
 * plugs in by registering a handler instead of editing the dispatchers.
 *
 * All callbacks are nullable, run on the main-loop task, and take the affected
 * connection slot index. The built-in HTTP/Telnet/SSH handlers are registered
 * lazily on first lookup, so dispatch works even before begin() (the native
 * test harness drives server_tick() directly).
 */

#ifndef PROTOCORE_PROTO_HANDLER_H
#define PROTOCORE_PROTO_HANDLER_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Per-protocol connection event/poll callbacks (the server's dispatch vtable).
 */
typedef struct ProtoHandler
{
    void (*on_accept)(uint8_t slot); ///< EvtType::EVT_CONNECT: a new connection was accepted.
    void (*on_data)(uint8_t slot);   ///< EvtType::EVT_DATA: bytes are available in the slot's rx ring.
    void (*on_close)(uint8_t slot);  ///< EvtType::EVT_DISCONNECT: the peer closed normally.
    /// EvtType::EVT_ERROR: the connection was aborted, not closed. RFC 9293 sec 3.6 MUST-12 - "the
    /// local application MUST be informed whether it closed normally or was aborted" - so the two
    /// arrive separately. Null falls back to on_close, which cannot tell them apart.
    void (*on_abort)(uint8_t slot);
    void (*on_poll)(uint8_t slot); ///< Called for an active slot each handle() loop (nullable).
} ProtoHandler;

/**
 * @brief Install every handler the build compiled in. Defined in server/protocore_builtins.c, the
 *        policy list: this layer owns the mechanism and names no protocol.
 */
void protocore_register_builtins(void);

/** @brief The registry's own table and the calls that reach it, described only in session.c. */
struct ProtoRegistryInternal;

/**
 * @brief The protocol registry.
 *
 * A caller sets the members a call takes, invokes it through ::Protocols, and reads the outcome off
 * the same handle. The table itself is behind @ref internal.
 *
 * @var ProtoRegistryNs::proto              the protocol a call names
 * @var ProtoRegistryNs::h                  the handler an add binds to it
 * @var ProtoRegistryNs::handler            the handler a lookup reports, or NULL when none is bound
 * @var ProtoRegistryNs::register_builtins  install every handler the build compiled in
 * @var ProtoRegistryNs::add                bind one handler to one protocol
 * @var ProtoRegistryNs::get                the handler for a protocol, or null if none is bound
 * @var ProtoRegistryNs::internal           the table and the calls that reach it
 */
typedef struct
{
    ProtoConn proto;
    const ProtoHandler *h;

    const ProtoHandler *handler;

    void (*register_builtins)(struct ProtoRegistryInternal *ctx);
    void (*add)(struct ProtoRegistryInternal *ctx);
    void (*get)(struct ProtoRegistryInternal *ctx);

    struct ProtoRegistryInternal *internal;
} ProtoRegistryNs;

/** @brief The one symbol this module exports. ProtoHandler is the per-protocol record above. */
extern ProtoRegistryNs Protocols;

PROTOCORE_END_DECLS

#endif // PROTOCORE_PROTO_HANDLER_H
