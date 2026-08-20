// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay_listener.h
 * @brief Server-side TCP relay / DNAT listener (PROTOCORE_ENABLE_RELAY) - publish an internal
 *        `host:port` on a server port.
 *
 * Wires the pure relay engine (relay.h) into the server: an inbound connection accepted on a
 * published port is bridged to an origin (an outbound `protocore_client` connection to the internal
 * service). A ProtoConn::PROTO_RELAY connection handler opens the origin on accept, pumps bytes both ways each
 * poll (via protocore_relay_step), and tears both down on close - the DNAT return path is automatic.
 *
 * Usage (opt-in twice: compiled out by default, and inert until you publish a port):
 * @code
 *   int32_t li = server.listen(8080, ProtoConn::PROTO_RELAY);   // front port 8080
 *   protocore_relay_publish((uint8_t)li, "192.168.1.60", 80);  // -> internal 192.168.1.60:80
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RELAY

PROTOCORE_BEGIN_DECLS

// PROTOCORE_RELAY_LISTENER_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief What publish takes: listener_id, origin_host, origin_port. */
typedef struct
{
    uint8_t listener_id;     ///< the id returned by `server.listen(...)`
    const char *origin_host; ///< the internal host to forward to (dotted-quad or a name; copied)
    uint16_t origin_port;    ///< the internal port
} RelayListenerPublishArgs;

/**
 * @brief Server-side TCP relay / DNAT listener (PROTOCORE_ENABLE_RELAY) - publish an internal `host:port` on a server
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::RelayListener with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   RelayListener.publish_args.listener_id = ...;
 *   RelayListener.publish_args.origin_host = ...;
 *   RelayListener.publish_args.origin_port = ...;
 *   RelayListener.publish(work);
 *   // RelayListener.ok is what the call reports
 *
 * @var RelayListenerNs::publish_args  what publish takes: listener_id, origin_host, origin_port
 * @var RelayListenerNs::ok  true; false if the origin host is null/too long or the bind table ...
 * @var RelayListenerNs::publish  bind a published listener to an origin. Call after ...
 * @var RelayListenerNs::reset  clear all published binds and active bridges (start from empty)
 *
 * @c work is PROTOCORE_RELAY_LISTENER_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    RelayListenerPublishArgs publish_args;

    proto_bool ok;

    void (*const publish)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
} RelayListenerNs;

/** @brief The one symbol this module exports. */
extern RelayListenerNs RelayListener;

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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RELAY

#endif // PROTOCORE_RELAY_LISTENER_H
