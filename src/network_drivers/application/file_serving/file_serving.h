// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file file_serving.h
 * @brief Layer 7 static file responses: open a path on a mount and page it out over a slot.
 *
 * serve_file_internal() opens the file and writes the response head; file_send_pump() writes one
 * send-buffer window per call until the file is drained, so a response larger than the buffer is
 * spread across dispatches instead of held in one. pc_file_holds_slot() reports whether a slot is
 * mid-paging.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FILE_SERVING_H
#define PROTOCORE_FILE_SERVING_H

#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq
#include "network_drivers/presentation/http/route/http_route.h"        // HttpRoute (by pointer)
#include "protocore_config.h"
#include "server/filesystem/mnt.h" // pc_mnt_backend

PROTO_BEGIN_DECLS

/** @brief Format @p epoch as an RFC 1123 GMT date into @p out (cap bytes); @p out is emptied for epoch <= 0. */
void http_rfc1123(int64_t epoch, char *out, size_t cap);

#if PC_ENABLE_FILE_SERVING

/** @brief Dispatch a ROUTE_STATIC match: resolve the FS path and serve it (MIME/index/gzip). */
void serve_static_request(uint8_t slot_id, HttpReq *req, const HttpRoute *r);

/**
 * @brief Open @p fs_path on @p file_sys and stream it as 200 with the given type and optional
 *        Content-Encoding. A null @p file_sys means whatever is mounted.
 */
void serve_file_internal(uint8_t slot_id, proto_bool head, const pc_mnt_backend *file_sys, const char *fs_path,
                         const char *content_type, const char *content_encoding);

/** @brief Resume a pending file response: page out one send-buffer window, finishing when drained. */
void file_send_pump(uint8_t slot_id);

/** @brief True while a file response is paging out on @p slot. */
proto_bool pc_file_holds_slot(uint8_t slot);

/**
 * @brief Serve a file from the mounted volume.
 *
 * Opens @p fs_path through the filesystem accessor, sends HTTP 200 with the appropriate headers
 * (Content-Type, Content-Length), and streams the file body in FILE_CHUNK_SIZE chunks. Sends 404 if
 * the file cannot be opened.
 *
 * @param slot_id      Connection slot index.
 * @param file_sys     Backend to read from; NULL uses whatever is mounted (the board's).
 * @param fs_path      Request path to the file, resolved against the mount root.
 * @param content_type MIME type string, e.g. "text/html".
 */
void serve_file(uint8_t slot_id, const pc_mnt_backend *file_sys, const char *fs_path, const char *content_type);

/**
 * @brief Mount a filesystem subtree at a URL prefix (one-call static serving).
 *
 * Registers a wildcard route so every request under @p url_prefix is served from @p fs_root on the
 * mounted volume. The request path beyond the prefix is appended to @p fs_root; a request ending in
 * `/` (or exactly the prefix) serves `index.html`. Content-Type is auto-detected from the extension
 * (see mime_type()). If the client sends `Accept-Encoding: gzip` and a `<path>.gz` exists, the
 * pre-compressed file is served with `Content-Encoding: gzip`. Paths containing `..` are rejected
 * (404). Only GET and HEAD are served; other methods get 405.
 *
 * @code
 * serve_static("/", NULL, "/www");          // the board's own storage
 * serve_static("/ram/", pc_mnt_ram(), "/"); // or any backend that satisfies our vtable
 * @endcode
 *
 * @param url_prefix  URL prefix to mount (with or without a trailing `*`).
 * @param file_sys    Backend to serve from; NULL uses whatever is mounted (the board's).
 * @param fs_root     Subtree on that backend (persistent string).
 */
void serve_static(const char *url_prefix, const pc_mnt_backend *file_sys, const char *fs_root);

#endif // PC_ENABLE_FILE_SERVING

PROTO_END_DECLS

#endif // PROTOCORE_FILE_SERVING_H
