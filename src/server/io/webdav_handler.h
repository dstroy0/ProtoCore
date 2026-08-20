// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webdav_handler.h
 * @brief WebDAV request handling against a mounted filesystem (RFC 4918).
 *
 * The server half of WebDAV: it resolves a request path against a ROUTE_DAV mount, dispatches the
 * method, walks directories for PROPFIND, and streams a PUT body to the file. The wire format it
 * speaks - method classification, Depth parsing, the 207 Multi-Status builder, and the lock table -
 * is the pure codec in network_drivers/application/webdav/webdav.h, which this file calls.
 *
 * dav(), the mount call an application makes, is public API and is declared in protocore.h.
 */

#ifndef PROTOCORE_WEBDAV_HANDLER_H
#define PROTOCORE_WEBDAV_HANDLER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_WEBDAV

PROTOCORE_BEGIN_DECLS

// PROTOCORE_WEBDAV_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief HttpReq, as the caller already knows it. */
struct HttpReq;

/** @brief What try_serve_dav takes: slot_id, req. */
typedef struct
{
    uint8_t slot_id;
    struct HttpReq *req;
} DavTryServeDavArgs;

/**
 * @brief WebDAV request handling against a mounted filesystem (RFC 4918).
 *
 * A caller sets the members a call takes, invokes it through ::Dav with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Dav.try_serve_dav_args.slot_id = ...;
 *   Dav.try_serve_dav_args.req = ...;
 *   Dav.try_serve_dav(work);
 *   // Dav.ok is what the call reports
 *
 * @var DavNs::try_serve_dav_args  what try_serve_dav takes: slot_id, req
 * @var DavNs::ok  a call's true/false outcome
 * @var DavNs::try_serve_dav  if req matches a ROUTE_DAV mount, handle it as WebDAV and return ...
 *
 * @c work is PROTOCORE_WEBDAV_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    DavTryServeDavArgs try_serve_dav_args;

    proto_bool ok;

    void (*const try_serve_dav)(uint8_t *restrict work);
} DavNs;

/** @brief The one symbol this module exports. */
extern DavNs Dav;

/**
 * @brief The PROTOCORE_WEBDAV_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_webdav_handler_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBDAV

#endif // PROTOCORE_WEBDAV_HANDLER_H
