// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_WEBDAV_H
#define PROTOCORE_WEBDAV_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file webdav.h
 * @brief WebDAV wire format (RFC 4918): method classification, header parsing,
and the 207 Multi-Status XML builder.
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
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_WEBDAV_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    WebDavMethod (*method)(uint8_t *, const char *);
    int (*depth)(uint8_t *, const char *, int);
    size_t (*xml_escape)(uint8_t *, char *, size_t, const char *);
    proto_bool (*dest_path)(uint8_t *, const char *, char *, size_t);
    size_t (*ms_begin)(uint8_t *, char *, size_t, size_t);
    size_t (*ms_entry)(uint8_t *, char *, size_t, size_t, const char *, proto_bool, uint32_t, const char *,
                       const char *);
    size_t (*ms_end)(uint8_t *, char *, size_t, size_t);
    size_t (*proppatch_ms)(uint8_t *, char *, size_t, const char *, const char *, size_t);
    void (*lock_init)(uint8_t *, DavLockTable *);
    const DavLock *(*lock_acquire)(uint8_t *, DavLockTable *, const char *, const char *, proto_bool, proto_bool,
                                   uint32_t);
    size_t (*lock_sweep)(uint8_t *, DavLockTable *, uint32_t);
    const DavLock *(*lock_refresh)(uint8_t *, DavLockTable *, const char *, uint32_t);
    const DavLock *(*lock_find)(uint8_t *, const DavLockTable *, const char *);
    proto_bool (*lock_release)(uint8_t *, DavLockTable *, const char *);
    proto_bool (*lock_can_write)(uint8_t *, const DavLockTable *, const char *, const char *);
    proto_bool (*if_token)(uint8_t *, const char *, char *, size_t);
} WebdavNs;
PROTOCORE_NS_LAYOUT(WebdavNs, method, depth, xml_escape, dest_path, ms_begin, ms_entry, ms_end, proppatch_ms, lock_init,
                    lock_acquire, lock_sweep, lock_refresh, lock_find, lock_release, lock_can_write, if_token);

/**
 * @brief Classify an HTTP method token (e.g. "PROPFIND") into a WebDavMethod.
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param m M
 * @return The WebDavMethod.
 */
WebDavMethod protocore_webdav_method(uint8_t *work, const char *m);
/**
 * @brief Parse a Depth header value ("0", "1", or "infinity").
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param depth_hdr Depth hdr
 * @param dflt Dflt
 * @return The int.
 */
int protocore_webdav_depth(uint8_t *work, const char *depth_hdr, int dflt);
/**
 * @brief XML-escape src into dst (`&`, `<`, `>`, `"`, `'`).
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param dst Dst
 * @param cap Cap
 * @param src Src
 * @return The size_t.
 */
size_t protocore_webdav_xml_escape(uint8_t *work, char *dst, size_t cap, const char *src);
/**
 * @brief Extract and percent-decode the path of a Destination header. .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param destination Destination
 * @param out Out
 * @param cap Cap
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_webdav_dest_path(uint8_t *work, const char *destination, char *out, size_t cap);
/**
 * @brief Write the XML prolog and the open <multistatus> element.
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param len Len
 * @return The size_t.
 */
size_t protocore_webdav_ms_begin(uint8_t *work, char *buf, size_t cap, size_t len);
/**
 * @brief Append one <response> describing a resource.
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param len Len
 * @param href the resource's URL path (XML-escaped here)
 * @param is_collection true for a directory (emits <collection/>)
 * @param size content length (files only)
 * @param rfc1123_mtime Last-Modified string, or "" to omit
 * @param content_type MIME type (files only), or "" to omit
 * @return The size_t.
 */
size_t protocore_webdav_ms_entry(uint8_t *work, char *buf, size_t cap, size_t len, const char *href,
                                 proto_bool is_collection, uint32_t size, const char *rfc1123_mtime,
                                 const char *content_type);
/**
 * @brief Close the <multistatus> element.
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param len Len
 * @return The size_t.
 */
size_t protocore_webdav_ms_end(uint8_t *work, char *buf, size_t cap, size_t len);
/**
 * @brief Build a complete 207 Multi-Status body answering a PROPPATCH. The .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param buf destination buffer (whole document, NUL-terminated)
 * @param cap buffer capacity
 * @param href the resource path (XML-escaped here)
 * @param body the PROPPATCH request body (not required to be NUL-terminated)
 * @param body_len length of body
 * @return The size_t.
 */
size_t protocore_webdav_proppatch_ms(uint8_t *work, char *buf, size_t cap, const char *href, const char *body,
                                     size_t body_len);
/**
 * @brief Reset a lock table (no locks held).
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param t T
 */
void protocore_webdav_lock_init(uint8_t *work, DavLockTable *t);
/**
 * @brief Acquire a lock on path with the caller-supplied token (RFC 4918 .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param t T
 * @param path Path
 * @param token Token
 * @param exclusive Exclusive
 * @param depth_infinity Depth infinity
 * @param expiry_s the monotonic second the lock expires (0 = no timeout); protocore_dav_lock_sweep drops a
 * @return The const DavLock *.
 */
const DavLock *protocore_webdav_lock_acquire(uint8_t *work, DavLockTable *t, const char *path, const char *token,
                                             proto_bool exclusive, proto_bool depth_infinity, uint32_t expiry_s);
/**
 * @brief Expire and drop every lock whose timeout has passed (RFC 4918 .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param t T
 * @param now_s the caller's current monotonic second
 * @return The size_t.
 */
size_t protocore_webdav_lock_sweep(uint8_t *work, DavLockTable *t, uint32_t now_s);
/**
 * @brief Refresh a held lock's timeout to new_expiry_s, keyed by token (a .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param t T
 * @param token Token
 * @param new_expiry_s New expiry s
 * @return The const DavLock *.
 */
const DavLock *protocore_webdav_lock_refresh(uint8_t *work, DavLockTable *t, const char *token, uint32_t new_expiry_s);
/**
 * @brief Find a lock covering path: one on path itself, or a Depth-infinity .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param t T
 * @param path Path
 * @return The const DavLock *.
 */
const DavLock *protocore_webdav_lock_find(uint8_t *work, const DavLockTable *t, const char *path);
/**
 * @brief Release the lock whose token equals token (UNLOCK). true if one was .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param t T
 * @param token Token
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_webdav_lock_release(uint8_t *work, DavLockTable *t, const char *token);
/**
 * @brief May a write to path proceed given the token the request presented .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param t T
 * @param path Path
 * @param presented_token Presented token
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_webdav_lock_can_write(uint8_t *work, const DavLockTable *t, const char *path,
                                           const char *presented_token);
/**
 * @brief Extract the first lock token from an If header value (RFC 4918 .
 * @param work PROTOCORE_WEBDAV_BORROW bytes the caller took. Not held past the call.
 * @param if_header If header
 * @param out Out
 * @param cap Cap
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_webdav_if_token(uint8_t *work, const char *if_header, char *out, size_t cap);

/** @brief Module namespace. */
PROTOCORE_NS WebdavNs Webdav PROTOCORE_UNUSED = {.method = protocore_webdav_method,
                                                 .depth = protocore_webdav_depth,
                                                 .xml_escape = protocore_webdav_xml_escape,
                                                 .dest_path = protocore_webdav_dest_path,
                                                 .ms_begin = protocore_webdav_ms_begin,
                                                 .ms_entry = protocore_webdav_ms_entry,
                                                 .ms_end = protocore_webdav_ms_end,
                                                 .proppatch_ms = protocore_webdav_proppatch_ms,
                                                 .lock_init = protocore_webdav_lock_init,
                                                 .lock_acquire = protocore_webdav_lock_acquire,
                                                 .lock_sweep = protocore_webdav_lock_sweep,
                                                 .lock_refresh = protocore_webdav_lock_refresh,
                                                 .lock_find = protocore_webdav_lock_find,
                                                 .lock_release = protocore_webdav_lock_release,
                                                 .lock_can_write = protocore_webdav_lock_can_write,
                                                 .if_token = protocore_webdav_if_token};

PROTOCORE_END_DECLS

#endif // PROTOCORE_WEBDAV_H
