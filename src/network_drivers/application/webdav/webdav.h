// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webdav.h
 * @brief WebDAV wire format (RFC 4918): method classification, header parsing,
 *        and the 207 Multi-Status XML builder.
 *
 * Mirrors the CoAP/SNMP split: this header declares the pure, host-testable core
 * (no sockets, no filesystem - unit-tested in env:native_webdav). The
 * filesystem-backed request handling (PROPFIND directory walk, PUT/MKCOL/DELETE/
 * COPY/MOVE, GET via the file-serving path) lives in server/webdav_handler.h
 * and runs only on a build with a real filesystem.
 *
 * Scope: class 1 (PROPFIND Depth 0/1, PROPPATCH, PUT, DELETE, MKCOL, COPY, MOVE)
 * plus OPTIONS and class 2 LOCK/UNLOCK, now enforced by a small lock table (see
 * the lock manager below): a locked resource rejects a write that does not present
 * the matching token in its If header (423 Locked). PROPPATCH is answered 207 with every requested property refused 403
 * (read-only live properties, no dead-property store). The filesystem-backed
 * handler streams a PUT body straight to the file (PC's stream-body
 * hook), so uploads are not bounded by BODY_BUF_SIZE.
 */

#ifndef PROTOCORE_WEBDAV_H
#define PROTOCORE_WEBDAV_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_WEBDAV

/** @brief WebDAV request methods recognized by the server. */
typedef enum PROTO_ENUM_PACKED
{
    DAV_M_OPTIONS,
    DAV_M_GET,
    DAV_M_HEAD,
    DAV_M_PUT,
    DAV_M_DELETE,
    DAV_M_PROPFIND,
    DAV_M_PROPPATCH,
    DAV_M_MKCOL,
    DAV_M_COPY,
    DAV_M_MOVE,
    DAV_M_LOCK,
    DAV_M_UNLOCK,
    DAV_M_UNSUPPORTED ///< Anything else - answered 405 Method Not Allowed.
} WebDavMethod;

/** @brief "infinity" Depth value (RFC 4918 §10.2). */
/** @brief Depth: infinity sentinel (a lone constant). */
#define PC_DAV_DEPTH_INFINITY 0x7fffffff

/** @brief Classify an HTTP method token (e.g. "PROPFIND") into a WebDavMethod. */
WebDavMethod pc_webdav_method(const char *m);

/**
 * @brief Parse a Depth header value ("0", "1", or "infinity").
 * @return 0, 1, or PC_DAV_DEPTH_INFINITY; @p dflt when @p depth_hdr is null/empty.
 */
int pc_webdav_depth(const char *depth_hdr, int dflt);

/**
 * @brief XML-escape @p src into @p dst (`&`, `<`, `>`, `"`, `'`).
 * @return length written (NUL-terminated; truncated to fit @p cap).
 */
size_t pc_webdav_xml_escape(char *dst, size_t cap, const char *src);

/**
 * @brief Extract and percent-decode the path of a Destination header.
 *
 * Accepts an absolute URI ("http://host/p/q") or an absolute path ("/p/q"); the
 * scheme + authority are skipped and `%xx` escapes are decoded into @p out.
 * @return false on overflow or a malformed value.
 */
proto_bool pc_webdav_dest_path(const char *destination, char *out, size_t cap);

// 207 Multi-Status incremental builder. Each call appends to a buffer already
// holding @p len bytes and returns the new length, never exceeding @p cap. A
// return value equal to @p len means the fragment did not fit (the caller should
// stop adding entries and close the document).

/** @brief Write the XML prolog and the open <multistatus> element. */
size_t pc_webdav_ms_begin(char *buf, size_t cap, size_t len);

/**
 * @brief Append one <response> describing a resource.
 *
 * @param href           the resource's URL path (XML-escaped here).
 * @param is_collection  true for a directory (emits <collection/>).
 * @param size           content length (files only).
 * @param rfc1123_mtime  Last-Modified string, or "" to omit.
 * @param content_type   MIME type (files only), or "" to omit.
 */
size_t pc_webdav_ms_entry(char *buf, size_t cap, size_t len, const char *href, proto_bool is_collection, uint32_t size,
                          const char *rfc1123_mtime, const char *content_type);

/** @brief Close the <multistatus> element. */
size_t pc_webdav_ms_end(char *buf, size_t cap, size_t len);

/**
 * @brief Build a complete 207 Multi-Status body answering a PROPPATCH.
 *
 * The server has no dead-property store and its live properties are read-only, so
 * every requested property is refused with 403 Forbidden. The property elements
 * are echoed verbatim (self-closed) from the request @p body so the client sees
 * exactly which properties it asked for, namespace declarations intact; a property
 * whose tag would contain a stray '<' is skipped (no XML injection). Up to
 * PC_WEBDAV_MAX_PROPS properties are echoed.
 *
 * @param buf       destination buffer (whole document, NUL-terminated).
 * @param cap       buffer capacity.
 * @param href      the resource path (XML-escaped here).
 * @param body      the PROPPATCH request body (not required to be NUL-terminated).
 * @param body_len  length of @p body.
 * @return bytes written, or 0 if the document did not fit @p cap.
 */
size_t pc_webdav_proppatch_ms(char *buf, size_t cap, const char *href, const char *body, size_t body_len);

// ── lock manager (RFC 4918 §6-7, class 2) ──────────────────────────────────────────────────────
//
// A small fixed lock table that makes LOCK/UNLOCK enforceable rather than advisory: a locked resource
// rejects a write (PUT / DELETE / MKCOL / MOVE / PROPPATCH) unless the request presents the matching lock
// token in its If header. This is the pure, host-testable core - the handler supplies the token (its own
// RNG) and wires the checks into the mutating methods.

/** @brief Maximum concurrent locks (fixed - a small structural bound, not a per-board tunable). */
#define PC_DAV_LOCK_MAX 8
/** @brief Maximum locked-path length, including the NUL. */
#define PC_DAV_LOCK_PATH_MAX 128
/** @brief Maximum lock-token length, including the NUL (e.g. "opaquelocktoken:xxxxxxxx-pc"). */
#define PC_DAV_LOCK_TOKEN_MAX 48

/** @brief One active lock (RFC 4918 §6.4). */
typedef struct
{
    char path[PC_DAV_LOCK_PATH_MAX];   ///< the locked resource path (trailing slash normalized off)
    char token[PC_DAV_LOCK_TOKEN_MAX]; ///< the lock token (an opaquelocktoken URI)
    proto_bool exclusive;              ///< exclusive-write (true) or shared (false)
    proto_bool depth_infinity;         ///< the lock covers the whole subtree (Depth: infinity) vs just the resource
    proto_bool active;                 ///< false = free slot
    uint32_t expiry_s;                 ///< monotonic second the lock expires (0 = no timeout); swept by _sweep
} DavLock;

/** @brief The server-global lock table (one instance, not per-connection). */
typedef struct
{
    DavLock locks[PC_DAV_LOCK_MAX];
} DavLockTable;

/** @brief Reset a lock table (no locks held). */
void pc_dav_lock_init(DavLockTable *t);

/**
 * @brief Acquire a lock on @p path with the caller-supplied @p token (RFC 4918 §6-7).
 *
 * Fails when a conflicting lock already covers @p path or its subtree - an exclusive request conflicts
 * with any overlapping lock, a shared request only with an overlapping exclusive one - or when the table
 * is full / an argument is bad.
 * @param expiry_s  the monotonic second the lock expires (0 = no timeout); @ref pc_dav_lock_sweep drops
 *                  a lock once now reaches it. The clock stays out of this pure core - the caller passes
 *                  its own now + timeout.
 * @return the stored lock on success, nullptr on conflict / full.
 */
const DavLock *pc_dav_lock_acquire(DavLockTable *t, const char *path, const char *token, proto_bool exclusive,
                                   proto_bool depth_infinity, uint32_t expiry_s);

/**
 * @brief Expire and drop every lock whose timeout has passed (RFC 4918 §6.6). Call before a lock query so
 *        stale locks never gate a write. A lock with @c expiry_s == 0 never expires.
 * @param now_s  the caller's current monotonic second.
 * @return the number of locks dropped.
 */
size_t pc_dav_lock_sweep(DavLockTable *t, uint32_t now_s);

/**
 * @brief Refresh a held lock's timeout to @p new_expiry_s, keyed by @p token (a LOCK refresh, RFC 4918
 *        §9.10.2). @return the refreshed lock, or nullptr if no live lock has that token.
 */
const DavLock *pc_dav_lock_refresh(DavLockTable *t, const char *token, uint32_t new_expiry_s);

/**
 * @brief Find a lock covering @p path: one on @p path itself, or a Depth-infinity lock on an ancestor.
 * @return the covering lock, or nullptr if @p path is unlocked.
 */
const DavLock *pc_dav_lock_find(const DavLockTable *t, const char *path);

/** @brief Release the lock whose token equals @p token (UNLOCK). @return true if one was removed. */
proto_bool pc_dav_lock_release(DavLockTable *t, const char *token);

/**
 * @brief May a write to @p path proceed given the token the request presented (RFC 4918 §7)?
 *
 * Allowed when no lock covers @p path, or when @p presented_token (from the If header, may be nullptr)
 * matches a covering lock. A locked resource with no / wrong token is denied (the handler answers 423).
 */
proto_bool pc_dav_lock_can_write(const DavLockTable *t, const char *path, const char *presented_token);

/**
 * @brief Extract the first lock token from an If header value (RFC 4918 §10.4).
 *
 * Handles the tagged (`<res> (<token>)`) and untagged (`(<token>)`) list forms and a `Not` condition by
 * taking the first Coded-URL inside the first condition list. @return true and fills @p out on success.
 */
proto_bool pc_dav_if_token(const char *if_header, char *out, size_t cap);

#endif // PC_ENABLE_WEBDAV

PROTO_END_DECLS

#endif // PROTOCORE_WEBDAV_H
