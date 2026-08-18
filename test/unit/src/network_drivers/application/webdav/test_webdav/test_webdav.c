// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t webdav_work[16]; // the borrow an entry takes; Webdav never reads it

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
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-s";
    Webdav.lock_acquire_args.exclusive = PROTO_FALSE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-x";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);

    // Row "Shared Lock": another shared is granted, an exclusive is not.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-s1";
    Webdav.lock_acquire_args.exclusive = PROTO_FALSE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-s2";
    Webdav.lock_acquire_args.exclusive = PROTO_FALSE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-x";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);

    // Row "Exclusive Lock": neither is granted.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-x1";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-s";
    Webdav.lock_acquire_args.exclusive = PROTO_FALSE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok-x2";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
}

// sec 9.10.3: a Depth-infinity lock covers the whole subtree, so it conflicts with a lock anywhere
// under it, while a Depth-0 lock covers only its own resource. sec 5.2 makes the boundary a path
// segment, so a lock on /a must not reach /ab.
void test_lock_scope_follows_depth_and_segment_boundaries(void)
{
    DavLockTable t;

    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/a";
    Webdav.lock_acquire_args.token = "tok-inf";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_TRUE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/a/b";
    Webdav.lock_acquire_args.token = "tok2";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/a/b/c";
    Webdav.lock_acquire_args.token = "tok3";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/ab";
    Webdav.lock_acquire_args.token = "tok4";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/b";
    Webdav.lock_acquire_args.token = "tok5";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);

    // A Depth-0 lock reaches no further than itself.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/a";
    Webdav.lock_acquire_args.token = "tok0";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/a/b";
    Webdav.lock_acquire_args.token = "tok6";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);

    // A new Depth-infinity lock over an existing lock inside its subtree conflicts too.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/a/b";
    Webdav.lock_acquire_args.token = "tokx";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/a";
    Webdav.lock_acquire_args.token = "toky";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_TRUE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);

    // A Depth-infinity lock on the root covers every path below it.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/";
    Webdav.lock_acquire_args.token = "root";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_TRUE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/anything/at/all";
    Webdav.lock_acquire_args.token = "tokz";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
}

// sec 8.3: a collection URL with and without its trailing slash names the same resource, so a lock
// taken on one must be found by a query on the other.
void test_lock_paths_normalize_the_trailing_slash(void)
{
    DavLockTable t;
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/dir/";
    Webdav.lock_acquire_args.token = "tok";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = "/dir";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = "/dir/";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/dir";
    Webdav.lock_acquire_args.token = "tok2";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);

    // The root keeps its single slash rather than normalizing to nothing.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/";
    Webdav.lock_acquire_args.token = "root";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = "/";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
}

// sec 7.1: "a lock-null resource ... a write MUST fail unless the lock token is submitted". A write
// with no token or the wrong token is denied; the matching token lets it through; an unlocked
// resource is always writable.
void test_write_needs_the_covering_lock_token(void)
{
    DavLockTable t;
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/r";
    Webdav.lock_can_write_args.presented_token = NULL;
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok); // nothing locked yet

    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "opaquelocktoken:1";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/r";
    Webdav.lock_can_write_args.presented_token = NULL;
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/r";
    Webdav.lock_can_write_args.presented_token = "opaquelocktoken:2";
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/r";
    Webdav.lock_can_write_args.presented_token = "opaquelocktoken:1";
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/other";
    Webdav.lock_can_write_args.presented_token = NULL;
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);

    // A Depth-infinity lock gates its whole subtree the same way.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/c";
    Webdav.lock_acquire_args.token = "opaquelocktoken:9";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_TRUE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/c/deep/file";
    Webdav.lock_can_write_args.presented_token = NULL;
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/c/deep/file";
    Webdav.lock_can_write_args.presented_token = "opaquelocktoken:9";
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);

    // UNLOCK releases by token and the resource becomes writable again.
    Webdav.lock_release_args.t = &t;
    Webdav.lock_release_args.token = "opaquelocktoken:9";
    Webdav.lock_release(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    Webdav.lock_can_write_args.t = &t;
    Webdav.lock_can_write_args.path = "/c/deep/file";
    Webdav.lock_can_write_args.presented_token = NULL;
    Webdav.lock_can_write(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    Webdav.lock_release_args.t = &t;
    Webdav.lock_release_args.token = "opaquelocktoken:9";
    Webdav.lock_release(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok); // already gone
}

// sec 6.6: "a lock is destroyed ... when its timeout expires", and sec 9.10.2 lets a LOCK refresh
// push the timeout out. A lock with no timeout is never swept.
void test_lock_timeout_and_refresh(void)
{
    DavLockTable t;
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 100;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);

    Webdav.lock_sweep_args.t = &t;
    Webdav.lock_sweep_args.now_s = 99;
    Webdav.lock_sweep(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n); // not yet
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = "/r";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);

    // A refresh moves the expiry, so the second the lock would have died passes harmlessly.
    Webdav.lock_refresh_args.t = &t;
    Webdav.lock_refresh_args.token = "tok";
    Webdav.lock_refresh_args.new_expiry_s = 200;
    Webdav.lock_refresh(webdav_work);
    const DavLock *l = Webdav.ptr;
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQUAL_UINT32(200, l->expiry_s);
    Webdav.lock_sweep_args.t = &t;
    Webdav.lock_sweep_args.now_s = 100;
    Webdav.lock_sweep(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = "/r";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);

    Webdav.lock_sweep_args.t = &t;
    Webdav.lock_sweep_args.now_s = 200;
    Webdav.lock_sweep(webdav_work);
    TEST_ASSERT_EQUAL_size_t(1, Webdav.n); // the expiry second itself
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = "/r";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_refresh_args.t = &t;
    Webdav.lock_refresh_args.token = "tok";
    Webdav.lock_refresh_args.new_expiry_s = 300;
    Webdav.lock_refresh(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr); // no live lock has that token

    // Expiry 0 means no timeout.
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "forever";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
    Webdav.lock_sweep_args.t = &t;
    Webdav.lock_sweep_args.now_s = 0xFFFFFFFFu;
    Webdav.lock_sweep(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = "/r";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
}

// The table is a fixed structural bound: once every slot holds a lock, a further non-conflicting
// LOCK is refused rather than overwriting one.
void test_lock_table_is_bounded(void)
{
    DavLockTable t;
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);
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
        Webdav.lock_acquire_args.t = &t;
        Webdav.lock_acquire_args.path = path;
        Webdav.lock_acquire_args.token = token;
        Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
        Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
        Webdav.lock_acquire_args.expiry_s = 0;
        Webdav.lock_acquire(webdav_work);
        TEST_ASSERT_NOT_NULL(Webdav.ptr);
    }
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/zz";
    Webdav.lock_acquire_args.token = "tzz";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);

    // Freeing one slot makes room again.
    Webdav.lock_release_args.t = &t;
    Webdav.lock_release_args.token = "ta";
    Webdav.lock_release(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/zz";
    Webdav.lock_acquire_args.token = "tzz";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NOT_NULL(Webdav.ptr);
}

// A path or token longer than its fixed field is refused: a silently truncated lock would guard the
// wrong resource, or answer to a token nobody holds.
void test_lock_oversized_path_and_token_are_refused(void)
{
    DavLockTable t;
    Webdav.lock_init_args.t = &t;
    Webdav.lock_init(webdav_work);

    char long_path[PROTOCORE_DAV_LOCK_PATH_MAX + 8];
    memset(long_path, 'p', sizeof(long_path) - 1);
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = long_path;
    Webdav.lock_acquire_args.token = "tok";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);

    char long_token[PROTOCORE_DAV_LOCK_TOKEN_MAX + 8];
    memset(long_token, 't', sizeof(long_token) - 1);
    long_token[sizeof(long_token) - 1] = '\0';
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = long_token;
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);

    Webdav.lock_acquire_args.t = NULL;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = "tok";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = NULL;
    Webdav.lock_acquire_args.token = "tok";
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_acquire_args.t = &t;
    Webdav.lock_acquire_args.path = "/r";
    Webdav.lock_acquire_args.token = NULL;
    Webdav.lock_acquire_args.exclusive = PROTO_TRUE;
    Webdav.lock_acquire_args.depth_infinity = PROTO_FALSE;
    Webdav.lock_acquire_args.expiry_s = 0;
    Webdav.lock_acquire(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_find_args.t = NULL;
    Webdav.lock_find_args.path = "/r";
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_find_args.t = &t;
    Webdav.lock_find_args.path = NULL;
    Webdav.lock_find(webdav_work);
    TEST_ASSERT_NULL(Webdav.ptr);
    Webdav.lock_release_args.t = NULL;
    Webdav.lock_release_args.token = "tok";
    Webdav.lock_release(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.lock_release_args.t = &t;
    Webdav.lock_release_args.token = NULL;
    Webdav.lock_release(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.lock_sweep_args.t = NULL;
    Webdav.lock_sweep_args.now_s = 1;
    Webdav.lock_sweep(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
}

// RFC 4918 sec 10.4.2: State-token = Coded-URL, and a Coded-URL is "<" Simple-ref ">" inside a
// condition list. The examples of sec 10.4.6 and 10.4.7 are taken verbatim:
//   (<urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2> ["I am an ETag"])
//   (Not <urn:uuid:181d4fae-...> <urn:uuid:58f202ac-...>)
// and the tagged form of sec 10.4.7's Tagged-list, whose Resource-Tag precedes the first list.
void test_if_header_state_token(void)
{
    char out[64];

    Webdav.if_token_args.if_header = "(<urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2> [\"I am an ETag\"])";
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2", out);

    // "Not" prefixes the condition; the first Coded-URL is still the token the list names.
    Webdav.if_token_args.if_header = "(Not <urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2>)";
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("urn:uuid:181d4fae-7d8c-11d0-a765-00a0c91e6bf2", out);

    // Tagged-list: the Resource-Tag before the '(' is not the state token.
    Webdav.if_token_args.if_header = "</resource1> (<opaquelocktoken:abc-pc>)";
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("opaquelocktoken:abc-pc", out);

    // An entity-tag-only condition carries no state token.
    Webdav.if_token_args.if_header = "([\"I am another ETag\"])";
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    // No condition list at all.
    Webdav.if_token_args.if_header = "<opaquelocktoken:abc>";
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    // Unterminated Coded-URL.
    Webdav.if_token_args.if_header = "(<opaquelocktoken:abc";
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);

    // A token that does not fit is refused, not truncated.
    char tiny[8];
    Webdav.if_token_args.if_header = "(<opaquelocktoken:abc>)";
    Webdav.if_token_args.out = tiny;
    Webdav.if_token_args.cap = sizeof(tiny);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);

    Webdav.if_token_args.if_header = NULL;
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.if_token_args.if_header = "(<a>)";
    Webdav.if_token_args.out = NULL;
    Webdav.if_token_args.cap = sizeof(out);
    Webdav.if_token(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.if_token_args.if_header = "(<a>)";
    Webdav.if_token_args.out = out;
    Webdav.if_token_args.cap = 0;
    Webdav.if_token(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
}

// RFC 4918 sec 10.2: Depth = "Depth" ":" ("0" | "1" | "infinity"). Anything else is not a Depth
// value, so the caller's per-method default stands (sec 10.2 leaves the absent case to the method).
void test_depth_header(void)
{
    Webdav.depth_args.depth_hdr = "0";
    Webdav.depth_args.dflt = 1;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(0, Webdav.i32);
    Webdav.depth_args.depth_hdr = "1";
    Webdav.depth_args.dflt = 0;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(1, Webdav.i32);
    Webdav.depth_args.depth_hdr = "infinity";
    Webdav.depth_args.dflt = 0;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DAV_DEPTH_INFINITY, Webdav.i32);
    TEST_ASSERT_EQUAL_INT(0x7fffffff, PROTOCORE_DAV_DEPTH_INFINITY);

    Webdav.depth_args.depth_hdr = NULL;
    Webdav.depth_args.dflt = 7;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(7, Webdav.i32);
    Webdav.depth_args.depth_hdr = "";
    Webdav.depth_args.dflt = 7;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(7, Webdav.i32);
    Webdav.depth_args.depth_hdr = "2";
    Webdav.depth_args.dflt = 7;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(7, Webdav.i32);
    Webdav.depth_args.depth_hdr = "Infinity";
    Webdav.depth_args.dflt = 7;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(7, Webdav.i32); // the ABNF token is lowercase
    Webdav.depth_args.depth_hdr = "0 ";
    Webdav.depth_args.dflt = 7;
    Webdav.depth(webdav_work);
    TEST_ASSERT_EQUAL_INT(7, Webdav.i32);
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
        {"OPTIONS", DAV_M_OPTIONS},
        {"GET", DAV_M_GET},
        {"HEAD", DAV_M_HEAD},
        {"PUT", DAV_M_PUT},
        {"DELETE", DAV_M_DELETE},
        {"PROPFIND", DAV_M_PROPFIND},
        {"PROPPATCH", DAV_M_PROPPATCH},
        {"MKCOL", DAV_M_MKCOL},
        {"COPY", DAV_M_COPY},
        {"MOVE", DAV_M_MOVE},
        {"LOCK", DAV_M_LOCK},
        {"UNLOCK", DAV_M_UNLOCK},
        {"POST", DAV_M_UNSUPPORTED},
        {"PATCH", DAV_M_UNSUPPORTED},
        {"", DAV_M_UNSUPPORTED},
        // RFC 9110 sec 9.1 makes the method token case-sensitive, so a lowercase spelling is a
        // different token and not one of these.
        {"propfind", DAV_M_UNSUPPORTED},
        {"Get", DAV_M_UNSUPPORTED},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        Webdav.method_args.m = CASES[i].name;
        Webdav.method(webdav_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CASES[i].want, Webdav.value, CASES[i].name);
    }
    Webdav.method_args.m = NULL;
    Webdav.method(webdav_work);
    TEST_ASSERT_EQUAL_INT(DAV_M_UNSUPPORTED, Webdav.value);
}

// XML 1.0 sec 4.6 defines exactly five predefined entities: amp, lt, gt, apos, quot. A property
// value or an href carrying any of those raw characters would close the element it sits in, so the
// escaper replaces them and leaves everything else alone.
void test_xml_escape(void)
{
    char out[128];

    Webdav.xml_escape_args.dst = out;
    Webdav.xml_escape_args.cap = sizeof(out);
    Webdav.xml_escape_args.src = "&<>\"'";
    Webdav.xml_escape(webdav_work);
    TEST_ASSERT_EQUAL_size_t(strlen("&amp;&lt;&gt;&quot;&apos;"), Webdav.n);
    TEST_ASSERT_EQUAL_STRING("&amp;&lt;&gt;&quot;&apos;", out);

    Webdav.xml_escape_args.dst = out;
    Webdav.xml_escape_args.cap = sizeof(out);
    Webdav.xml_escape_args.src = "/a b/c.txt";
    Webdav.xml_escape(webdav_work);
    TEST_ASSERT_EQUAL_size_t(strlen("/a b/c.txt"), Webdav.n);
    TEST_ASSERT_EQUAL_STRING("/a b/c.txt", out);

    Webdav.xml_escape_args.dst = out;
    Webdav.xml_escape_args.cap = sizeof(out);
    Webdav.xml_escape_args.src = "";
    Webdav.xml_escape(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
    TEST_ASSERT_EQUAL_STRING("", out);

    // A closing tag smuggled into an href cannot survive the escape.
    Webdav.xml_escape_args.dst = out;
    Webdav.xml_escape_args.cap = sizeof(out);
    Webdav.xml_escape_args.src = "</D:href><D:evil/>";
    Webdav.xml_escape(webdav_work);
    TEST_ASSERT_NULL(strstr(out, "<"));
    TEST_ASSERT_NULL(strstr(out, ">"));

    // The result is always NUL-terminated inside the caller's buffer, whatever gets clipped.
    char small[8];
    memset(small, 0x7F, sizeof(small));
    Webdav.xml_escape_args.dst = small;
    Webdav.xml_escape_args.cap = sizeof(small);
    Webdav.xml_escape_args.src = "&&&&&&&&";
    Webdav.xml_escape(webdav_work);
    size_t n = Webdav.n;
    TEST_ASSERT_TRUE(n < sizeof(small));
    TEST_ASSERT_EQUAL_size_t(strlen(small), n);
    Webdav.xml_escape_args.dst = small;
    Webdav.xml_escape_args.cap = 0;
    Webdav.xml_escape_args.src = "x";
    Webdav.xml_escape(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
}

// RFC 4918 sec 10.3: "The Destination request header specifies the URI that identifies a
// destination resource", and sec 8.3 allows either an absolute URI or an absolute path. RFC 3986
// sec 2.1 makes %HH an octet, so the path is percent-decoded before it names a file.
void test_destination_header_path(void)
{
    char out[128];

    Webdav.dest_path_args.destination = "http://host/p/q";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("/p/q", out);
    Webdav.dest_path_args.destination = "https://host:8080/p/q";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("/p/q", out);
    Webdav.dest_path_args.destination = "/p/q";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("/p/q", out);

    // RFC 3986 sec 2.1: %20 is a space, and the hex digits are case-insensitive.
    Webdav.dest_path_args.destination = "/a%20b/c%2Fd";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("/a b/c/d", out);
    Webdav.dest_path_args.destination = "/a%2fb";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("/a/b", out);

    // A malformed escape is not a path.
    Webdav.dest_path_args.destination = "/a%zzb";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.dest_path_args.destination = "/a%2";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.dest_path_args.destination = "/a%";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    // An authority with no path names no resource, and a relative reference is not accepted.
    Webdav.dest_path_args.destination = "http://host";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.dest_path_args.destination = "p/q";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);

    // Overflow is a refusal, not a truncated path.
    char tiny[4];
    Webdav.dest_path_args.destination = "/abcdefg";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = 4;
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.dest_path_args.destination = "/ab";
    Webdav.dest_path_args.out = tiny;
    Webdav.dest_path_args.cap = sizeof(tiny);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_TRUE(Webdav.ok);
    TEST_ASSERT_EQUAL_STRING("/ab", tiny);

    Webdav.dest_path_args.destination = NULL;
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.dest_path_args.destination = "/p";
    Webdav.dest_path_args.out = NULL;
    Webdav.dest_path_args.cap = sizeof(out);
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
    Webdav.dest_path_args.destination = "/p";
    Webdav.dest_path_args.out = out;
    Webdav.dest_path_args.cap = 0;
    Webdav.dest_path(webdav_work);
    TEST_ASSERT_FALSE(Webdav.ok);
}

// RFC 4918 sec 14.16: a Multi-Status document is a DAV: multistatus element containing one response
// per resource, and sec 14.24 puts href and propstat inside each. sec 14.9 marks a collection with
// an empty collection element inside resourcetype.
void test_multistatus_document_shape(void)
{
    char buf[2048];
    Webdav.ms_begin_args.buf = buf;
    Webdav.ms_begin_args.cap = sizeof(buf);
    Webdav.ms_begin_args.len = 0;
    Webdav.ms_begin(webdav_work);
    size_t len = Webdav.n;
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:multistatus xmlns:D=\"DAV:\">"));

    Webdav.ms_entry_args.buf = buf;
    Webdav.ms_entry_args.cap = sizeof(buf);
    Webdav.ms_entry_args.len = len;
    Webdav.ms_entry_args.href = "/dir/";
    Webdav.ms_entry_args.is_collection = PROTO_TRUE;
    Webdav.ms_entry_args.size = 0;
    Webdav.ms_entry_args.rfc1123_mtime = "";
    Webdav.ms_entry_args.content_type = "";
    Webdav.ms_entry(webdav_work);
    len = Webdav.n;
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:href>/dir/</D:href>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:collection/>"));

    Webdav.ms_entry_args.buf = buf;
    Webdav.ms_entry_args.cap = sizeof(buf);
    Webdav.ms_entry_args.len = len;
    Webdav.ms_entry_args.href = "/dir/f.txt";
    Webdav.ms_entry_args.is_collection = PROTO_FALSE;
    Webdav.ms_entry_args.size = 1234;
    Webdav.ms_entry_args.rfc1123_mtime = "Sun, 06 Nov 1994 08:49:37 GMT";
    Webdav.ms_entry_args.content_type = "text/plain";
    Webdav.ms_entry(webdav_work);
    len = Webdav.n;
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:href>/dir/f.txt</D:href>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:getcontentlength>1234</D:getcontentlength>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:getcontenttype>text/plain</D:getcontenttype>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:getlastmodified>Sun, 06 Nov 1994 08:49:37 GMT</D:getlastmodified>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:status>HTTP/1.1 200 OK</D:status>"));

    // A file entry has no collection marker; a collection has no content length.
    const char *file_entry = strstr(buf, "<D:href>/dir/f.txt</D:href>");
    TEST_ASSERT_NULL(strstr(file_entry, "<D:collection/>"));

    Webdav.ms_end_args.buf = buf;
    Webdav.ms_end_args.cap = sizeof(buf);
    Webdav.ms_end_args.len = len;
    Webdav.ms_end(webdav_work);
    len = Webdav.n;
    TEST_ASSERT_EQUAL_size_t(strlen(buf), len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "</D:multistatus>"));

    // An href with XML metacharacters is escaped where it lands in the document.
    Webdav.ms_begin_args.buf = buf;
    Webdav.ms_begin_args.cap = sizeof(buf);
    Webdav.ms_begin_args.len = 0;
    Webdav.ms_begin(webdav_work);
    len = Webdav.n;
    Webdav.ms_entry_args.buf = buf;
    Webdav.ms_entry_args.cap = sizeof(buf);
    Webdav.ms_entry_args.len = len;
    Webdav.ms_entry_args.href = "/a&b<c>";
    Webdav.ms_entry_args.is_collection = PROTO_FALSE;
    Webdav.ms_entry_args.size = 1;
    Webdav.ms_entry_args.rfc1123_mtime = "";
    Webdav.ms_entry_args.content_type = "";
    Webdav.ms_entry(webdav_work);
    len = Webdav.n;
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:href>/a&amp;b&lt;c&gt;</D:href>"));
}

// The builder appends whole elements: when one does not fit it leaves the length unchanged, so the
// caller sees no progress and closes the document rather than shipping half a response element.
void test_multistatus_entry_is_atomic(void)
{
    char buf[256];
    Webdav.ms_begin_args.buf = buf;
    Webdav.ms_begin_args.cap = sizeof(buf);
    Webdav.ms_begin_args.len = 0;
    Webdav.ms_begin(webdav_work);
    size_t len = Webdav.n;
    size_t before = len;
    Webdav.ms_entry_args.buf = buf;
    Webdav.ms_entry_args.cap = sizeof(buf);
    Webdav.ms_entry_args.len = len;
    Webdav.ms_entry_args.href = "/dir/some-long-name.txt";
    Webdav.ms_entry_args.is_collection = PROTO_FALSE;
    Webdav.ms_entry_args.size = 1234;
    Webdav.ms_entry_args.rfc1123_mtime = "Sun, 06 Nov 1994 08:49:37 GMT";
    Webdav.ms_entry_args.content_type = "text/plain";
    Webdav.ms_entry(webdav_work);
    size_t after = Webdav.n;
    TEST_ASSERT_EQUAL_size_t(before, after);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), after);
    TEST_ASSERT_NULL(strstr(buf, "<D:response>")); // nothing partial was left behind

    // The same is true of begin and end against a buffer that cannot hold them.
    char tiny[8];
    tiny[0] = '\0';
    Webdav.ms_begin_args.buf = tiny;
    Webdav.ms_begin_args.cap = sizeof(tiny);
    Webdav.ms_begin_args.len = 0;
    Webdav.ms_begin(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
    Webdav.ms_end_args.buf = tiny;
    Webdav.ms_end_args.cap = sizeof(tiny);
    Webdav.ms_end_args.len = 0;
    Webdav.ms_end(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
}

// RFC 4918 sec 9.2: PROPPATCH is answered with a Multi-Status naming every property it was asked
// about. This server stores no dead properties, so every one comes back 403 Forbidden, echoed as a
// self-closed element with its namespace prefix intact.
void test_proppatch_multistatus_echoes_the_requested_properties(void)
{
    static const char BODY[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
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
    Webdav.proppatch_ms_args.buf = buf;
    Webdav.proppatch_ms_args.cap = sizeof(buf);
    Webdav.proppatch_ms_args.href = "/bar.html";
    Webdav.proppatch_ms_args.body = BODY;
    Webdav.proppatch_ms_args.body_len = sizeof(BODY) - 1;
    Webdav.proppatch_ms(webdav_work);
    size_t n = Webdav.n;
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
    Webdav.proppatch_ms_args.buf = buf;
    Webdav.proppatch_ms_args.cap = sizeof(buf);
    Webdav.proppatch_ms_args.href = "/r";
    Webdav.proppatch_ms_args.body = INJECT;
    Webdav.proppatch_ms_args.body_len = sizeof(INJECT) - 1;
    Webdav.proppatch_ms(webdav_work);
    size_t n = Webdav.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(buf, "<b\""));

    static const char UNTERMINATED[] = "<D:prop><never-closed";
    Webdav.proppatch_ms_args.buf = buf;
    Webdav.proppatch_ms_args.cap = sizeof(buf);
    Webdav.proppatch_ms_args.href = "/r";
    Webdav.proppatch_ms_args.body = UNTERMINATED;
    Webdav.proppatch_ms_args.body_len = sizeof(UNTERMINATED) - 1;
    Webdav.proppatch_ms(webdav_work);
    n = Webdav.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(buf, "never-closed"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<D:status>HTTP/1.1 403 Forbidden</D:status>"));

    // An empty body still produces a well-formed document with no properties in it.
    Webdav.proppatch_ms_args.buf = buf;
    Webdav.proppatch_ms_args.cap = sizeof(buf);
    Webdav.proppatch_ms_args.href = "/r";
    Webdav.proppatch_ms_args.body = "";
    Webdav.proppatch_ms_args.body_len = 0;
    Webdav.proppatch_ms(webdav_work);
    n = Webdav.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "</D:multistatus>"));

    // A destination too small for even the fixed markup yields 0 and a valid empty C string.
    char tiny[16];
    Webdav.proppatch_ms_args.buf = tiny;
    Webdav.proppatch_ms_args.cap = sizeof(tiny);
    Webdav.proppatch_ms_args.href = "/r";
    Webdav.proppatch_ms_args.body = "";
    Webdav.proppatch_ms_args.body_len = 0;
    Webdav.proppatch_ms(webdav_work);
    TEST_ASSERT_EQUAL_size_t(0, Webdav.n);
    TEST_ASSERT_EQUAL_STRING("", tiny);
}
