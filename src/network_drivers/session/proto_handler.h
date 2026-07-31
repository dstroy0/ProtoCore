// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proto_handler.h
 * @brief Layer 5 (Session) - per-protocol connection handler dispatch table.
 *
 * Every application protocol (HTTP, Telnet, SSH, and optional services such as
 * MQTT or Modbus) registers one ProtoHandler. The session layer (server_tick)
 * routes each connection event - and the main loop (PC::handle())
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
#include <stdint.h>

/**
 * @brief Per-protocol connection event/poll callbacks (Layer 5 dispatch vtable).
 */
struct ProtoHandler
{
    void (*on_accept)(uint8_t slot); ///< EvtType::EVT_CONNECT: a new connection was accepted.
    void (*on_data)(uint8_t slot);   ///< EvtType::EVT_DATA: bytes are available in the slot's rx ring.
    void (*on_close)(uint8_t slot);  ///< EvtType::EVT_DISCONNECT / EvtType::EVT_ERROR: tear down slot state.
    void (*on_poll)(uint8_t slot);   ///< Called for an active slot each handle() loop (nullable).
};

/** @brief Register @p h for protocol @p proto (replaces any previous handler). */
void proto_register(ConnProto proto, const ProtoHandler *h);

/**
 * @brief Register every built-in protocol's handler (the policy list).
 *
 * Defined in proto_builtins.cpp - the one place that knows which protocols exist.
 * Each built-in's handler lives in its own module (http in presentation, ssh in
 * ssh_conn, ...) behind a `*_proto_handler()` accessor; this installs each. The
 * session dispatcher (session.cpp) calls this once (lazily, on first lookup) so it
 * never names a protocol itself. Optional runtime-gated handlers (e.g. the SSH
 * remote-forward listener) self-register at their own opt-in entry point instead.
 */
void proto_register_builtins(void);

/**
 * @brief Look up the handler for @p proto.
 * @return the registered handler, or nullptr if @p proto is ConnProto::PROTO_NONE or has
 *         no registered handler (no implicit fallback; the event is dropped).
 */
const ProtoHandler *proto_get(ConnProto proto);

#endif // PROTOCORE_PROTO_HANDLER_H
