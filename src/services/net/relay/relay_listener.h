// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay_listener.h
 * @brief Server-side TCP relay / DNAT listener (PC_ENABLE_RELAY) - publish an internal
 *        `host:port` on a server port.
 *
 * Wires the pure relay engine (relay.h) into the server: an inbound connection accepted on a
 * published port is bridged to an origin (an outbound `pc_client` connection to the internal
 * service). A ConnProto::PROTO_RELAY connection handler opens the origin on accept, pumps bytes both ways each
 * poll (via pc_relay_step), and tears both down on close - the DNAT return path is automatic.
 *
 * Usage (opt-in twice: compiled out by default, and inert until you publish a port):
 * @code
 *   int32_t li = server.listen(8080, ConnProto::PROTO_RELAY);   // front port 8080
 *   pc_relay_publish((uint8_t)li, "192.168.1.60", 80);  // -> internal 192.168.1.60:80
 * @endcode
 *
 * Security: this is an open forward to whatever origin you publish - only publish trusted internal
 * targets, and do not expose the front port to an untrusted network without an upstream ACL.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RELAY_LISTENER_H
#define PROTOCORE_RELAY_LISTENER_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_RELAY

/**
 * @brief Bind a published listener to an origin. Call after `server.listen(port, ConnProto::PROTO_RELAY)` with
 *        the returned listener id; installs the ConnProto::PROTO_RELAY handler on the first call.
 * @param listener_id  the id returned by `server.listen(...)`.
 * @param origin_host  the internal host to forward to (dotted-quad or a name; copied).
 * @param origin_port  the internal port.
 * @return true; false if the origin host is null/too long or the bind table is full
 *         (PC_RELAY_MAX_PUBLISH).
 */
proto_bool pc_relay_publish(uint8_t listener_id, const char *origin_host, uint16_t origin_port);

/** @brief Clear all published binds and active bridges (start from empty). */
void pc_relay_listener_reset(void);

#endif // PC_ENABLE_RELAY

PROTO_END_DECLS

#endif // PROTOCORE_RELAY_LISTENER_H
