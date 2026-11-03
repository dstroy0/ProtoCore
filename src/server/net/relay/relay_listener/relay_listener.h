// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_RELAY_LISTENER_H
#define PROTOCORE_RELAY_LISTENER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file relay_listener.h
 * @brief Server-side TCP relay / DNAT listener (PROTOCORE_ENABLE_RELAY) - publish an internal
`host:port` on a server port.
 *
 * Wires the pure relay engine (relay.h) into the server: an inbound connection accepted on a
 * published port is bridged to an origin (an outbound `protocore_client` connection to the internal
 * service). A ProtoConn::PROTO_RELAY connection handler opens the origin on accept, pumps bytes both ways each
 * poll (via protocore_relay_step), and tears both down on close - the DNAT return path is automatic.
 *
 * Usage (opt-in twice: compiled out by default, and inert until you publish a port):
 * @code
 * int32_t li = server.listen(8080, ProtoConn::PROTO_RELAY);   // front port 8080
 * protocore_relay_publish((uint8_t)li, "192.168.1.60", 80);  // -> internal 192.168.1.60:80
 * @endcode
 *
 * Security: this is an open forward to whatever origin you publish - only publish trusted internal
 * targets, and do not expose the front port to an untrusted network without an upstream ACL.
 *
 * @c work is PROTOCORE_RELAY_LISTENER_BORROW bytes the CALLER took, at an address it knows. It is not held past the
call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*publish)(uint8_t *, uint8_t, const char *, uint16_t);
    void (*reset)(uint8_t *);
} RelayListenerNs;
PROTOCORE_NS_LAYOUT(RelayListenerNs, publish, reset);

/**
 * @brief Bind a published listener to an origin. Call after .
 * @param work PROTOCORE_RELAY_LISTENER_BORROW bytes the caller took. Not held past the call.
 * @param listener_id the id returned by `server.listen(...)`
 * @param origin_host the internal host to forward to (dotted-quad or a name; copied)
 * @param origin_port the internal port
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_relay_listener_publish(uint8_t *work, uint8_t listener_id, const char *origin_host,
                                            uint16_t origin_port);
/**
 * @brief Clear all published binds and active bridges (start from empty).
 * @param work PROTOCORE_RELAY_LISTENER_BORROW bytes the caller took. Not held past the call.
 */
void protocore_relay_listener_reset(uint8_t *work);

/**
 * @brief The PROTOCORE_RELAY_LISTENER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_relay_listener_span(void);

/** @brief Module namespace. */
PROTOCORE_NS RelayListenerNs RelayListener PROTOCORE_UNUSED = {.publish = protocore_relay_listener_publish,
                                                               .reset = protocore_relay_listener_reset};

PROTOCORE_END_DECLS

#endif // PROTOCORE_RELAY_LISTENER_H
