// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "lfs_mock.h"
#include "network_drivers/presentation/http/http.h"
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include "server/storage/filesystem/filesystem.h"

#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

static const protocore_mnt_backend *davfs;

static void feed_and_handle(uint8_t slot, const char *req)
{
    push_str(slot, req);
    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    handle();
}

static void rearm()
{
    conn_pool[0] = (TcpConn){0};
    conn_pool[0].id = 0;
    conn_pool[0].state = CONN_ACTIVE;
    conn_pool[0].proto = PROTO_HTTP;
    conn_pool[0].pcb = protocore_net_host_pcb();
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    tcp_capture_reset();
}

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
    protocore_mnt_stat st;
    return lfsm()->stat(path, &st) && st.is_dir;
}
static proto_bool tree_content_eq(const char *path, const char *exp)
{
    protocore_mnt_stat st;
    if (!lfsm()->stat(path, &st) || st.is_dir || st.size != strlen(exp))
    {
        return PROTO_FALSE;
    }
    char buf[512];
    if (st.size >= sizeof(buf))
    {
        return PROTO_FALSE;
    }
    int h = lfsm()->open(path, PROTOCORE_MNT_READ);
    if (h < 0)
    {
        return PROTO_FALSE;
    }
    int n = lfsm()->read(h, buf, sizeof(buf));
    lfsm()->close(h);
    return (n == (int)strlen(exp) && memcmp(buf, exp, (size_t)n) == 0) ? PROTO_TRUE : PROTO_FALSE;
}
static proto_bool protocore_resp_status(int code)
{
    char want[20];
    snprintf(want, sizeof(want), "HTTP/1.1 %d", code);
    return strstr(tcp_captured(), want) != NULL;
}

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
    protocore_server_reset();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = protocore_net_host_pcb();
        HttpConnV.slot = i;
        HttpConn.reset(protocore_http_conn_span());
    }
    Ws.init(protocore_ws_span());
    Sse.init(protocore_sse_span());
    tcp_capture_reset();
    lfsm_format();
    davfs = lfsm();
    MntV.args.backend = davfs;
    Mnt.mount(mnt_work);

    lfsm_mkdir("/dav");
    dav("/dav", davfs, "/dav");
}

void tearDown()
{
    tcp_capture_disable();
}

void test_copy_collection_recursive()
{
    populate_src();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/dst"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/a.txt", "alpha"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/b.txt", "bravo"));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/dst/sub"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/sub/c.txt", "charlie"));

    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "alpha"));
}

void test_copy_collection_depth0_shallow()
{
    populate_src();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/shallow\r\nDepth: 0\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/shallow"));
    TEST_ASSERT_FALSE(tree_has("/dav/shallow/a.txt"));
    TEST_ASSERT_FALSE(tree_has("/dav/shallow/sub"));
}

void test_copy_overwrite_semantics()
{
    populate_src();
    tree_mkdir("/dav/dst");
    tree_put("/dav/dst/stale.txt", "old");

    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(204));
    TEST_ASSERT_FALSE(tree_has("/dav/dst/stale.txt"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst/a.txt", "alpha"));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst\r\nOverwrite: F\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(412));
}

void test_move_collection_recursive()
{
    populate_src();
    feed_and_handle(0, "MOVE /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/moved\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/moved/sub/c.txt", "charlie"));
    TEST_ASSERT_FALSE(tree_has("/dav/src"));
    TEST_ASSERT_FALSE(tree_has("/dav/src/sub/c.txt"));
}

void test_delete_collection_recursive()
{
    populate_src();
    feed_and_handle(0, "DELETE /dav/src HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(204));
    TEST_ASSERT_FALSE(tree_has("/dav/src"));
    TEST_ASSERT_FALSE(tree_has("/dav/src/a.txt"));
    TEST_ASSERT_FALSE(tree_has("/dav/src/sub/c.txt"));
}

void test_propfind_depth0_collection_only()
{
    populate_src();
    feed_and_handle(0, "PROPFIND /dav/src HTTP/1.1\r\nHost: x\r\nDepth: 0\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(protocore_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "/dav/src"));
    TEST_ASSERT_NULL(strstr(r, "a.txt"));
}

void test_propfind_depth1_lists_members()
{
    populate_src();
    feed_and_handle(0, "PROPFIND /dav/src HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(protocore_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "a.txt"));
    TEST_ASSERT_NOT_NULL(strstr(r, "b.txt"));
    TEST_ASSERT_NOT_NULL(strstr(r, "sub"));
}

void test_mkcol_create_and_conflict()
{
    feed_and_handle(0, "MKCOL /dav/newdir HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_is_dir("/dav/newdir"));

    rearm();
    feed_and_handle(0, "MKCOL /dav/newdir HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(405));
}

void test_delete_single_file()
{
    populate_src();
    feed_and_handle(0, "DELETE /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(204));
    TEST_ASSERT_FALSE(tree_has("/dav/src/a.txt"));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/b.txt", "bravo"));
}

void test_options_advertises_dav()
{
    feed_and_handle(0, "OPTIONS /dav/ HTTP/1.1\r\nHost: x\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(protocore_resp_status(200) || protocore_resp_status(204));
    TEST_ASSERT_NOT_NULL(strstr(r, "DAV:"));
    TEST_ASSERT_NOT_NULL(strstr(r, "PROPFIND"));
}

void test_get_file_through_mount()
{
    populate_src();
    feed_and_handle(0, "GET /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(r, "alpha"));
}

static void feed_put(uint8_t slot, const char *path, const uint8_t *body, size_t n)
{
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "PUT %s HTTP/1.1\r\nHost: x\r\nContent-Length: %u\r\n\r\n", path, (unsigned)n);
    push_str(slot, hdr);
    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    for (size_t off = 0; off < n;)
    {
        size_t chunk = n - off > 200 ? 200 : n - off;
        push_bytes(slot, body + off, chunk);
        HttpConnV.slot = slot;
        HttpConn.parse(protocore_http_conn_span());
        off += chunk;
    }
    handle();
}

void test_put_stream_create()
{
    const char *body = "hello world";
    feed_put(0, "/dav/up.txt", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/up.txt", "hello world"));
}

void test_put_stream_overwrite()
{
    tree_put("/dav/up.txt", "stale contents");
    const char *body = "new";
    feed_put(0, "/dav/up.txt", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(protocore_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/up.txt", "new"));
}

void test_put_empty_buffered()
{
    feed_and_handle(0, "PUT /dav/empty.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_has("/dav/empty.txt"));
}

void test_put_stream_write_fails_507()
{
    lfsm_fail_prog_after(3);
    static uint8_t big[2100];
    memset(big, 'A', sizeof(big));
    feed_put(0, "/dav/big.txt", big, sizeof(big));
    TEST_ASSERT_TRUE(protocore_resp_status(507));
}

void test_put_stream_open_fails_409()
{
    lfsm_hold_all_handles();
    const char *body = "abc";
    feed_put(0, "/dav/overflow.txt", (const uint8_t *)body, strlen(body));
    lfsm_release_handles();
    TEST_ASSERT_TRUE(protocore_resp_status(409));
}

void test_put_stream_traversal_403()
{
    const char *body = "abc";
    feed_put(0, "/dav/../secret", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(protocore_resp_status(403));
}

void test_put_stream_begin_declines()
{

    feed_and_handle(0, "POST /dav/x.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc");
    rearm();

    const char *body = "abc";
    feed_put(0, "/nomatch/y.txt", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(protocore_resp_status(404));
}

void test_put_stream_abort()
{

    push_str(0, "PUT /dav/ab.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nabcd");
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    TEST_ASSERT_TRUE(tree_has("/dav/ab.txt"));
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());

    TEST_ASSERT_TRUE(tree_has("/dav/ab.txt"));
}

static void feed_put_if(uint8_t slot, const char *path, const char *if_hdr, const uint8_t *body, size_t n)
{
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "PUT %s HTTP/1.1\r\nHost: x\r\nIf: %s\r\nContent-Length: %u\r\n\r\n", path, if_hdr,
             (unsigned)n);
    push_str(slot, hdr);
    HttpConnV.slot = slot;
    HttpConn.parse(protocore_http_conn_span());
    for (size_t off = 0; off < n;)
    {
        size_t chunk = n - off > 200 ? 200 : n - off;
        push_bytes(slot, body + off, chunk);
        HttpConnV.slot = slot;
        HttpConn.parse(protocore_http_conn_span());
        off += chunk;
    }
    handle();
}

static proto_bool extract_lock_token(const char *resp, char *out, size_t cap)
{
    const char *p = strstr(resp, "opaquelocktoken:");
    if (!p)
    {
        return PROTO_FALSE;
    }
    const char *e = strchr(p, '>');
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

void test_lock_enforcement()
{
    populate_src();
    feed_and_handle(0, "LOCK /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    char token[48];
    TEST_ASSERT_TRUE(extract_lock_token(tcp_captured(), token, sizeof(token)));

    rearm();
    feed_put(0, "/dav/src/a.txt", (const uint8_t *)"HACKED", 6);
    TEST_ASSERT_TRUE(protocore_resp_status(423));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "alpha"));

    rearm();
    feed_and_handle(0, "UNLOCK /dav/src/a.txt HTTP/1.1\r\nHost: x\r\nLock-Token: <opaquelocktoken:nope>\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));

    rearm();
    char if_hdr[64];
    snprintf(if_hdr, sizeof(if_hdr), "(<%s>)", token);
    feed_put_if(0, "/dav/src/a.txt", if_hdr, (const uint8_t *)"updated", 7);
    TEST_ASSERT_TRUE(protocore_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "updated"));

    rearm();
    char ltok[64];
    snprintf(ltok, sizeof(ltok), "<%s>", token);
    char unlock[160];
    snprintf(unlock, sizeof(unlock), "UNLOCK /dav/src/a.txt HTTP/1.1\r\nHost: x\r\nLock-Token: %s\r\n\r\n", ltok);
    feed_and_handle(0, unlock);
    TEST_ASSERT_TRUE(protocore_resp_status(204));

    rearm();
    feed_put(0, "/dav/src/a.txt", (const uint8_t *)"free", 4);
    TEST_ASSERT_TRUE(protocore_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/src/a.txt", "free"));
}

void test_webdav_error_paths()
{
    feed_and_handle(0, "DELETE /dav/nope HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(404));

    rearm();
    populate_src();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(400));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /other/x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(502));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/../x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(403));

    rearm();
    feed_and_handle(0, "COPY /dav/gone HTTP/1.1\r\nHost: x\r\nDestination: /dav/x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(404));

    rearm();
    tree_mkdir("/dav/mvdst");
    feed_and_handle(0, "MOVE /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/mvdst\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(204));

    rearm();
    feed_and_handle(0, "PROPFIND /dav/nope HTTP/1.1\r\nHost: x\r\nDepth: 0\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(404));

    rearm();
    populate_src();
    feed_and_handle(0, "PROPFIND /dav/src HTTP/1.1\r\nHost: x\r\nDepth: infinity\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(403));
}

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
    TEST_ASSERT_TRUE(protocore_resp_status(403));
    TEST_ASSERT_TRUE(tree_has("/dav/deep"));

    rearm();
    feed_and_handle(0, "COPY /dav/deep HTTP/1.1\r\nHost: x\r\nDestination: /dav/dcopy\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));
}

void test_webdav_propfind_limit_and_proppatch()
{
    tree_mkdir("/dav/big");
    for (int i = 0; i < 40; i++)
    {
        char p[64];
        snprintf(p, sizeof p, "/dav/big/f%02d.txt", i);
        tree_put(p, "x");
    }
    feed_and_handle(0, "PROPFIND /dav/big HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(207));

    rearm();
    tree_put("/dav/file.txt", "data");
    feed_and_handle(0, "PROPPATCH /dav/file.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(207));

    rearm();
    feed_and_handle(0, "PROPPATCH /dav/nope HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(404));
}

void test_webdav_copy_fs_table_full()
{
    tree_put("/dav/f.txt", "data");
    tree_mkdir("/dav/d");
    lfsm_fail_prog_always();
    feed_and_handle(0, "COPY /dav/f.txt HTTP/1.1\r\nHost: x\r\nDestination: /dav/fc\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));

    rearm();
    feed_and_handle(0, "COPY /dav/d HTTP/1.1\r\nHost: x\r\nDestination: /dav/dc\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));
}

void test_webdav_get_put_dest_edges()
{
    feed_and_handle(0, "GET /dav/missing.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(404));

    rearm();
    feed_and_handle(0, "HEAD /dav/missing.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(404));

    rearm();
    tree_mkdir("/dav/adir");
    feed_and_handle(0, "GET /dav/adir HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(405));

    rearm();
    tree_put("/dav/f.txt", "hi");
    feed_and_handle(0, "COPY /dav/f.txt HTTP/1.1\r\nHost: x\r\nDestination: /dav/g.txt/\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_has("/dav/g.txt"));

    rearm();
    lfsm_fail_prog_after(1);
    feed_and_handle(0, "PUT /dav/newfile.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));
}

void test_webdav_copy_dest_path_too_long_414()
{

    static char longroot[241];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0';
    dav("/d2", davfs, longroot);

    char req[128];

    snprintf(req, sizeof(req), "COPY /d2/s HTTP/1.1\r\nHost: x\r\nDestination: /d2/destination_file_name.txt\r\n\r\n");
    feed_and_handle(0, req);
    TEST_ASSERT_TRUE(protocore_resp_status(414));
}

void test_webdav_recursive_open_failure()
{

    tree_put("/dav/locked.txt", "data");
    lfsm_fail_prog_always();
    feed_and_handle(0, "DELETE /dav/locked.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(403));
    lfsm_no_prog_failure();
    TEST_ASSERT_TRUE(tree_has("/dav/locked.txt"));

    rearm();
    populate_src();
    lfsm_fail_prog_always();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/cdst\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));
    lfsm_no_prog_failure();
}

void test_webdav_source_path_too_long_414()
{
    static char longroot[255];
    memset(longroot, 'r', sizeof(longroot) - 1);
    longroot[0] = '/';
    longroot[sizeof(longroot) - 1] = '\0';
    dav("/d3", davfs, longroot);
    feed_and_handle(0, "GET /d3/x HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(414));
}

void test_webdav_dav_wildcard_and_route_full()
{

    dav("/w*", davfs, "/w");
    tree_put("/w/f.txt", "hi");
    feed_and_handle(0, "GET /w/f.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));

    rearm();
    for (int i = 0; i < MAX_ROUTES; i++)
    {
        char p[16];
        snprintf(p, sizeof p, "/r%d", i);
        dav(p, davfs, "/r");
    }
    dav("/dropped", davfs, "/d");
    feed_and_handle(0, "GET /dropped/x HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(404));
}

void test_webdav_join_root_variants()
{

    dav("/ts", davfs, "/tsroot/");
    tree_put("/tsroot/f.txt", "hi");
    feed_and_handle(0, "GET /ts/f.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "hi"));

    rearm();
    dav("/er", davfs, "");
    tree_put("/f2.txt", "yo");
    feed_and_handle(0, "GET /er/f2.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "yo"));

    rearm();
    feed_and_handle(0, "GET /er HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(405));

    rearm();
    dav("/nr", davfs, NULL);
    tree_put("/n.txt", "nn");
    feed_and_handle(0, "GET /nr/n.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "nn"));
}

void test_webdav_dav_empty_prefix_mount()
{
    dav("", davfs, "/ep");
    tree_put("/ep/x.txt", "ee");
    feed_and_handle(0, "GET /x.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "ee"));
}

void test_webdav_method_dispatch_edges()
{
    populate_src();
    feed_and_handle(0, "HEAD /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Content-Length: 5"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "alpha"));

    rearm();
    feed_and_handle(0, "BREW /dav/src/a.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(405));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Allow:"));

    rearm();
    tree_put("/dav/e.txt", "old");
    feed_and_handle(0, "PUT /dav/e.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(204));

    rearm();
    feed_and_handle(0, "MOVE /dav/gone HTTP/1.1\r\nHost: x\r\nDestination: /dav/mv\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));
    TEST_ASSERT_FALSE(tree_has("/dav/mv"));

    rearm();
    lfsm_fail_prog_after(1);
    feed_and_handle(0, "MKCOL /dav/newcol HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(409));
}

void test_webdav_copy_header_edges()
{
    populate_src();

    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: notapath\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(400));

    rearm();
    tree_mkdir("/dav/dst2");
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst2\r\nOverwrite: f\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(412));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/dst2\r\nOverwrite: T\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(204));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/dst2/a.txt", "alpha"));

    rearm();
    feed_and_handle(0, "COPY /dav/src HTTP/1.1\r\nHost: x\r\nDestination: /dav/deep1\r\nDepth: 1\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(201));
    TEST_ASSERT_TRUE(tree_content_eq("/dav/deep1/sub/c.txt", "charlie"));
}

void test_webdav_copy_dest_joins_to_root()
{
    dav("/z", davfs, "");
    tree_put("/src.txt", "s");
    feed_and_handle(0, "COPY /z/src.txt HTTP/1.1\r\nHost: x\r\nDestination: /z\r\n\r\n");

    TEST_ASSERT_TRUE(protocore_resp_status(409));
    TEST_ASSERT_TRUE(tree_has("/src.txt"));
}

void test_webdav_propfind_file_and_trailing_slash()
{
    tree_put("/dav/doc.txt", "hello");
    feed_and_handle(0, "PROPFIND /dav/doc.txt HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    const char *r = tcp_captured();
    TEST_ASSERT_TRUE(protocore_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "getcontentlength"));
    TEST_ASSERT_NOT_NULL(strstr(r, "getcontenttype"));
    TEST_ASSERT_NULL(strstr(r, "<D:collection/>"));

    rearm();
    tree_mkdir("/dav/col");
    tree_put("/dav/col/m.txt", "m");
    feed_and_handle(0, "PROPFIND /dav/col/ HTTP/1.1\r\nHost: x\r\nDepth: 1\r\n\r\n");
    r = tcp_captured();
    TEST_ASSERT_TRUE(protocore_resp_status(207));
    TEST_ASSERT_NOT_NULL(strstr(r, "<D:href>/dav/col/</D:href>"));
    TEST_ASSERT_NOT_NULL(strstr(r, "/dav/col/m.txt"));
}

static void h_plain(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "plain");
}

void test_webdav_route_scan_skips_non_dav_routes()
{
    on_http("/plain", HTTP_GET, h_plain);
    feed_and_handle(0, "GET /plain HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "plain"));

    rearm();
    const char *body = "abc";
    feed_put(0, "/plain", (const uint8_t *)body, strlen(body));
    TEST_ASSERT_TRUE(protocore_resp_status(405));
}

void test_webdav_stream_put_abort_without_open()
{
    lfsm_fail_prog_always();
    push_str(0, "PUT /dav/never.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nabcd");
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    TEST_ASSERT_FALSE(tree_has("/dav/never.txt"));
    HttpConnV.slot = 0;
    HttpConn.reset(protocore_http_conn_span());
    TEST_ASSERT_FALSE(tree_has("/dav/never.txt"));
    lfsm_no_prog_failure();
}

void test_webdav_status_on_dead_connection()
{
    push_str(0, "UNLOCK /dav/x HTTP/1.1\r\nHost: x\r\n\r\n");
    HttpConnV.slot = 0;
    HttpConn.parse(protocore_http_conn_span());
    conn_pool[0].pcb = NULL;
    handle();
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

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
        {207, "Multi-Status"},
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
        {412, "Precondition Failed"},
        {423, "Locked"},
        {502, "Bad Gateway"},
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
        HttpV.code = expect[i].code;
        Http.status_text(protocore_http_span());
        TEST_ASSERT_EQUAL_STRING(expect[i].phrase, HttpV.text);
    }

    // A code the table has no phrase for reads "Unknown" rather than an empty string, so a response
    // line is always well formed.
    static const int unknown[3] = {299, 0, -1};
    for (size_t i = 0; i < 3u; i++)
    {
        HttpV.code = unknown[i];
        Http.status_text(protocore_http_span());
        TEST_ASSERT_EQUAL_STRING("Unknown", HttpV.text);
    }
}

void test_webdav_join_root_slash_with_empty_subpath()
{
    dav("/ts", davfs, "/tsroot/");
    tree_mkdir("/tsroot");
    tree_put("/tsroot/f.txt", "hi");

    feed_and_handle(0, "GET /ts HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(405));

    rearm();
    feed_and_handle(0, "GET /ts/f.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(protocore_resp_status(200));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "hi"));
}

void test_put_stream_error_latches_for_later_chunks()
{

    static uint8_t big[2600];
    memset(big, 'A', sizeof(big));
    lfsm_fail_prog_after(6);
    feed_put(0, "/dav/huge.txt", big, sizeof(big));
    lfsm_no_prog_failure();
    TEST_ASSERT_TRUE(protocore_resp_status(507));

    protocore_mnt_stat hst;
    if (lfsm()->stat("/dav/huge.txt", &hst))
    {
        TEST_ASSERT_LESS_OR_EQUAL_UINT64((uint64_t)(LFSM_BLOCK_SIZE * LFSM_BLOCK_COUNT), hst.size);
        TEST_ASSERT_LESS_THAN_UINT64((uint64_t)sizeof(big), hst.size);
    }
}

void test_protocore_fs_join_seam()
{
    char out[64];

    TEST_ASSERT_TRUE(protocore_fs_join("/a/", "/b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    TEST_ASSERT_TRUE(protocore_fs_join("/a/", "b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    TEST_ASSERT_TRUE(protocore_fs_join("/", "/b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/b", out);

    TEST_ASSERT_FALSE(protocore_fs_join("/abc/", "def", "", out, 4));
}

void test_protocore_fs_resolve_traversal_and_root_edge()
{
    char out[64];

    TEST_ASSERT_EQUAL_INT(-1, protocore_fs_resolve("/root/", "/a/../b", "", out, sizeof(out)));

    TEST_ASSERT_EQUAL_INT(0, protocore_fs_resolve("/", "/", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/", out);

    TEST_ASSERT_EQUAL_INT(0, protocore_fs_resolve("/a/", "/", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a", out);

    TEST_ASSERT_EQUAL_INT(0, protocore_fs_resolve("/a/", "/b", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    TEST_ASSERT_EQUAL_INT(-2, protocore_fs_resolve("/abc/", "def", "", out, 4));
}
