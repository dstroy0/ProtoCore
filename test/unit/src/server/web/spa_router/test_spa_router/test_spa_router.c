// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for single-page-app routing and conditional UI streaming (server/web/spa_router/spa_router.h).
//
// No standard governs "serve the shell or serve the file", so apart from RFC 3986 sec 3.3 - which
// defines a path as segments separated by "/", making the LAST segment the one that can carry a
// filename - these expectations are PROPERTIES, and the header comment says so rather than inventing
// a citation. The load-bearing one is test_stream_output_is_chunk_size_independent: the streamer
// resumes mid-fragment, so the same fragment set must produce byte-identical output whether it is
// drained one octet at a time or in one buffer larger than the whole page. A resume that loses or
// repeats an octet is invisible at any single buffer size and corrupts the page at every other.

#include "server/web/spa_router/spa_router.h"
#include <string.h>

#include <unity.h>

static uint8_t spa_router_work[16]; // the borrow an entry takes; SpaRouter never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// --- extension detection (RFC 3986 sec 3.3: path = *( "/" segment )) -----------------------------

// The dot must be in the last segment, after its first octet, with at least one octet following it.
void test_extension_lives_in_the_last_segment(void)
{
    static const char *const HAS[] = {
        "/app.js", "/assets/style.css", "/dir/file.min.js", "index.html", "/a/b/c.png",
    };
    static const char *const HAS_NOT[] = {
        "/devices/42",  // no dot at all
        "/",            //
        "",             //
        "/v1.2/status", // the dot is in an earlier segment
        "/.hidden",     // the dot opens the segment
        "/dir/.env",    //
        "/trailing.",   // nothing follows the dot
        "/a.b/c",       //
    };
    for (size_t i = 0; i < sizeof(HAS) / sizeof(HAS[0]); i++)
    {
        SpaRouterV.has_extension_args.path = HAS[i];
        SpaRouter.has_extension(spa_router_work);
        TEST_ASSERT_TRUE_MESSAGE(SpaRouterV.ok, HAS[i]);
    }
    for (size_t i = 0; i < sizeof(HAS_NOT) / sizeof(HAS_NOT[0]); i++)
    {
        SpaRouterV.has_extension_args.path = HAS_NOT[i];
        SpaRouter.has_extension(spa_router_work);
        TEST_ASSERT_FALSE_MESSAGE(SpaRouterV.ok, HAS_NOT[i]);
    }
    SpaRouterV.has_extension_args.path = NULL;
    SpaRouter.has_extension(spa_router_work);
    TEST_ASSERT_FALSE(SpaRouterV.ok);
}

// --- the routing decision -------------------------------------------------------------------------

// "/" and "" are the app entry point, so they serve the shell rather than looking for a file.
void test_root_serves_the_shell(void)
{
    SpaRouterV.route_args.path = "/";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
    SpaRouterV.route_args.path = "";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
    SpaRouterV.route_args.path = NULL;
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
}

// The API prefix is tested before the extension rule, so an endpoint that happens to end in a dotted
// segment still reaches its handler instead of being looked up on the filesystem.
void test_api_prefix_passes_through(void)
{
    SpaRouterV.route_args.path = "/api/state";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, SpaRouterV.action);
    SpaRouterV.route_args.path = "/api/";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, SpaRouterV.action);
    SpaRouterV.route_args.path = "/api/v1/device.json";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, SpaRouterV.action);
    // A path that only shares a leading substring with the prefix is not under it.
    SpaRouterV.route_args.path = "/apiary";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
    // No prefix configured: nothing passes through.
    SpaRouterV.route_args.path = "/api/state";
    SpaRouterV.route_args.api_prefix = NULL;
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
    SpaRouterV.route_args.path = "/api/state";
    SpaRouterV.route_args.api_prefix = "";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
}

// An extensionless path is a client route the browser's own router owns; anything with an extension
// is a real file on disk.
void test_client_routes_get_the_shell_and_assets_get_the_file(void)
{
    SpaRouterV.route_args.path = "/dashboard";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
    SpaRouterV.route_args.path = "/devices/42";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
    SpaRouterV.route_args.path = "/app.js";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FILE, SpaRouterV.action);
    SpaRouterV.route_args.path = "/assets/style.css";
    SpaRouterV.route_args.api_prefix = "/api/";
    SpaRouter.route(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FILE, SpaRouterV.action);
}

// --- the fallback HMI -----------------------------------------------------------------------------

static protocore_spa_ctx healthy(void)
{
    protocore_spa_ctx c;
    c.api_prefix = "/api/";
    c.shell_available = PROTO_TRUE;
    c.client_scripting = PROTO_TRUE;
    c.degraded = PROTO_FALSE;
    return c;
}

// With everything healthy the extended router makes exactly the same decisions as the plain one.
void test_route_ex_matches_plain_route_when_healthy(void)
{
    protocore_spa_ctx c = healthy();
    static const char *const PATHS[] = {"/", "/dashboard", "/app.js", "/api/state", "/devices/42"};
    for (size_t i = 0; i < sizeof(PATHS) / sizeof(PATHS[0]); i++)
    {
        // The plain decision is captured before the extended one runs: both report through
        // SpaRouter.action, so comparing them in one expression would compare the second with itself.
        SpaRouterV.route_args.path = PATHS[i];
        SpaRouterV.route_args.api_prefix = "/api/";
        SpaRouter.route(spa_router_work);
        const protocore_spa_action plain = SpaRouterV.action;
        SpaRouterV.route_ex_args.path = PATHS[i];
        SpaRouterV.route_ex_args.ctx = &c;
        SpaRouter.route_ex(spa_router_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(plain, SpaRouterV.action, PATHS[i]);
    }
    // A null context degrades to the no-prefix plain route.
    SpaRouterV.route_ex_args.path = "/api/state";
    SpaRouterV.route_ex_args.ctx = NULL;
    SpaRouter.route_ex(spa_router_work);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, SpaRouterV.action);
}

// Each of the three conditions on its own turns a shell decision into the no-JS control page, and
// none of them touches the asset or API decisions.
void test_only_the_shell_decision_degrades(void)
{
    protocore_spa_ctx no_shell = healthy();
    no_shell.shell_available = PROTO_FALSE;
    protocore_spa_ctx no_js = healthy();
    no_js.client_scripting = PROTO_FALSE;
    protocore_spa_ctx degraded = healthy();
    degraded.degraded = PROTO_TRUE;

    const protocore_spa_ctx *CASES[3] = {&no_shell, &no_js, &degraded};
    for (int i = 0; i < 3; i++)
    {
        SpaRouterV.route_ex_args.path = "/";
        SpaRouterV.route_ex_args.ctx = CASES[i];
        SpaRouter.route_ex(spa_router_work);
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FALLBACK, SpaRouterV.action);
        SpaRouterV.route_ex_args.path = "/dashboard";
        SpaRouterV.route_ex_args.ctx = CASES[i];
        SpaRouter.route_ex(spa_router_work);
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FALLBACK, SpaRouterV.action);
        SpaRouterV.route_ex_args.path = "/app.js";
        SpaRouterV.route_ex_args.ctx = CASES[i];
        SpaRouter.route_ex(spa_router_work);
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FILE, SpaRouterV.action);
        SpaRouterV.route_ex_args.path = "/api/state";
        SpaRouterV.route_ex_args.ctx = CASES[i];
        SpaRouter.route_ex(spa_router_work);
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, SpaRouterV.action);
    }
}

// --- conditional UI streaming ---------------------------------------------------------------------

static proto_bool always_false(void *ctx)
{
    (void)ctx;
    return PROTO_FALSE;
}

static proto_bool flag_is_set(void *ctx)
{
    return *(const int *)ctx != 0;
}

static const protocore_ui_fragment PAGE[] = {
    {"head", "<html><body>", NULL},
    {"alarm", "<div id=alarm>TRIP</div>", always_false},
    {"status", "<div id=status>RUN</div>", NULL},
    {"tail", "</body></html>", NULL},
};

// Drain the whole stream through a @p cap-sized buffer into @p out; return the total length.
static size_t drain(const protocore_ui_fragment *frags, size_t count, void *ctx, size_t cap, char *out, size_t out_cap)
{
    protocore_ui_stream s;
    char chunk[64];
    size_t total = 0;
    SpaRouterV.ui_stream_begin_args.s = &s;
    SpaRouterV.ui_stream_begin_args.frags = frags;
    SpaRouterV.ui_stream_begin_args.count = count;
    SpaRouterV.ui_stream_begin_args.ctx = ctx;
    SpaRouter.ui_stream_begin(spa_router_work);
    for (;;)
    {
        // The done test is read into a local rather than sitting in the loop condition: the
        // condition is re-evaluated every iteration, and the namespace reports through one member.
        SpaRouterV.ui_stream_done_args.s = &s;
        SpaRouter.ui_stream_done(spa_router_work);
        const proto_bool done = SpaRouterV.ok;
        if (done)
        {
            break;
        }
        SpaRouterV.ui_stream_next_args.s = &s;
        SpaRouterV.ui_stream_next_args.out = chunk;
        SpaRouterV.ui_stream_next_args.cap = cap;
        SpaRouter.ui_stream_next(spa_router_work);
        size_t n = SpaRouterV.n;
        if (n == 0)
        {
            break;
        }
        TEST_ASSERT_TRUE(total + n < out_cap);
        memcpy(out + total, chunk, n);
        total += n;
    }
    out[total] = '\0';
    return total;
}

// The stream resumes mid-fragment, so the concatenation is the same for every buffer size from one
// octet up to more than the whole page.
void test_stream_output_is_chunk_size_independent(void)
{
    char whole[512];
    size_t n = drain(PAGE, 4, NULL, 64, whole, sizeof(whole));
    TEST_ASSERT_EQUAL_STRING("<html><body><div id=status>RUN</div></body></html>", whole);
    TEST_ASSERT_EQUAL_UINT(strlen("<html><body><div id=status>RUN</div></body></html>"), n);

    for (size_t cap = 1; cap <= 64; cap++)
    {
        char got[512];
        size_t m = drain(PAGE, 4, NULL, cap, got, sizeof(got));
        TEST_ASSERT_EQUAL_UINT(n, m);
        TEST_ASSERT_EQUAL_STRING(whole, got);
    }
}

// A predicate runs when the stream reaches its fragment, not once at begin(), so state that changes
// mid-render is reflected by the fragments after the change.
void test_predicates_run_as_the_stream_reaches_them(void)
{
    static int flag;
    static const protocore_ui_fragment FRAGS[] = {
        {"a", "AAAA", NULL},
        {"b", "BBBB", flag_is_set},
    };

    flag = 0;
    char off[64];
    TEST_ASSERT_EQUAL_UINT(4u, drain(FRAGS, 2, &flag, 64, off, sizeof(off)));
    TEST_ASSERT_EQUAL_STRING("AAAA", off);

    // Emit the first fragment, flip the flag, then continue: the second fragment is now included.
    protocore_ui_stream s;
    char buf[64];
    flag = 0;
    SpaRouterV.ui_stream_begin_args.s = &s;
    SpaRouterV.ui_stream_begin_args.frags = FRAGS;
    SpaRouterV.ui_stream_begin_args.count = 2;
    SpaRouterV.ui_stream_begin_args.ctx = &flag;
    SpaRouter.ui_stream_begin(spa_router_work);
    SpaRouterV.ui_stream_next_args.s = &s;
    SpaRouterV.ui_stream_next_args.out = buf;
    SpaRouterV.ui_stream_next_args.cap = 4;
    SpaRouter.ui_stream_next(spa_router_work);
    size_t n = SpaRouterV.n;
    TEST_ASSERT_EQUAL_UINT(4u, n);
    flag = 1;
    SpaRouterV.ui_stream_next_args.s = &s;
    SpaRouterV.ui_stream_next_args.out = buf;
    SpaRouterV.ui_stream_next_args.cap = 64;
    SpaRouter.ui_stream_next(spa_router_work);
    size_t m = SpaRouterV.n;
    TEST_ASSERT_EQUAL_UINT(4u, m);
    TEST_ASSERT_EQUAL_MEMORY("BBBB", buf, 4);
    SpaRouterV.ui_stream_done_args.s = &s;
    SpaRouter.ui_stream_done(spa_router_work);
    TEST_ASSERT_TRUE(SpaRouterV.ok);
}

// A fragment set with nothing to emit finishes without ever writing an octet.
void test_empty_and_all_skipped_streams_finish_immediately(void)
{
    protocore_ui_stream s;
    char buf[32];

    SpaRouterV.ui_stream_begin_args.s = &s;
    SpaRouterV.ui_stream_begin_args.frags = PAGE;
    SpaRouterV.ui_stream_begin_args.count = 0;
    SpaRouterV.ui_stream_begin_args.ctx = NULL;
    SpaRouter.ui_stream_begin(spa_router_work);
    SpaRouterV.ui_stream_done_args.s = &s;
    SpaRouter.ui_stream_done(spa_router_work);
    TEST_ASSERT_TRUE(SpaRouterV.ok);
    SpaRouterV.ui_stream_next_args.s = &s;
    SpaRouterV.ui_stream_next_args.out = buf;
    SpaRouterV.ui_stream_next_args.cap = sizeof(buf);
    SpaRouter.ui_stream_next(spa_router_work);
    TEST_ASSERT_EQUAL_UINT(0u, SpaRouterV.n);

    SpaRouterV.ui_stream_begin_args.s = &s;
    SpaRouterV.ui_stream_begin_args.frags = NULL;
    SpaRouterV.ui_stream_begin_args.count = 4;
    SpaRouterV.ui_stream_begin_args.ctx = NULL;
    SpaRouter.ui_stream_begin(spa_router_work);
    SpaRouterV.ui_stream_done_args.s = &s;
    SpaRouter.ui_stream_done(spa_router_work);
    TEST_ASSERT_TRUE(SpaRouterV.ok);

    static const protocore_ui_fragment NONE[] = {
        {"a", "AAAA", always_false},
        {"b", "BBBB", always_false},
    };
    SpaRouterV.ui_stream_begin_args.s = &s;
    SpaRouterV.ui_stream_begin_args.frags = NONE;
    SpaRouterV.ui_stream_begin_args.count = 2;
    SpaRouterV.ui_stream_begin_args.ctx = NULL;
    SpaRouter.ui_stream_begin(spa_router_work);
    SpaRouterV.ui_stream_done_args.s = &s;
    SpaRouter.ui_stream_done(spa_router_work);
    TEST_ASSERT_FALSE(SpaRouterV.ok);
    SpaRouterV.ui_stream_next_args.s = &s;
    SpaRouterV.ui_stream_next_args.out = buf;
    SpaRouterV.ui_stream_next_args.cap = sizeof(buf);
    SpaRouter.ui_stream_next(spa_router_work);
    TEST_ASSERT_EQUAL_UINT(0u, SpaRouterV.n);
    SpaRouterV.ui_stream_done_args.s = &s;
    SpaRouter.ui_stream_done(spa_router_work);
    TEST_ASSERT_TRUE(SpaRouterV.ok);
}

// A fragment whose html pointer is null is skipped like a false predicate rather than dereferenced.
void test_null_fragment_body_is_skipped(void)
{
    static const protocore_ui_fragment FRAGS[] = {
        {"a", NULL, NULL},
        {"b", "BBBB", NULL},
    };
    char got[32];
    TEST_ASSERT_EQUAL_UINT(4u, drain(FRAGS, 2, NULL, 32, got, sizeof(got)));
    TEST_ASSERT_EQUAL_STRING("BBBB", got);
}

// A null destination or a zero capacity writes nothing and reports nothing written.
void test_stream_refuses_bad_arguments(void)
{
    protocore_ui_stream s;
    char buf[8];
    buf[0] = 'x';
    SpaRouterV.ui_stream_begin_args.s = &s;
    SpaRouterV.ui_stream_begin_args.frags = PAGE;
    SpaRouterV.ui_stream_begin_args.count = 4;
    SpaRouterV.ui_stream_begin_args.ctx = NULL;
    SpaRouter.ui_stream_begin(spa_router_work);
    SpaRouterV.ui_stream_next_args.s = &s;
    SpaRouterV.ui_stream_next_args.out = NULL;
    SpaRouterV.ui_stream_next_args.cap = sizeof(buf);
    SpaRouter.ui_stream_next(spa_router_work);
    TEST_ASSERT_EQUAL_UINT(0u, SpaRouterV.n);
    SpaRouterV.ui_stream_next_args.s = &s;
    SpaRouterV.ui_stream_next_args.out = buf;
    SpaRouterV.ui_stream_next_args.cap = 0;
    SpaRouter.ui_stream_next(spa_router_work);
    TEST_ASSERT_EQUAL_UINT(0u, SpaRouterV.n);
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
    SpaRouterV.ui_stream_next_args.s = NULL;
    SpaRouterV.ui_stream_next_args.out = buf;
    SpaRouterV.ui_stream_next_args.cap = sizeof(buf);
    SpaRouter.ui_stream_next(spa_router_work);
    TEST_ASSERT_EQUAL_UINT(0u, SpaRouterV.n);
    SpaRouterV.ui_stream_done_args.s = NULL;
    SpaRouter.ui_stream_done(spa_router_work);
    TEST_ASSERT_TRUE(SpaRouterV.ok);
}
