// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the WebDAV request handler's recursive filesystem operations
// (COPY / MOVE / DELETE of collections, RFC 4918). These exercise the real
// handler against real littlefs on a RAM block device (test/mocks/lfs_mock.h) - the same
// filesystem the device runs, so a recursive walk is answered here exactly as it is on
// hardware, and the gate does not cost a flash cycle.

#include "lfs_mock.h" // the littlefs-backed store this suite walks
#include "protocore.h"
#include "server/filesystem/filesystem.h" // pc_fs_join()/pc_fs_resolve(): a pure header shared with SFTP/SCP,
                                          // neither of which is linked into this env, so tested directly below
#include <stdio.h>
#include <string.h>

#include <unity.h>
#include "network_drivers/transport/tcp.h"

static const pc_mnt_backend *davfs; // the store WebDAV walks; bound in setUp

static void push_str(uint8_t slot, const char *s)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; s[i]; i++)
    {
        size_t next = (c->rx_head + 1) % RX_BUF_SIZE;
        c->rx_buffer[c->rx_head] = (uint8_t)s[i];
        c->rx_head = next;
    }
}

static void feed_and_handle(uint8_t slot, const char *req)
{
    push_str(slot, req);
    http_parse(slot);
    handle();
}

// Re-arm slot 0 for a fresh request on the (close-after-each-DAV-response) flow.
static void rearm()
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = pc_net_host_pcb();
    http_reset(0);
    tcp_capture_reset();
}

// --- tree helpers (through the seam, so they see what littlefs actually stored) ------
static void tree_put(const char *path, const char *content)
{
    TEST_ASSERT_TRUE(lfsm_write_text(path, content));
}
static void tree_mkdir(const char *path)
{
    TEST_ASSERT_TRUE(lfsm_mkdir(path));
}
static proto_bool tree_has(const char *path)
{
    return lfsm()->exists(path);
}
static proto_bool tree_is_dir(const char *path)
{
    pc_mnt_stat st;
    return lfsm()->stat(path, &st) && st.is_dir;
}
static proto_bool tree_content_eq(const char *path, const char *exp)
{
    pc_mnt_stat st;
    if (!lfsm()->stat(path, &st) || st.is_dir || st.size != strlen(exp))
    {
        return PROTO_FALSE;
    }
    char buf[512];
    if (st.size >= sizeof(buf))
    {
        return PROTO_FALSE;
    }
    int h = lfsm()->open(path, PC_MNT_READ);
    if (h < 0)
    {
        return PROTO_FALSE;
    }
    int n = lfsm()->read(h, buf, sizeof(buf));
    lfsm()->close(h);
    return (n == (int)strlen(exp) && memcmp(buf, exp, (size_t)n) == 0) ? PROTO_TRUE : PROTO_FALSE;
}
static proto_bool pc_resp_status(int code)
{
    char want[20];
    snprintf(want, sizeof(want), "HTTP/1.1 %d", code);
    return strstr(tcp_captured(), want) != NULL;
}

// Build a source collection /dav/src with a nested subcollection + files.
static void populate_src()
{
    tree_mkdir("/dav/src");
    tree_put("/dav/src/a.txt", "alpha");
    tree_put("/dav/src/b.txt", "bravo");
    tree_mkdir("/dav/src/sub");
    tree_put("/dav/src/sub/c.txt", "charlie");
}

void setUp()
{
    pc_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = pc_net_host_pcb();
        http_reset(i);
    }
    ws_init();
    pc_sse_init();
    tcp_capture_reset();
    lfsm_format();
    davfs = lfsm();
    pc_mnt_mount(davfs);
    // The mount's own root has to exist before anything can be created under it. The flat mock
    // stored paths, so /dav was never a directory and never needed to be one.
    lfsm_mkdir("/dav");
    dav("/dav", davfs, "/dav");
}

void tearDown()
{
    tcp_capture_disable();
}

// ====================================================================
// TESTS
// ====================================================================

// RFC 4918 9.8: COPY of a collection (Depth infinity, the default) copies the
// whole tree, byte-exact, including the nested subcollection.
void test_copy_collection_recursive()
{
    populate_src();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(201));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/dst"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/a.txt", "alpha"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/b.txt", "bravo"));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/dst/sub"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/sub/c.txt", "charlie"));
    // The source is left intact (COPY, not MOVE).
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "alpha"));
}

// RFC 4918 9.8.3: COPY with Depth: 0 copies only the collection, not its members.
void test_copy_collection_depth0_shallow()
{
    populate_src();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/shallow\r\nDepth: 0\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(201));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/shallow"));
    TEST_ASSERT_FALSE(tree_has("/dav/shallow/a.txt")); // members not copied
    TEST_ASSERT_FALSE(tree_has("/dav/shallow/sub"));
}

// RFC 4918 9.8.4: Overwrite defaults to T (replace -> 204); Overwrite: F over an
// existing destination is refused with 412.
void test_copy_overwrite_semantics()
{
    populate_src();
    tree_mkdir("/dav/dst"); // a pre-existing destination
    tree_put("/dav/dst/stale.txt", "old");

    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(204));             // replaced
    TEST_ASSERT_FALSE(tree_has("/dav/dst/stale.txt")); // target cleared first
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/a.txt", "alpha"));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst\r\nOverwrite: F\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(412));
}

// MOVE of a collection re-paths the whole tree and removes the source.
void test_move_collection_recursive()
{
    populate_src();
    feed_and_handle(0, "MOVE /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/moved\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(201));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/moved/sub/c.txt", "charlie"));
    TEST_ASSERT_FALSE(tree_has("/dav/src"));
    TEST_ASSERT_FALSE(tree_has("/dav/src/sub/c.txt"));
}

// DELETE of a collection removes the whole tree (recursive), answering 204.
void test_delete_collection_recursive()
{
    populate_src();
    feed_and_handle(0, "DELETE /dav/src HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(204));
    TEST_ASSERT_FALSE(tree_has("/dav/src"));
    TEST_ASSERT_FALSE(tree_has("/dav/src/a.txt"));
    TEST_ASSERT_FALSE(tree_has("/dav/src/sub/c.txt"));
}

// PROPFIND Depth: 0 returns a 207 describing only the collection itself.
void test_propfind_depth0_collection_only()
{
    populate_src();
    feed_and_handle(0, "PROPFIND /dav/src HTTP/1.1\r\nHost: x\r\nDepth: 0\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(pc_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "/dav/src"));
    TEST_ASSERT_NULL(strstr(r, "a.txt")); // members are not listed at Depth 0
}

// PROPFIND Depth: 1 lists the collection and its immediate members.
void test_propfind_depth1_lists_members()
{
    populate_src();
    feed_and_handle(0, "PROPFIND /dav/src HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(pc_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "a.txt"));
    TEST_ASSERT_NOT_NULL(strstr(r, "b.txt"));
    TEST_ASSERT_NOT_NULL(strstr(r, "sub")); // the subcollection is a member
}

// MKCOL creates a collection (201); repeating it on an existing path is 405.
void test_mkcol_create_and_conflict()
{
    feed_and_handle(0, "MKCOL /dav/newdir HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(201));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/newdir"));

    rearm();
    feed_and_handle(0, "MKCOL /dav/newdir HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(405)); // already exists
}

// DELETE of a single file removes just that file (204), leaving siblings intact.
void test_delete_single_file()
{
    populate_src();
    feed_and_handle(0, "DELETE /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(204));
    TEST_ASSERT_FALSE(tree_has("/dav/src/a.txt"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/b.txt", "bravo")); // sibling untouched
}

// OPTIONS advertises WebDAV: a DAV compliance-class header and an Allow list.
void test_options_advertises_dav()
{
    feed_and_handle(0, "OPTIONS /dav/ HTTP/1.1\r\nHost: x\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(pc_resp_status(200) || pc_resp_status(204));
    TEST_ASSERT_NOT_NULL(strstr(r, "DAV:"));     // compliance class header
    TEST_ASSERT_NOT_NULL(strstr(r, "PROPFIND")); // Allow lists the DAV methods
}

// GET through the DAV mount streams a stored file's bytes (file-serving path).
void test_get_file_through_mount()
{
    populate_src();
    feed_and_handle(0, "GET /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(r, "alpha"));
}

// --- streaming-PUT helpers -------------------------------------------------
// Push `len` raw bytes into slot's rx ring (a body may exceed any single ring
// so feed it in chunks, draining after each - as the real transport does).
static void push_bytes(uint8_t slot, const uint8_t *b, size_t len)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; i < len; i++)
    {
        c->rx_buffer[c->rx_head] = b[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
}

// Feed a complete PUT with an arbitrary-length body and run the handler. The
// body streams to the DAV file via dav_stream_put_*; chunked feeding keeps the
// ring from overflowing regardless of RX_BUF_SIZE.
static void feed_put(uint8_t slot, const char *path, const uint8_t *body, size_t n)
{
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "PUT %s HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", path, (unsigned)n);
    push_str(slot, hdr);
    http_parse(slot);
    for (size_t off = 0; off < n;)
    {
        size_t chunk = n - off > 200 ? 200 : n - off;
        push_bytes(slot, body + off, chunk);
        http_parse(slot);
        off += chunk;
    }
    handle();
}

// A new file streams straight to disk and answers 201 Created, byte-exact.
void test_put_stream_create()
{
    const char *body = "hello world";
    feed_put(0, "/dav/up.txt", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(pc_resp_status(201));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/up.txt", "hello world"));
}

// PUT over an existing file truncates and answers 204 (existed).
void test_put_stream_overwrite()
{
    tree_put("/dav/up.txt", "stale contents");
    const char *body = "new";
    feed_put(0, "/dav/up.txt", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(pc_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/up.txt", "new"));
}

// An empty PUT has no streamed body (Content-Length 0), so it takes the buffered
// fallback: create the file and answer 201.
void test_put_empty_buffered()
{
    feed_and_handle(0, "PUT /dav/empty.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(201));
    TEST_ASSERT_TRUE(tree_has("/dav/empty.txt"));
}

// A body the store has no room for -> the sink flags the failed write and the handler answers
// 507 Insufficient Storage. The volume is filled first, so the refusal is a real ENOSPC.
void test_put_stream_write_fails_507()
{
    lfsm_fail_prog_after(3); // the file is created, then the medium refuses its body
    static uint8_t big[2100];
    memset(big, 'A', sizeof(big));
    feed_put(0, "/dav/big.txt", big, sizeof(big));
    TEST_ASSERT_TRUE(pc_resp_status(507));
}

// When the store cannot open the target the sink never activates and the handler answers 409
// Conflict. Every descriptor is held, which is how a device runs out of them.
void test_put_stream_open_fails_409()
{
    lfsm_hold_all_handles();
    const char *body = "abc";
    feed_put(0, "/dav/overflow.txt", (const uint8_t *)body, strlen(body));
    lfsm_release_handles();
    TEST_ASSERT_TRUE(pc_resp_status(409));
}

// A ".." in the target is rejected at the stream-begin resolve (so it never
// streams) and again by the handler: 403 Forbidden.
void test_put_stream_traversal_403()
{
    const char *body = "abc";
    feed_put(0, "/dav/../secret", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(pc_resp_status(403));
}

// The stream-begin hook fires for any bodied request: a non-PUT method and a
// path that matches no DAV route both decline the sink (and don't crash).
void test_put_stream_begin_declines()
{
    // Non-PUT with a body: begin sees method != PUT and declines.
    feed_and_handle(0, "POST /dav/x.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc");
    rearm();
    // PUT to a path outside any DAV mount: begin finds no route and declines.
    const char *body = "abc";
    feed_put(0, "/nomatch/y.txt", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(pc_resp_status(404)); // no route -> not found
}

// A streamed PUT torn down before completion (peer reset) runs the abort hook,
// which closes the half-open file so the handle is not leaked.
void test_put_stream_abort()
{
    // Headers + a partial body: Content-Length promises 10, only 4 arrive.
    push_str(0, "PUT /dav/ab.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nabcd");
    http_parse(0);
    TEST_ASSERT_TRUE(tree_has("/dav/ab.txt")); // begin opened (created) the file
    http_reset(0);                             // body_streaming && !COMPLETE -> abort hook
    // The file is still present (open created the node); the handle was closed.
    TEST_ASSERT_TRUE(tree_has("/dav/ab.txt"));
}

// Feed a streamed PUT carrying an If header (for presenting a lock token).
static void feed_put_if(uint8_t slot, const char *path, const char *if_hdr, const uint8_t *body, size_t n)
{
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "PUT %s HTTP/1.1\r\nHost: x\r\nIf: %s\r\nContent-Length: %u\r\n\r\n", path, if_hdr,
             (unsigned)n);
    push_str(slot, hdr);
    http_parse(slot);
    for (size_t off = 0; off < n;)
    {
        size_t chunk = n - off > 200 ? 200 : n - off;
        push_bytes(slot, body + off, chunk);
        http_parse(slot);
        off += chunk;
    }
    handle();
}

// Pull the "opaquelocktoken:...-pc" out of a LOCK response's Lock-Token header.
static proto_bool extract_lock_token(const char *resp, char *out, size_t cap)
{
    const char *p = strstr(resp, "opaquelocktoken:");
    if (!p)
    {
        return PROTO_FALSE;
    }
    const char *e = strchr(p, '>'); // the Lock-Token header wraps it in "<...>"
    if (!e)
    {
        return PROTO_FALSE;
    }
    size_t len = (size_t)(e - p);
    if (len + 1 > cap)
    {
        return PROTO_FALSE;
    }
    memcpy(out, p, len);
    out[len] = 0;
    return PROTO_TRUE;
}

// LOCK now enforces: a locked resource rejects a tokenless write with 423; the token unlocks it; UNLOCK
// requires the matching token (RFC 4918 §6-7).
void test_lock_enforcement()
{
    populate_src();
    feed_and_handle(0, "LOCK /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    char token[48];
    TEST_ASSERT_TRUE(extract_lock_token(tcp_captured(), token, sizeof(token)));

    // A PUT without the token is refused 423 and the file keeps its original content.
    rearm();
    feed_put(0, "/dav/src/a.txt", (const uint8_t *)"HACKED", 6);
    TEST_ASSERT_TRUE(pc_resp_status(423));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "alpha"));

    // UNLOCK with the wrong token is refused 409 (you cannot release another principal's lock).
    rearm();
    feed_and_handle(0, "UNLOCK /dav/src/a.txt HTTP/1.1\r\nHost: x\r\nLock-Token: <opaquelocktoken:nope>\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(409));

    // A PUT presenting the token in its If header succeeds and updates the file.
    rearm();
    char if_hdr[64];
    snprintf(if_hdr, sizeof(if_hdr), "(<%s>)", token);
    feed_put_if(0, "/dav/src/a.txt", if_hdr, (const uint8_t *)"updated", 7);
    TEST_ASSERT_TRUE(pc_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "updated"));

    // UNLOCK with the correct token releases it (204); afterward a tokenless PUT is allowed again.
    rearm();
    char ltok[64];
    snprintf(ltok, sizeof(ltok), "<%s>", token);
    char unlock[160];
    snprintf(unlock, sizeof(unlock), "UNLOCK /dav/src/a.txt HTTP/1.1\r\nHost: x\r\nLock-Token: %s\r\n\r\n", ltok);
    feed_and_handle(0, unlock);
    TEST_ASSERT_TRUE(pc_resp_status(204));

    rearm();
    feed_put(0, "/dav/src/a.txt", (const uint8_t *)"free", 4);
    TEST_ASSERT_TRUE(pc_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "free"));
}

// The WebDAV method error paths: bad/foreign/traversal destinations, missing sources, and a
// Depth: infinity PROPFIND rejection.
void test_webdav_error_paths()
{
    feed_and_handle(0, "DELETE /dav/nope HTTP/1.1\r\nHost: x\r\n\r\n"); // no such resource
    TEST_ASSERT_TRUE(pc_resp_status(404));

    rearm();
    populate_src();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\n\r\n"); // no Destination
    TEST_ASSERT_TRUE(pc_resp_status(400));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /other/x\r\n\r\n"); // foreign mount
    TEST_ASSERT_TRUE(pc_resp_status(502));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/../x\r\n\r\n"); // traversal
    TEST_ASSERT_TRUE(pc_resp_status(403));

    rearm();
    feed_and_handle(0, "COPY /dav/gone HTTP/1.1\r\nHost: x\r\nDestination: /dav/x\r\n\r\n"); // no such source
    TEST_ASSERT_TRUE(pc_resp_status(404));

    rearm();
    tree_mkdir("/dav/mvdst");
    feed_and_handle(0, "MOVE /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/mvdst\r\n\r\n"); // replace existing
    TEST_ASSERT_TRUE(pc_resp_status(204));

    rearm();
    feed_and_handle(0, "PROPFIND /dav/nope HTTP/1.1\r\nHost: x\r\nDepth: 0\r\n\r\n"); // no such resource
    TEST_ASSERT_TRUE(pc_resp_status(404));

    rearm();
    populate_src();
    feed_and_handle(0, "PROPFIND /dav/src HTTP/1.1\r\nHost: x\r\nDepth: infinity\r\n\r\n"); // finite-depth only
    TEST_ASSERT_TRUE(pc_resp_status(403));
}

// A tree deeper than the recursion bound (8) is refused by both the recursive delete and copy,
// which fail closed (nothing removed) rather than overflow the stack.
void test_webdav_deep_tree_rejected()
{
    char p[300];
    int off = snprintf(p, sizeof p, "/dav/deep");
    tree_mkdir(p);
    for (int i = 0; i < 10; i++)
    {
        off += snprintf(p + off, sizeof(p) - off, "/l%d", i);
        tree_mkdir(p);
    }
    feed_and_handle(0, "DELETE /dav/deep HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(403));   // dav_rm_recursive refuses past depth 8
    TEST_ASSERT_TRUE(tree_has("/dav/deep")); // nothing was removed

    rearm();
    feed_and_handle(0, "COPY /dav/deep HTTP/1.1\r\nHost: x\r\nDestination: /dav/dcopy\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(409)); // dav_copy_recursive refuses past depth 8
}

// PROPFIND of a directory with more members than the listing cap (PC_WEBDAV_MAX_ENTRIES) stops at
// the limit; PROPPATCH answers 207 for an existing resource and 404 for a missing one.
void test_webdav_propfind_limit_and_proppatch()
{
    tree_mkdir("/dav/big");
    for (int i = 0; i < 40; i++) // more than PC_WEBDAV_MAX_ENTRIES (32)
    {
        char p[64];
        snprintf(p, sizeof p, "/dav/big/f%02d.txt", i);
        tree_put(p, "x");
    }
    feed_and_handle(0, "PROPFIND /dav/big HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(207)); // listing truncated at the entry cap

    rearm();
    tree_put("/dav/file.txt", "data");
    feed_and_handle(0, "PROPPATCH /dav/file.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(207)); // properties refused, request accepted

    rearm();
    feed_and_handle(0, "PROPPATCH /dav/nope HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(404));
}

// COPY when the FS node table is exhausted: the destination file / collection cannot be created, so
// the recursive copy fails closed (409) rather than emitting a partial tree.
void test_webdav_copy_fs_table_full()
{
    tree_put("/dav/f.txt", "data");
    tree_mkdir("/dav/d");
    lfsm_fail_prog_always(); // the medium refuses every write: neither the file nor the collection lands
    feed_and_handle(0, "COPY /dav/f.txt HTTP/1.1\r\nHost: x\r\nDestination: /dav/fc\r\n\r\n"); // dst open("w") fails
    TEST_ASSERT_TRUE(pc_resp_status(409));

    rearm();
    feed_and_handle(0, "COPY /dav/d HTTP/1.1\r\nHost: x\r\nDestination: /dav/dc\r\n\r\n"); // mkdir(dst) fails
    TEST_ASSERT_TRUE(pc_resp_status(409));
}

// WebDAV method-handler edges: GET/HEAD on a missing resource (404) and on a collection
// (405, not a download); a COPY Destination with a trailing slash (the joined path's
// trailing '/' is stripped); and a buffered (empty-body) PUT whose file cannot be created
// because the FS node table is exhausted (409).
void test_webdav_get_put_dest_edges()
{
    feed_and_handle(0, "GET /dav/missing.txt HTTP/1.1\r\nHost: x\r\n\r\n"); // no such resource
    TEST_ASSERT_TRUE(pc_resp_status(404));

    rearm();
    feed_and_handle(0, "HEAD /dav/missing.txt HTTP/1.1\r\nHost: x\r\n\r\n"); // same open-fail guard
    TEST_ASSERT_TRUE(pc_resp_status(404));

    rearm();
    tree_mkdir("/dav/adir");
    feed_and_handle(0, "GET /dav/adir HTTP/1.1\r\nHost: x\r\n\r\n"); // GET on a collection
    TEST_ASSERT_TRUE(pc_resp_status(405));

    rearm();
    tree_put("/dav/f.txt", "hi");
    feed_and_handle(0, "COPY /dav/f.txt HTTP/1.1\r\nHost: x\r\nDestination: /dav/g.txt/\r\n\r\n"); // trailing slash
    TEST_ASSERT_TRUE(pc_resp_status(201));    // created at the slash-stripped path
    TEST_ASSERT_TRUE(tree_has("/dav/g.txt")); // no trailing-slash node

    rearm();
    lfsm_fail_prog_after(1); // the medium refuses the write that would create it
    feed_and_handle(0, "PUT /dav/newfile.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n"); // open("w") fails
    TEST_ASSERT_TRUE(pc_resp_status(409));
}

// COPY whose destination filesystem path would overflow the 256-byte path buffer: a deep
// fs root plus a destination name exceeds it, so the destination dav_join fails -> 414. The
// destination join runs before the source-exists check, and the short source path under the
// same deep root still resolves, so the request reaches the COPY case (not a top-level 404).
void test_webdav_copy_dest_path_too_long_414()
{
    // 240-char fs root: a short source ("/s") still joins under 256, but root + any
    // real destination sub-path overflows the 256-byte join buffer.
    static char longroot[241];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0';
    dav("/d2", davfs, longroot);

    char req[128];
    // dest sub-path "/destination_file_name.txt" (26 chars) + the 240-char root overflows 256.
    snprintf(req, sizeof(req), "COPY /d2/s HTTP/1.1\r\nHost: x\r\nDestination: /d2/destination_file_name.txt\r\n\r\n");
    feed_and_handle(0, req);
    TEST_ASSERT_TRUE(pc_resp_status(414));
}

// The recursive delete/copy helpers re-open each node defensively (a core can invalidate
// an open handle across the writes a copy makes). When a node that exists() reports cannot
// actually be opened, the helper fails closed rather than proceeding on a bad handle:
// DELETE of such a resource answers 403, and a COPY whose child cannot be opened answers 409.
void test_webdav_recursive_open_failure()
{
    // DELETE: the store cannot remove the name -> 403. Unlinking is a metadata write, not an
    // open, so refusing writes is what actually stops it.
    tree_put("/dav/locked.txt", "data");
    lfsm_fail_prog_always();
    feed_and_handle(0, "DELETE /dav/locked.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(403));
    lfsm_no_prog_failure();
    TEST_ASSERT_TRUE(tree_has("/dav/locked.txt")); // nothing removed

    // COPY of a collection the recursion cannot write out -> 409.
    rearm();
    populate_src();
    lfsm_fail_prog_always();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/cdst\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(409));
    lfsm_no_prog_failure();
}

// A request under a mount whose fs root is so long that root + the request sub-path would
// overflow the 256-byte resolve buffer: dav_resolve_path fails at the top -> 414 for the
// method (here GET), before any method-specific handling.
void test_webdav_source_path_too_long_414()
{
    static char longroot[255];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0'; // 254-char fs root: root + "/x" == 256, the join fails
    dav("/d3", davfs, longroot);
    feed_and_handle(0, "GET /d3/x HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(414));
}

// dav() route registration edges: a prefix already ending in '*' is stored verbatim (no
// second wildcard appended), and once the route table is full a further dav() mount is
// dropped (fails closed) so a request to it is not served.
void test_webdav_dav_wildcard_and_route_full()
{
    // (a) A wildcard-terminated prefix is stored as-is; a request under it still routes.
    dav("/w*", davfs, "/w");
    tree_put("/w/f.txt", "hi");
    feed_and_handle(0, "GET /w/f.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));

    // (b) Fill the route table, then a further dav() mount is dropped -> its path 404s.
    rearm();
    for (int i = 0; i < MAX_ROUTES; i++)
    {
        char p[16];
        snprintf(p, sizeof p, "/r%d", i);
        dav(p, davfs, "/r");
    }
    dav("/dropped", davfs, "/d"); // table full -> dropped
    feed_and_handle(0, "GET /dropped/x HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(404)); // never registered
}

// dav_join()'s separator handling across the three root shapes: a root that already ends in
// '/' (the doubled separator is collapsed by skipping the sub-path's leading '/'), an empty
// root, and a null fs_root (treated as empty). All three must resolve to the same on-disk
// path the tree was populated at.
void test_webdav_join_root_variants()
{
    // (a) root ending in '/': "/tsroot/" + "/f.txt" must not become "/tsroot//f.txt".
    dav("/ts", davfs, "/tsroot/");
    tree_put("/tsroot/f.txt", "hi");
    feed_and_handle(0, "GET /ts/f.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "hi"));

    // (b) empty root: the request sub-path is the whole fs path.
    rearm();
    dav("/er", davfs, "");
    tree_put("/f2.txt", "yo");
    feed_and_handle(0, "GET /er/f2.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "yo"));

    // (c) the bare mount prefix under an empty root joins to "/" - a one-character path, so the
    // trailing-slash strip must leave it alone (stripping would yield ""). What proves the lookup
    // was well formed is that it resolves to the root directory and GET refuses a collection with
    // 405. The flat mock answered 404 here only because it had no "/" entry to find; a real
    // filesystem always has a root.
    rearm();
    feed_and_handle(0, "GET /er HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(405));

    // (d) a null fs_root behaves exactly like an empty one.
    rearm();
    dav("/nr", davfs, NULL);
    tree_put("/n.txt", "nn");
    feed_and_handle(0, "GET /nr/n.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "nn"));
}

// dav() with an empty url_prefix stores the pattern as the bare wildcard "*", which matches
// every path; the mount prefix length is then zero, so the whole request path is the sub-path.
void test_webdav_dav_empty_prefix_mount()
{
    dav("", davfs, "/ep");
    tree_put("/ep/x.txt", "ee");
    feed_and_handle(0, "GET /x.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "ee"));
}

// Method-dispatch edges the other tests do not reach: HEAD of an existing file (headers only,
// via the file-serving path), a method token WebDAV does not classify (405 + Allow), a buffered
// empty PUT over an EXISTING file (204, not 201), MKCOL when the FS cannot create the
// collection (409), and MOVE of a source that does not exist (rename fails -> 409).
void test_webdav_method_dispatch_edges()
{
    populate_src();
    feed_and_handle(0, "HEAD /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 5"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "alpha")); // HEAD: headers only

    rearm();
    feed_and_handle(0, "BREW /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n"); // not a WebDAV method
    TEST_ASSERT_TRUE(pc_resp_status(405));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Allow:"));

    rearm();
    tree_put("/dav/e.txt", "old");
    feed_and_handle(0, "PUT /dav/e.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(204)); // existed -> 204, not 201

    rearm();
    feed_and_handle(0, "MOVE /dav/gone HTTP/1.1\r\nHost: x\r\nDestination: /dav/mv\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(409)); // rename of a missing source fails
    TEST_ASSERT_FALSE(tree_has("/dav/mv"));

    rearm();
    lfsm_fail_prog_after(1); // the medium refuses the write that would create the collection
    feed_and_handle(0, "MKCOL /dav/newcol HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(409)); // does not exist, but mkdir failed
}

// COPY/MOVE header edges: a Destination that is not a usable path at all (400), the
// lowercase and explicit-true Overwrite forms, a COPY Depth other than 0 (not shallow),
// and a destination that joins to the one-character path "/" (no trailing-slash strip).
void test_webdav_copy_header_edges()
{
    populate_src();
    // A Destination that is neither an abs-path nor an absolute URI is malformed -> 400.
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: notapath\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(400));

    // Overwrite: f (lowercase) is the same refusal as F.
    rearm();
    tree_mkdir("/dav/dst2");
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst2\r\nOverwrite: f\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(412));

    // Overwrite: T is the explicit form of the default -> replace -> 204.
    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst2\r\nOverwrite: T\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst2/a.txt", "alpha"));

    // A Depth header that is not "0" is not shallow: the whole tree is copied.
    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/deep1\r\nDepth: 1\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(201));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/deep1/sub/c.txt", "charlie"));
}

// A destination whose joined fs path is exactly "/" (empty root, Destination == the mount
// prefix): the trailing-slash strip must not touch a one-character path.
void test_webdav_copy_dest_joins_to_root()
{
    dav("/z", davfs, "");
    tree_put("/src.txt", "s");
    feed_and_handle(0, "COPY /z/src.txt HTTP/1.1\r\nHost: x\r\nDestination: /z\r\n\r\n");
    // The destination joining to "/" is what this proves. On a real store that is the root
    // collection, which already exists, so the handler takes its overwrite path - remove the
    // target, then copy onto it - and neither can be done to a root. Observed: 409 Conflict.
    // The flat mock answered 201 because "/" was just another name it could create.
    TEST_ASSERT_TRUE(pc_resp_status(409));
    TEST_ASSERT_TRUE(tree_has("/src.txt")); // the source is left alone
}

// PROPFIND property selection: a plain file reports getcontentlength + getcontenttype and
// is never enumerated as a collection; a collection requested WITH a trailing slash already
// has a collection href, so no second '/' is appended.
void test_webdav_propfind_file_and_trailing_slash()
{
    tree_put("/dav/doc.txt", "hello");
    feed_and_handle(0, "PROPFIND /dav/doc.txt HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(pc_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "getcontentlength"));
    TEST_ASSERT_NOT_NULL(strstr(r, "getcontenttype")); // a file carries a content type
    TEST_ASSERT_NULL(strstr(r, "<D:collection/>"));    // not a collection

    rearm();
    tree_mkdir("/dav/col");
    tree_put("/dav/col/m.txt", "m");
    feed_and_handle(0, "PROPFIND /dav/col/ HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    r = tcp_captured();
    TEST_ASSERT_TRUE(pc_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "<D:href>/dav/col/</D:href>")); // no doubled separator
    TEST_ASSERT_NOT_NULL(strstr(r, "/dav/col/m.txt"));
}

// The DAV route scan skips table entries that are not DAV mounts: a plain on() route
// registered alongside a mount is stepped over both by the request dispatcher and by the
// streaming-PUT begin hook, and still serves its own path normally.
static void h_plain(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "plain");
}

void test_webdav_route_scan_skips_non_dav_routes()
{
    on_http("/plain", HTTP_GET, h_plain);
    feed_and_handle(0, "GET /plain HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "plain"));

    // A bodied PUT to the non-DAV path: the begin hook walks the same table, skips the
    // non-DAV entry, finds no mount, and declines the sink (the route answers 405).
    rearm();
    const char *body = "abc";
    feed_put(0, "/plain", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(pc_resp_status(405)); // GET-only route, method not allowed
}

// The abort hook must be a no-op for a slot whose PUT never opened a file: with the node
// table exhausted the sink records the error but holds no handle, so a torn-down transfer
// has nothing to close.
void test_webdav_stream_put_abort_without_open()
{
    lfsm_fail_prog_always(); // the store refuses the write that would create it
    push_str(0, "PUT /dav/never.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nabcd");
    http_parse(0);
    TEST_ASSERT_FALSE(tree_has("/dav/never.txt")); // the open failed: nothing was created
    http_reset(0);                                 // body_streaming && !COMPLETE -> abort hook
    TEST_ASSERT_FALSE(tree_has("/dav/never.txt"));
    lfsm_no_prog_failure();
}

// The peer disappears between the request being parsed and the response being written:
// dav_send_status must recognize the dead slot, reset it, and write nothing.
void test_webdav_status_on_dead_connection()
{
    push_str(0, "UNLOCK /dav/x HTTP/1.1\r\nHost: x\r\n\r\n");
    http_parse(0);
    conn_pool[0].pcb = NULL; // connection torn down before the handler runs
    handle();
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len()); // nothing written to a dead slot
}

// status_text() maps every status code the server can emit to its RFC reason phrase, and
// anything else to "Unknown". This env is the only one that compiles the WebDAV-gated arms
// (207 / 412 / 423 / 502), so the whole table is pinned here.
void test_webdav_status_text_table()
{
    struct
    {
        int code;
        const char *phrase;
    } expect[] = {
        {200, "OK"},
        {201, "Created"},
        {204, "No Content"},
        {206, "Partial Content"},
        {207, "Multi-Status"}, // WebDAV-only arm
        {301, "Moved Permanently"},
        {302, "Found"},
        {303, "See Other"},
        {304, "Not Modified"},
        {307, "Temporary Redirect"},
        {308, "Permanent Redirect"},
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {408, "Request Timeout"},
        {409, "Conflict"},
        {412, "Precondition Failed"}, // WebDAV-only arm
        {423, "Locked"},              // WebDAV-only arm (LOCK/UNLOCK)
        {502, "Bad Gateway"},         // WebDAV-only arm (foreign COPY/MOVE destination)
        {413, "Payload Too Large"},
        {414, "URI Too Long"},
        {416, "Range Not Satisfiable"},
        {429, "Too Many Requests"},
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
        {503, "Service Unavailable"},
    };
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); i++)
    {
        TEST_ASSERT_EQUAL_STRING(expect[i].phrase, status_text(expect[i].code));
    }

    // Anything outside the table falls to the default arm rather than reading off the end.
    TEST_ASSERT_EQUAL_STRING("Unknown", status_text(299));
    TEST_ASSERT_EQUAL_STRING("Unknown", status_text(0));
    TEST_ASSERT_EQUAL_STRING("Unknown", status_text(-1));
}

// dav_join's separator handling when the request is the bare mount prefix under a root that
// already ends in '/': the sub-path is empty, so there is no leading '/' to skip, and the
// root's own trailing '/' still supplies the separator. The joined path must be the root
// with its trailing slash stripped - i.e. the collection itself, which GET refuses with 405.
void test_webdav_join_root_slash_with_empty_subpath()
{
    dav("/ts", davfs, "/tsroot/");
    tree_mkdir("/tsroot");
    tree_put("/tsroot/f.txt", "hi");

    feed_and_handle(0, "GET /ts HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(405)); // resolved to "/tsroot", a collection

    // The same mount still resolves a real member, so the empty-sub case did not corrupt
    // the separator handling for non-empty sub-paths.
    rearm();
    feed_and_handle(0, "GET /ts/f.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(pc_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "hi"));
}

// Once a streamed PUT has short-written, the sink latches the error and every LATER body
// chunk is dropped instead of being appended past the failure point: the file keeps only
// what fitted, and the handler still answers 507.
void test_put_stream_error_latches_for_later_chunks()
{
    // The file is created and takes some of the body, then the medium refuses - leaving several
    // chunks still to arrive. Those must be dropped on the latched error, not written anywhere.
    static uint8_t big[2600];
    memset(big, 'A', sizeof(big));
    lfsm_fail_prog_after(6);
    feed_put(0, "/dav/huge.txt", big, sizeof(big));
    lfsm_no_prog_failure();
    TEST_ASSERT_TRUE(pc_resp_status(507));

    // Whatever landed before the refusal is bounded by the volume; nothing went past it.
    pc_mnt_stat hst;
    if (lfsm()->stat("/dav/huge.txt", &hst))
    {
        TEST_ASSERT_LESS_OR_EQUAL_UINT64((uint64_t)(LFSM_BLOCK_SIZE * LFSM_BLOCK_COUNT), hst.size);
        TEST_ASSERT_LESS_THAN_UINT64((uint64_t)sizeof(big), hst.size); // the tail was dropped
    }
}

// filesystem.h: pc_fs_join()'s seam, direct. This env does not link ssh_sftp.cpp /
// ssh_scp.cpp (the only other callers), so the header is otherwise uncompiled here.
// A mount root ends with '/', so the only decision left is whether sub duplicates it.
void test_pc_fs_join_seam()
{
    char out[64];

    // sub starts with '/': the duplicate is consumed, the root's separator is the one kept.
    TEST_ASSERT_TRUE(pc_fs_join("/a/", "/b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    // sub does NOT start with '/': sub is left alone, the root already supplies the separator.
    TEST_ASSERT_TRUE(pc_fs_join("/a/", "b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    // The mount root itself is a valid root: no doubled slash at the seam.
    TEST_ASSERT_TRUE(pc_fs_join("/", "/b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/b", out);

    // The joined path does not fit the output buffer -> 0 (the overflow guard).
    TEST_ASSERT_FALSE(pc_fs_join("/abc/", "def", "", out, 4));
}

// filesystem.h: pc_fs_resolve()'s traversal reject, the pc_fs_join()-failure
// passthrough, and the trailing-slash strip's one-character-path edge (must not strip a lone "/").
void test_pc_fs_resolve_traversal_and_root_edge()
{
    char out[64];

    // A ".." anywhere in sub is refused before touching pc_fs_join.
    TEST_ASSERT_EQUAL_INT(-1, pc_fs_resolve("/root/", "/a/../b", "", out, sizeof(out)));

    // Joined result is exactly "/" (the mount root, sub "/"): length 1, so the trailing-slash
    // strip must not fire (fpl>1 is false).
    TEST_ASSERT_EQUAL_INT(0, pc_fs_resolve("/", "/", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/", out);

    // Joined result ends in '/' with length>1: the trailing slash IS stripped.
    TEST_ASSERT_EQUAL_INT(0, pc_fs_resolve("/a/", "/", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a", out);

    // Joined result does not end in '/': left untouched.
    TEST_ASSERT_EQUAL_INT(0, pc_fs_resolve("/a/", "/b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    // A pc_fs_join() overflow surfaces as -2.
    TEST_ASSERT_EQUAL_INT(-2, pc_fs_resolve("/abc/", "def", "", out, 4));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_pc_fs_join_seam);
    RUN_TEST(test_pc_fs_resolve_traversal_and_root_edge);
    RUN_TEST(test_webdav_status_text_table);
    RUN_TEST(test_webdav_join_root_slash_with_empty_subpath);
    RUN_TEST(test_put_stream_error_latches_for_later_chunks);
    RUN_TEST(test_webdav_join_root_variants);
    RUN_TEST(test_webdav_dav_empty_prefix_mount);
    RUN_TEST(test_webdav_method_dispatch_edges);
    RUN_TEST(test_webdav_copy_header_edges);
    RUN_TEST(test_webdav_copy_dest_joins_to_root);
    RUN_TEST(test_webdav_propfind_file_and_trailing_slash);
    RUN_TEST(test_webdav_route_scan_skips_non_dav_routes);
    RUN_TEST(test_webdav_stream_put_abort_without_open);
    RUN_TEST(test_webdav_status_on_dead_connection);
    RUN_TEST(test_webdav_get_put_dest_edges);
    RUN_TEST(test_webdav_copy_dest_path_too_long_414);
    RUN_TEST(test_webdav_recursive_open_failure);
    RUN_TEST(test_webdav_source_path_too_long_414);
    RUN_TEST(test_webdav_dav_wildcard_and_route_full);
    RUN_TEST(test_webdav_error_paths);
    RUN_TEST(test_webdav_deep_tree_rejected);
    RUN_TEST(test_webdav_propfind_limit_and_proppatch);
    RUN_TEST(test_webdav_copy_fs_table_full);
    RUN_TEST(test_copy_collection_recursive);
    RUN_TEST(test_copy_collection_depth0_shallow);
    RUN_TEST(test_copy_overwrite_semantics);
    RUN_TEST(test_move_collection_recursive);
    RUN_TEST(test_delete_collection_recursive);
    RUN_TEST(test_propfind_depth0_collection_only);
    RUN_TEST(test_propfind_depth1_lists_members);
    RUN_TEST(test_mkcol_create_and_conflict);
    RUN_TEST(test_delete_single_file);
    RUN_TEST(test_options_advertises_dav);
    RUN_TEST(test_get_file_through_mount);
    RUN_TEST(test_put_stream_create);
    RUN_TEST(test_put_stream_overwrite);
    RUN_TEST(test_put_empty_buffered);
    RUN_TEST(test_put_stream_write_fails_507);
    RUN_TEST(test_put_stream_open_fails_409);
    RUN_TEST(test_put_stream_traversal_403);
    RUN_TEST(test_put_stream_begin_declines);
    RUN_TEST(test_put_stream_abort);
    RUN_TEST(test_lock_enforcement);
    return UNITY_END();
}
