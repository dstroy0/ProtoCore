// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "network_drivers/network/route.h"                             // Route (by pointer)
#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq
#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_WEBDAV

/** @brief If @p req matches a ROUTE_DAV mount, handle it as WebDAV and return true. */
proto_bool try_serve_dav(uint8_t slot_id, HttpReq *req);

/** @brief Dispatch a WebDAV request against the mount @p r (resolves the FS path, then the method). */
void serve_dav_request(uint8_t slot_id, HttpReq *req, const Route *r);

/** @brief Send a bodyless WebDAV status with optional extra header lines (each ending in CRLF). */
void dav_send_status(uint8_t slot_id, int code, const char *extra_headers);

#if PC_ENABLE_STREAM_BODY

/** @brief Stream-begin hook: if @p req is a PUT under a DAV mount, open the file and stream the body. */
proto_bool dav_stream_put_begin(HttpReq *req);

/** @brief Stream-data hook: write one body chunk to @p req's slot's DAV PUT file. */
void dav_stream_put_data(HttpReq *req, const uint8_t *data, size_t len);

/** @brief Stream-abort hook: close the half-written PUT file if the transfer is torn down early. */
void dav_put_abort_tramp(HttpReq *req);

#endif // PC_ENABLE_STREAM_BODY

#endif // PC_ENABLE_WEBDAV

PROTO_END_DECLS

#endif // PROTOCORE_WEBDAV_HANDLER_H
