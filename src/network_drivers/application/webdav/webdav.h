// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webdav.h
 * @brief WebDAV wire format (RFC 4918): method classification, header parsing,
 *        and the 207 Multi-Status XML builder.
 *
 * Mirrors the CoAP/SNMP split: this header declares the pure, host-testable core
 * (no sockets, no filesystem - unit-tested in env:native_webdav). The
 * filesystem-backed request handling (PROPFIND directory walk, PUT/MKCOL/DELETE/
 * COPY/MOVE, GET via the file-serving path) lives in server/io/webdav_handler.h
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_WEBDAV

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Depth: infinity sentinel (a lone constant). */
#define PROTOCORE_DAV_DEPTH_INFINITY 0x7fffffff

/** @brief Maximum concurrent locks (fixed - a small structural bound, not a per-board tunable). */
#define PROTOCORE_DAV_LOCK_MAX 8

/** @brief Maximum locked-path length, including the NUL. */
#define PROTOCORE_DAV_LOCK_PATH_MAX 128

/** @brief Maximum lock-token length, including the NUL (e.g. "opaquelocktoken:xxxxxxxx-pc"). */
#define PROTOCORE_DAV_LOCK_TOKEN_MAX 48

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

/** @brief One active lock (RFC 4918 §6.4). */
typedef struct
{
    char path[PROTOCORE_DAV_LOCK_PATH_MAX];   ///< the locked resource path (trailing slash normalized off)
    char token[PROTOCORE_DAV_LOCK_TOKEN_MAX]; ///< the lock token (an opaquelocktoken URI)
    proto_bool exclusive;                     ///< exclusive-write (true) or shared (false)
    proto_bool depth_infinity; ///< the lock covers the whole subtree (Depth: infinity) vs just the resource
    proto_bool active;         ///< false = free slot
    uint32_t expiry_s;         ///< monotonic second the lock expires (0 = no timeout); swept by _sweep
} DavLock;

/** @brief The server-global lock table (one instance, not per-connection). */
typedef struct
{
    DavLock locks[PROTOCORE_DAV_LOCK_MAX];
} DavLockTable;

/** @brief What method takes: m. */
typedef struct
{
    const char *m;
} WebdavMethodArgs;

/** @brief What depth takes: depth_hdr, dflt. */
typedef struct
{
    const char *depth_hdr;
    int dflt;
} WebdavDepthArgs;

/** @brief What xml_escape takes: dst, cap, src. */
typedef struct
{
    char *dst;
    size_t cap;
    const char *src;
} WebdavXmlEscapeArgs;

/** @brief What dest_path takes: destination, out, cap. */
typedef struct
{
    const char *destination;
    char *out;
    size_t cap;
} WebdavDestPathArgs;

/** @brief What ms_begin takes: buf, cap, len. */
typedef struct
{
    char *buf;
    size_t cap;
    size_t len;
} WebdavMsBeginArgs;

/** @brief What ms_entry takes: buf, cap, len, href, is_collection, ... */
typedef struct
{
    char *buf;
    size_t cap;
    size_t len;
    const char *href;          ///< the resource's URL path (XML-escaped here)
    proto_bool is_collection;  ///< true for a directory (emits <collection/>)
    uint32_t size;             ///< content length (files only)
    const char *rfc1123_mtime; ///< Last-Modified string, or "" to omit
    const char *content_type;  ///< MIME type (files only), or "" to omit
} WebdavMsEntryArgs;

/** @brief What ms_end takes: buf, cap, len. */
typedef struct
{
    char *buf;
    size_t cap;
    size_t len;
} WebdavMsEndArgs;

/** @brief What proppatch_ms takes: buf, cap, href, body, body_len. */
typedef struct
{
    char *buf;        ///< destination buffer (whole document, NUL-terminated)
    size_t cap;       ///< buffer capacity
    const char *href; ///< the resource path (XML-escaped here)
    const char *body; ///< the PROPPATCH request body (not required to be NUL-terminated)
    size_t body_len;  ///< length of body
} WebdavProppatchMsArgs;

/** @brief What lock_init takes: t. */
typedef struct
{
    DavLockTable *t;
} WebdavLockInitArgs;

/** @brief What lock_acquire takes: t, path, token, exclusive, ... */
typedef struct
{
    DavLockTable *t;
    const char *path;
    const char *token;
    proto_bool exclusive;
    proto_bool depth_infinity;
    uint32_t expiry_s; ///< the monotonic second the lock expires (0 = no timeout); protocore_dav_lock_sweep drops a ...
} WebdavLockAcquireArgs;

/** @brief What lock_sweep takes: t, now_s. */
typedef struct
{
    DavLockTable *t;
    uint32_t now_s; ///< the caller's current monotonic second
} WebdavLockSweepArgs;

/** @brief What lock_refresh takes: t, token, new_expiry_s. */
typedef struct
{
    DavLockTable *t;
    const char *token;
    uint32_t new_expiry_s;
} WebdavLockRefreshArgs;

/** @brief What lock_find takes: t, path. */
typedef struct
{
    const DavLockTable *t;
    const char *path;
} WebdavLockFindArgs;

/** @brief What lock_release takes: t, token. */
typedef struct
{
    DavLockTable *t;
    const char *token;
} WebdavLockReleaseArgs;

/** @brief What lock_can_write takes: t, path, presented_token. */
typedef struct
{
    const DavLockTable *t;
    const char *path;
    const char *presented_token;
} WebdavLockCanWriteArgs;

/** @brief What if_token takes: if_header, out, cap. */
typedef struct
{
    const char *if_header;
    char *out;
    size_t cap;
} WebdavIfTokenArgs;

/**
 * @brief WebDAV wire format (RFC 4918): method classification, header parsing, and the 207 Multi-Status XML builder.
 *
 * A caller sets the members a call takes, invokes it through ::Webdav with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Webdav.method_args.m = ...;
 *   Webdav.method(work);
 *   // Webdav.value is what the call reports
 *
 * @var WebdavNs::method_args  what method takes: m
 * @var WebdavNs::depth_args  what depth takes: depth_hdr, dflt
 * @var WebdavNs::xml_escape_args  what xml_escape takes: dst, cap, src
 * @var WebdavNs::dest_path_args  what dest_path takes: destination, out, cap
 * @var WebdavNs::ms_begin_args  what ms_begin takes: buf, cap, len
 * @var WebdavNs::ms_entry_args  what ms_entry takes: buf, cap, len, href, is_collection,
 * @var WebdavNs::ms_end_args  what ms_end takes: buf, cap, len
 * @var WebdavNs::proppatch_ms_args  what proppatch_ms takes: buf, cap, href, body, body_len
 * @var WebdavNs::lock_init_args  what lock_init takes: t
 * @var WebdavNs::lock_acquire_args  what lock_acquire takes: t, path, token, exclusive,
 * @var WebdavNs::lock_sweep_args  what lock_sweep takes: t, now_s
 * @var WebdavNs::lock_refresh_args  what lock_refresh takes: t, token, new_expiry_s
 * @var WebdavNs::lock_find_args  what lock_find takes: t, path
 * @var WebdavNs::lock_release_args  what lock_release takes: t, token
 * @var WebdavNs::lock_can_write_args  what lock_can_write takes: t, path, presented_token
 * @var WebdavNs::if_token_args  what if_token takes: if_header, out, cap
 * @var WebdavNs::ok  false on overflow or a malformed value
 * @var WebdavNs::value  the value a call reports
 * @var WebdavNs::i32  0, 1, or PROTOCORE_DAV_DEPTH_INFINITY; dflt when depth_hdr is ...
 * @var WebdavNs::n  length written (NUL-terminated; truncated to fit cap)
 * @var WebdavNs::ptr  the stored lock on success, nullptr on conflict / full
 * @var WebdavNs::method  classify an HTTP method token (e.g. "PROPFIND") into a WebDavMethod
 * @var WebdavNs::depth  parse a Depth header value ("0", "1", or "infinity")
 * @var WebdavNs::xml_escape  XML-escape src into dst (`&`, `<`, `>`, `"`, `'`)
 * @var WebdavNs::dest_path  extract and percent-decode the path of a Destination header. ...
 * @var WebdavNs::ms_begin  write the XML prolog and the open <multistatus> element
 * @var WebdavNs::ms_entry  append one <response> describing a resource
 * @var WebdavNs::ms_end  close the <multistatus> element
 * @var WebdavNs::proppatch_ms  build a complete 207 Multi-Status body answering a PROPPATCH. The ...
 * @var WebdavNs::lock_init  reset a lock table (no locks held)
 * @var WebdavNs::lock_acquire  acquire a lock on path with the caller-supplied token (RFC 4918 ...
 * @var WebdavNs::lock_sweep  expire and drop every lock whose timeout has passed (RFC 4918 ...
 * @var WebdavNs::lock_refresh  refresh a held lock's timeout to new_expiry_s, keyed by token (a ...
 * @var WebdavNs::lock_find  find a lock covering path: one on path itself, or a Depth-infinity ...
 * @var WebdavNs::lock_release  release the lock whose token equals token (UNLOCK). true if one was ...
 * @var WebdavNs::lock_can_write  may a write to path proceed given the token the request presented ...
 * @var WebdavNs::if_token  extract the first lock token from an If header value (RFC 4918 ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    WebdavMethodArgs method_args;
    WebdavDepthArgs depth_args;
    WebdavXmlEscapeArgs xml_escape_args;
    WebdavDestPathArgs dest_path_args;
    WebdavMsBeginArgs ms_begin_args;
    WebdavMsEntryArgs ms_entry_args;
    WebdavMsEndArgs ms_end_args;
    WebdavProppatchMsArgs proppatch_ms_args;
    WebdavLockInitArgs lock_init_args;
    WebdavLockAcquireArgs lock_acquire_args;
    WebdavLockSweepArgs lock_sweep_args;
    WebdavLockRefreshArgs lock_refresh_args;
    WebdavLockFindArgs lock_find_args;
    WebdavLockReleaseArgs lock_release_args;
    WebdavLockCanWriteArgs lock_can_write_args;
    WebdavIfTokenArgs if_token_args;

    proto_bool ok;
    WebDavMethod value;
    int i32;
    size_t n;
    const DavLock *ptr;

    void (*const method)(uint8_t *restrict work);
    void (*const depth)(uint8_t *restrict work);
    void (*const xml_escape)(uint8_t *restrict work);
    void (*const dest_path)(uint8_t *restrict work);
    void (*const ms_begin)(uint8_t *restrict work);
    void (*const ms_entry)(uint8_t *restrict work);
    void (*const ms_end)(uint8_t *restrict work);
    void (*const proppatch_ms)(uint8_t *restrict work);
    void (*const lock_init)(uint8_t *restrict work);
    void (*const lock_acquire)(uint8_t *restrict work);
    void (*const lock_sweep)(uint8_t *restrict work);
    void (*const lock_refresh)(uint8_t *restrict work);
    void (*const lock_find)(uint8_t *restrict work);
    void (*const lock_release)(uint8_t *restrict work);
    void (*const lock_can_write)(uint8_t *restrict work);
    void (*const if_token)(uint8_t *restrict work);
} WebdavNs;

/** @brief The one symbol this module exports. */
extern WebdavNs Webdav;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBDAV

#endif // PROTOCORE_WEBDAV_H
