// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proto_handler.h
 * @brief Layer 5 (Session) - per-protocol connection handler dispatch table.
 *
 * Every application protocol (HTTP, Telnet, SSH, and optional services such as
 * MQTT or Modbus) registers one ProtoHandler. The session layer (server_tick)
 * routes each connection event - and the main loop (handle())
 * polls each active slot - through this table by ConnProto, so a new protocol
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

PROTO_BEGIN_DECLS

/**
 * @brief Per-protocol connection event/poll callbacks (Layer 5 dispatch vtable).
 */
typedef struct ProtoHandler
{
    void (*on_accept)(uint8_t slot); ///< EvtType::EVT_CONNECT: a new connection was accepted.
    void (*on_data)(uint8_t slot);   ///< EvtType::EVT_DATA: bytes are available in the slot's rx ring.
    void (*on_close)(uint8_t slot);  ///< EvtType::EVT_DISCONNECT / EvtType::EVT_ERROR: tear down slot state.
    void (*on_poll)(uint8_t slot);   ///< Called for an active slot each handle() loop (nullable).
} ProtoHandler;

/**
 * @brief Install every handler the build compiled in. Defined in server/proto_builtins.c, the
 *        policy list: this layer owns the mechanism and names no protocol.
 */
void proto_register_builtins(void);

/**
 * @brief The protocol registry.
 *
 * @var ProtoRegistryNs::register_builtins  install every handler the build compiled in
 * @var ProtoRegistryNs::add                bind one handler to one protocol
 * @var ProtoRegistryNs::get                the handler for a protocol, or null if none is bound
 */
typedef struct
{
    void (*register_builtins)(void);
    void (*add)(ConnProto proto, const ProtoHandler *h);
    const ProtoHandler *(*get)(ConnProto proto);
} ProtoRegistryNs;

/** @brief The one symbol this module exports. ProtoHandler is the per-protocol record above. */
extern const ProtoRegistryNs Protocols;

PROTO_END_DECLS

#endif // PROTOCORE_PROTO_HANDLER_H
