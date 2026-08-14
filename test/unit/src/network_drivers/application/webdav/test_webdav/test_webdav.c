// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the WebDAV wire core (network_drivers/application/webdav/webdav.h).
//
// RFC 4918 sec 9.10.5 prints a Lock Compatibility Table with all six outcomes spelled out: against
// no lock both a shared and an exclusive request are granted, against a shared lock only a shared
// one is, and against an exclusive lock neither is. test_rfc4918_lock_compatibility_table walks
// every cell of that table, and it is the load-bearing case here because the lock manager is the
// only part of this module that can lose data if it is wrong - a granted lock that should have been
// refused lets two writers into the same resource.
//
// The rest is anchored on sec 10.2's Depth ABNF, sec 10.4.2's If-header ABNF with the worked
// examples of secs 10.4.6 and 10.4.7, sec 9.1 for the 207 Multi-Status document, and XML 1.0
// sec 2.4 for the five predefined entities the escaper writes.

#include "network_drivers/application/webdav/webdav.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 4918 sec 9.10.5, every cell:
//   Current State     Shared Lock OK   Exclusive Lock OK
//   None              True             True
//   Shared Lock       True             False
//   Exclusive Lock    False            False
void test_rfc4918_lock_compatibility_table(void)
{
    DavLockTable t;

    // Row "None".
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-s", PROTO_FALSE, PROTO_FALSE, 0));
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-x", PROTO_TRUE, PROTO_FALSE, 0));

    // Row "Shared Lock": another shared is granted, an exclusive is not.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-s1", PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-s2", PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-x", PROTO_TRUE, PROTO_FALSE, 0));

    // Row "Exclusive Lock": neither is granted.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-x1", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-s", PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok-x2", PROTO_TRUE, PROTO_FALSE, 0));
}

// sec 9.10.3: a Depth-infinity lock covers the whole subtree, so it conflicts with a lock anywhere
// under it, while a Depth-0 lock covers only its own resource. sec 5.2 makes the boundary a path
// segment, so a lock on /a must not reach /ab.
void test_lock_scope_follows_depth_and_segment_boundaries(void)
{
    DavLockTable t;

    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/a", "tok-inf", PROTO_TRUE, PROTO_TRUE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/a/b", "tok2", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/a/b/c", "tok3", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/ab", "tok4", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/b", "tok5", PROTO_TRUE, PROTO_FALSE, 0));

    // A Depth-0 lock reaches no further than itself.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/a", "tok0", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/a/b", "tok6", PROTO_TRUE, PROTO_FALSE, 0));

    // A new Depth-infinity lock over an existing lock inside its subtree conflicts too.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/a/b", "tokx", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/a", "toky", PROTO_TRUE, PROTO_TRUE, 0));

    // A Depth-infinity lock on the root covers every path below it.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/", "root", PROTO_TRUE, PROTO_TRUE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/anything/at/all", "tokz", PROTO_TRUE, PROTO_FALSE, 0));
}

// sec 8.3: a collection URL with and without its trailing slash names the same resource, so a lock
// taken on one must be found by a query on the other.
void test_lock_paths_normalize_the_trailing_slash(void)
{
    DavLockTable t;
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/dir/", "tok", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_find(&t, "/dir"));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_find(&t, "/dir/"));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/dir", "tok2", PROTO_TRUE, PROTO_FALSE, 0));

    // The root keeps its single slash rather than normalizing to nothing.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/", "root", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_find(&t, "/"));
}

// sec 7.1: "a lock-null resource ... a write MUST fail unless the lock token is submitted". A write
// with no token or the wrong token is denied; the matching token lets it through; an unlocked
// resource is always writable.
void test_write_needs_the_covering_lock_token(void)
{
    DavLockTable t;
    protocore_dav_lock_init(&t);
    TEST_ASSERT_TRUE(protocore_dav_lock_can_write(&t, "/r", NULL)); // nothing locked yet

    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "opaquelocktoken:1", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_FALSE(protocore_dav_lock_can_write(&t, "/r", NULL));
    TEST_ASSERT_FALSE(protocore_dav_lock_can_write(&t, "/r", "opaquelocktoken:2"));
    TEST_ASSERT_TRUE(protocore_dav_lock_can_write(&t, "/r", "opaquelocktoken:1"));
    TEST_ASSERT_TRUE(protocore_dav_lock_can_write(&t, "/other", NULL));

    // A Depth-infinity lock gates its whole subtree the same way.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/c", "opaquelocktoken:9", PROTO_TRUE, PROTO_TRUE, 0));
    TEST_ASSERT_FALSE(protocore_dav_lock_can_write(&t, "/c/deep/file", NULL));
    TEST_ASSERT_TRUE(protocore_dav_lock_can_write(&t, "/c/deep/file", "opaquelocktoken:9"));

    // UNLOCK releases by token and the resource becomes writable again.
    TEST_ASSERT_TRUE(protocore_dav_lock_release(&t, "opaquelocktoken:9"));
    TEST_ASSERT_TRUE(protocore_dav_lock_can_write(&t, "/c/deep/file", NULL));
    TEST_ASSERT_FALSE(protocore_dav_lock_release(&t, "opaquelocktoken:9")); // already gone
}

// sec 6.6: "a lock is destroyed ... when its timeout expires", and sec 9.10.2 lets a LOCK refresh
// push the timeout out. A lock with no timeout is never swept.
void test_lock_timeout_and_refresh(void)
{
    DavLockTable t;
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "tok", PROTO_TRUE, PROTO_FALSE, 100));

    TEST_ASSERT_EQUAL_size_t(0, protocore_dav_lock_sweep(&t, 99)); // not yet
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_find(&t, "/r"));

    // A refresh moves the expiry, so the second the lock would have died passes harmlessly.
    const DavLock *l = protocore_dav_lock_refresh(&t, "tok", 200);
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQUAL_UINT32(200, l->expiry_s);
    TEST_ASSERT_EQUAL_size_t(0, protocore_dav_lock_sweep(&t, 100));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_find(&t, "/r"));

    TEST_ASSERT_EQUAL_size_t(1, protocore_dav_lock_sweep(&t, 200)); // the expiry second itself
    TEST_ASSERT_NULL(protocore_dav_lock_find(&t, "/r"));
    TEST_ASSERT_NULL(protocore_dav_lock_refresh(&t, "tok", 300)); // no live lock has that token

    // Expiry 0 means no timeout.
    protocore_dav_lock_init(&t);
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/r", "forever", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_dav_lock_sweep(&t, 0xFFFFFFFFu));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_find(&t, "/r"));
}

// The table is a fixed structural bound: once every slot holds a lock, a further non-conflicting
// LOCK is refused rather than overwriting one.
void test_lock_table_is_bounded(void)
{
    DavLockTable t;
    protocore_dav_lock_init(&t);
    char path[16];
    char token[16];
    for (int i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        path[0] = '/';
        path[1] = (char)('a' + i);
        path[2] = '\0';
        token[0] = 't';
        token[1] = (char)('a' + i);
        token[2] = '\0';
        TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, path, token, PROTO_TRUE, PROTO_FALSE, 0));
    }
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/zz", "tzz", PROTO_TRUE, PROTO_FALSE, 0));

    // Freeing one slot makes room again.
    TEST_ASSERT_TRUE(protocore_dav_lock_release(&t, "ta"));
    TEST_ASSERT_NOT_NULL(protocore_dav_lock_acquire(&t, "/zz", "tzz", PROTO_TRUE, PROTO_FALSE, 0));
}

// A path or token longer than its fixed field is refused: a silently truncated lock would guard the
// wrong resource, or answer to a token nobody holds.
void test_lock_oversized_path_and_token_are_refused(void)
{
    DavLockTable t;
    protocore_dav_lock_init(&t);

    char long_path[PROTOCORE_DAV_LOCK_PATH_MAX + 8];
    memset(long_path, 'p', sizeof(long_path) - 1);
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, long_path, "tok", PROTO_TRUE, PROTO_FALSE, 0));

    char long_token[PROTOCORE_DAV_LOCK_TOKEN_MAX + 8];
    memset(long_token, 't', sizeof(long_token) - 1);
    long_token[sizeof(long_token) - 1] = '\0';
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/r", long_token, PROTO_TRUE, PROTO_FALSE, 0));

    TEST_ASSERT_NULL(protocore_dav_lock_acquire(NULL, "/r", "tok", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, NULL, "tok", PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_acquire(&t, "/r", NULL, PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_NULL(protocore_dav_lock_find(NULL, "/r"));
    TEST_ASSERT_NULL(protocore_dav_lock_find(&t, NULL));
    TEST_ASSERT_FALSE(protocore_dav_lock_release(NULL, "tok"));
    TEST_ASSERT_FALSE(protocore_dav_lock_release(&t, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_dav_lock_sweep(NULL, 1));
}

// RFC 4918 sec 10.4.2: State-token = Coded-URL, and a Coded-URL is "<" Simple-ref ">" inside a
// condition list. The examples of sec 10.4.6 and 10.4.7 are taken verbatim:
//   (<urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2> ["I am an ETag"])
//   (Not <urn:uuid:181d4fae-...> <urn:uuid:58f202ac-...>)
// and the tagged form of sec 10.4.7's Tagged-list, whose Resource-Tag precedes the first list.
void test_if_header_state_token(void)
{
    char out[64];

    TEST_ASSERT_TRUE(protocore_dav_if_token(
        "(<urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2> [\"I am an ETag\"])", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2", out);

    // "Not" prefixes the condition; the first Coded-URL is still the token the list names.
    TEST_ASSERT_TRUE(protocore_dav_if_token("(Not <urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2>)", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2", out);

    // Tagged-list: the Resource-Tag before the '(' is not the state token.
    TEST_ASSERT_TRUE(protocore_dav_if_token("</resource1> (<opaquelocktoken:abc-pc>)", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("opaquelocktoken:abc-pc", out);

    // An entity-tag-only condition carries no state token.
    TEST_ASSERT_FALSE(protocore_dav_if_token("([\"I am another ETag\"])", out, sizeof(out)));
    // No condition list at all.
    TEST_ASSERT_FALSE(protocore_dav_if_token("<opaquelocktoken:abc>", out, sizeof(out)));
    // Unterminated Coded-URL.
    TEST_ASSERT_FALSE(protocore_dav_if_token("(<opaquelocktoken:abc", out, sizeof(out)));

    // A token that does not fit is refused, not truncated.
    char tiny[8];
    TEST_ASSERT_FALSE(protocore_dav_if_token("(<opaquelocktoken:abc>)", tiny, sizeof(tiny)));

    TEST_ASSERT_FALSE(protocore_dav_if_token(NULL, out, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_dav_if_token("(<a>)", NULL, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_dav_if_token("(<a>)", out, 0));
}

// RFC 4918 sec 10.2: Depth = "Depth" ":" ("0" | "1" | "infinity"). Anything else is not a Depth
// value, so the caller's per-method default stands (sec 10.2 leaves the absent case to the method).
void test_depth_header(void)
{
    TEST_ASSERT_EQUAL_INT(0, protocore_webdav_depth("0", 1));
    TEST_ASSERT_EQUAL_INT(1, protocore_webdav_depth("1", 0));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DAV_DEPTH_INFINITY, protocore_webdav_depth("infinity", 0));
    TEST_ASSERT_EQUAL_INT(0x7fffffff, PROTOCORE_DAV_DEPTH_INFINITY);

    TEST_ASSERT_EQUAL_INT(7, protocore_webdav_depth(NULL, 7));
    TEST_ASSERT_EQUAL_INT(7, protocore_webdav_depth("", 7));
    TEST_ASSERT_EQUAL_INT(7, protocore_webdav_depth("2", 7));
    TEST_ASSERT_EQUAL_INT(7, protocore_webdav_depth("Infinity", 7)); // the ABNF token is lowercase
    TEST_ASSERT_EQUAL_INT(7, protocore_webdav_depth("0 ", 7));
}

// RFC 4918 sec 9 names the methods WebDAV adds to HTTP, and sec 9.1 through 9.11 define them:
// PROPFIND, PROPPATCH, MKCOL, COPY, MOVE, LOCK, UNLOCK, alongside the HTTP methods a DAV server
// still answers. Anything else is not one of them.
void test_method_classification(void)
{
    struct
    {
        const char *name;
        WebDavMethod want;
    } static const CASES[] = {
        {"OPTIONS", DAV_M_OPTIONS}, {"GET", DAV_M_GET},         {"HEAD", DAV_M_HEAD},
        {"PUT", DAV_M_PUT},         {"DELETE", DAV_M_DELETE},   {"PROPFIND", DAV_M_PROPFIND},
        {"PROPPATCH", DAV_M_PROPPATCH}, {"MKCOL", DAV_M_MKCOL}, {"COPY", DAV_M_COPY},
        {"MOVE", DAV_M_MOVE},       {"LOCK", DAV_M_LOCK},       {"UNLOCK", DAV_M_UNLOCK},
        {"POST", DAV_M_UNSUPPORTED}, {"PATCH", DAV_M_UNSUPPORTED}, {"", DAV_M_UNSUPPORTED},
        // RFC 9110 sec 9.1 makes the method token case-sensitive, so a lowercase spelling is a
        // different token and not one of these.
        {"propfind", DAV_M_UNSUPPORTED}, {"Get", DAV_M_UNSUPPORTED},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CASES[i].want, protocore_webdav_method(CASES[i].name), CASES[i].name);
    }
    TEST_ASSERT_EQUAL_INT(DAV_M_UNSUPPORTED, protocore_webdav_method(NULL));
}

// XML 1.0 sec 4.6 defines exactly five predefined entities: amp, lt, gt, apos, quot. A property
// value or an href carrying any of those raw characters would close the element it sits in, so the
// escaper replaces them and leaves everything else alone.
void test_xml_escape(void)
{
    char out[128];

    TEST_ASSERT_EQUAL_size_t(strlen("&amp;&lt;&gt;&quot;&apos;"), protocore_webdav_xml_escape(out, sizeof(out), "&<>\"'"));
    TEST_ASSERT_EQUAL_STRING("&amp;&lt;&gt;&quot;&apos;", out);

    TEST_ASSERT_EQUAL_size_t(strlen("/a b/c.txt"), protocore_webdav_xml_escape(out, sizeof(out), "/a b/c.txt"));
    TEST_ASSERT_EQUAL_STRING("/a b/c.txt", out);

    TEST_ASSERT_EQUAL_size_t(0, protocore_webdav_xml_escape(out, sizeof(out), ""));
    TEST_ASSERT_EQUAL_STRING("", out);

    // A closing tag smuggled into an href cannot survive the escape.
    protocore_webdav_xml_escape(out, sizeof(out), "</D:href><D:evil/>");
    TEST_ASSERT_NULL(strstr(out, "<"));
    TEST_ASSERT_NULL(strstr(out, ">"));

    // The result is always NUL-terminated inside the caller's buffer, whatever gets clipped.
    char small[8];
    memset(small, 0x7F, sizeof(small));
    size_t n = protocore_webdav_xml_escape(small, sizeof(small), "&&&&&&&&");
    TEST_ASSERT_TRUE(n < sizeof(small));
    TEST_ASSERT_EQUAL_size_t(strlen(small), n);
    TEST_ASSERT_EQUAL_size_t(0, protocore_webdav_xml_escape(small, 0, "x"));
}

// RFC 4918 sec 10.3: "The Destination request header specifies the URI that identifies a
// destination resource", and sec 8.3 allows either an absolute URI or an absolute path. RFC 3986
// sec 2.1 makes %HH an octet, so the path is percent-decoded before it names a file.
void test_destination_header_path(void)
{
    char out[128];

    TEST_ASSERT_TRUE(protocore_webdav_dest_path("http://host/p/q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/p/q", out);
    TEST_ASSERT_TRUE(protocore_webdav_dest_path("https://host:8080/p/q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/p/q", out);
    TEST_ASSERT_TRUE(protocore_webdav_dest_path("/p/q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/p/q", out);

    // RFC 3986 sec 2.1: %20 is a space, and the hex digits are case-insensitive.
    TEST_ASSERT_TRUE(protocore_webdav_dest_path("/a%20b/c%2Fd", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a b/c/d", out);
    TEST_ASSERT_TRUE(protocore_webdav_dest_path("/a%2fb", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    // A malformed escape is not a path.
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("/a%zzb", out, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("/a%2", out, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("/a%", out, sizeof(out)));
    // An authority with no path names no resource, and a relative reference is not accepted.
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("http://host", out, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("p/q", out, sizeof(out)));

    // Overflow is a refusal, not a truncated path.
    char tiny[4];
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("/abcdefg", out, 4));
    TEST_ASSERT_TRUE(protocore_webdav_dest_path("/ab", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("/ab", tiny);

    TEST_ASSERT_FALSE(protocore_webdav_dest_path(NULL, out, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("/p", NULL, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_webdav_dest_path("/p", out, 0));
}

// RFC 4918 sec 14.16: a Multi-Status document is a DAV: multistatus element containing one response
// per resource, and sec 14.24 puts href and propstat inside each. sec 14.9 marks a collection with
// an empty collection element inside resourcetype.
void test_multistatus_document_shape(void)
{
    char buf[2048];
    size_t len = protocore_webdav_ms_begin(buf, sizeof(buf), 0);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:multistatus xmlns:D=\"DAV:\">"));

    len = protocore_webdav_ms_entry(buf, sizeof(buf), len, "/dir/", PROTO_TRUE, 0, "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:href>/dir/</D:href>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:collection/>"));

    len = protocore_webdav_ms_entry(buf, sizeof(buf), len, "/dir/f.txt", PROTO_FALSE, 1234,
                                    "Sun, 06 Nov 1994 08:49:37 GMT", "text/plain");
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:href>/dir/f.txt</D:href>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:getcontentlength>1234</D:getcontentlength>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:getcontenttype>text/plain</D:getcontenttype>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:getlastmodified>Sun, 06 Nov 1994 08:49:37 GMT</D:getlastmodified>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:status>HTTP/1.1 200 OK</D:status>"));

    // A file entry has no collection marker; a collection has no content length.
    const char *file_entry = strstr(buf, "<D:href>/dir/f.txt</D:href>");
    TEST_ASSERT_NULL(strstr(file_entry, "<D:collection/>"));

    len = protocore_webdav_ms_end(buf, sizeof(buf), len);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "</D:multistatus>"));

    // An href with XML metacharacters is escaped where it lands in the document.
    len = protocore_webdav_ms_begin(buf, sizeof(buf), 0);
    len = protocore_webdav_ms_entry(buf, sizeof(buf), len, "/a&b<c>", PROTO_FALSE, 1, "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:href>/a&amp;b&lt;c&gt;</D:href>"));
}

// The builder appends whole elements: when one does not fit it leaves the length unchanged, so the
// caller sees no progress and closes the document rather than shipping half a response element.
void test_multistatus_entry_is_atomic(void)
{
    char buf[256];
    size_t len = protocore_webdav_ms_begin(buf, sizeof(buf), 0);
    size_t before = len;
    size_t after = protocore_webdav_ms_entry(buf, sizeof(buf), len, "/dir/some-long-name.txt", PROTO_FALSE, 1234,
                                             "Sun, 06 Nov 1994 08:49:37 GMT", "text/plain");
    TEST_ASSERT_EQUAL_size_t(before, after);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), after);
    TEST_ASSERT_NULL(strstr(buf, "<D:response>")); // nothing partial was left behind

    // The same is true of begin and end against a buffer that cannot hold them.
    char tiny[8];
    tiny[0] = '\0';
    TEST_ASSERT_EQUAL_size_t(0, protocore_webdav_ms_begin(tiny, sizeof(tiny), 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_webdav_ms_end(tiny, sizeof(tiny), 0));
}

// RFC 4918 sec 9.2: PROPPATCH is answered with a Multi-Status naming every property it was asked
// about. This server stores no dead properties, so every one comes back 403 Forbidden, echoed as a
// self-closed element with its namespace prefix intact.
void test_proppatch_multistatus_echoes_the_requested_properties(void)
{
    static const char BODY[] = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                               "<D:propertyupdate xmlns:D=\"DAV:\" xmlns:Z=\"http://ns.example.com/standards/z39.50/\">\n"
                               "  <D:set>\n"
                               "    <D:prop>\n"
                               "      <Z:Authors>\n"
                               "        <Z:Author>Jim Whitehead</Z:Author>\n"
                               "      </Z:Authors>\n"
                               "    </D:prop>\n"
                               "  </D:set>\n"
                               "  <D:remove>\n"
                               "    <D:prop><Z:Copyright-Owner/></D:prop>\n"
                               "  </D:remove>\n"
                               "</D:propertyupdate>";
    char buf[2048];
    size_t n = protocore_webdav_proppatch_ms(buf, sizeof(buf), "/bar.html", BODY, sizeof(BODY) - 1);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:multistatus xmlns:D=\"DAV:\">"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:href>/bar.html</D:href>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<Z:Authors/>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<Z:Copyright-Owner/>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:status>HTTP/1.1 403 Forbidden</D:status>"));

    // The wrappers are not properties and must not be echoed as ones.
    TEST_ASSERT_NULL(strstr(buf, "<D:propertyupdate"));
    TEST_ASSERT_NULL(strstr(buf, "<D:set"));
    TEST_ASSERT_NULL(strstr(buf, "<D:remove"));
    // Nor is the property's value.
    TEST_ASSERT_NULL(strstr(buf, "Jim Whitehead"));
}

// The body is attacker-chosen, so nothing in it may become markup: a property name carrying '<' is
// dropped rather than echoed, and an unterminated tag ends the walk instead of running off the end.
void test_proppatch_does_not_echo_injected_markup(void)
{
    char buf[2048];

    static const char INJECT[] = "<D:prop><evil attr=\"a\"><b\"/></D:prop>";
    size_t n = protocore_webdav_proppatch_ms(buf, sizeof(buf), "/r", INJECT, sizeof(INJECT) - 1);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(buf, "<b\""));

    static const char UNTERMINATED[] = "<D:prop><never-closed";
    n = protocore_webdav_proppatch_ms(buf, sizeof(buf), "/r", UNTERMINATED, sizeof(UNTERMINATED) - 1);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(buf, "never-closed"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:status>HTTP/1.1 403 Forbidden</D:status>"));

    // An empty body still produces a well-formed document with no properties in it.
    n = protocore_webdav_proppatch_ms(buf, sizeof(buf), "/r", "", 0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "</D:multistatus>"));

    // A destination too small for even the fixed markup yields 0 and a valid empty C string.
    char tiny[16];
    TEST_ASSERT_EQUAL_size_t(0, protocore_webdav_proppatch_ms(tiny, sizeof(tiny), "/r", "", 0));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}
