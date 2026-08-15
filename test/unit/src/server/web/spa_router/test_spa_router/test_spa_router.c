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
        TEST_ASSERT_TRUE_MESSAGE(protocore_spa_has_extension(HAS[i]), HAS[i]);
    }
    for (size_t i = 0; i < sizeof(HAS_NOT) / sizeof(HAS_NOT[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(protocore_spa_has_extension(HAS_NOT[i]), HAS_NOT[i]);
    }
    TEST_ASSERT_FALSE(protocore_spa_has_extension(NULL));
}

// --- the routing decision -------------------------------------------------------------------------

// "/" and "" are the app entry point, so they serve the shell rather than looking for a file.
void test_root_serves_the_shell(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route("/", "/api/"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route("", "/api/"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route(NULL, "/api/"));
}

// The API prefix is tested before the extension rule, so an endpoint that happens to end in a dotted
// segment still reaches its handler instead of being looked up on the filesystem.
void test_api_prefix_passes_through(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, protocore_spa_route("/api/state", "/api/"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, protocore_spa_route("/api/", "/api/"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, protocore_spa_route("/api/v1/device.json", "/api/"));
    // A path that only shares a leading substring with the prefix is not under it.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route("/apiary", "/api/"));
    // No prefix configured: nothing passes through.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route("/api/state", NULL));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route("/api/state", ""));
}

// An extensionless path is a client route the browser's own router owns; anything with an extension
// is a real file on disk.
void test_client_routes_get_the_shell_and_assets_get_the_file(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route("/dashboard", "/api/"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route("/devices/42", "/api/"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FILE, protocore_spa_route("/app.js", "/api/"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FILE, protocore_spa_route("/assets/style.css", "/api/"));
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
        TEST_ASSERT_EQUAL_INT_MESSAGE(protocore_spa_route(PATHS[i], "/api/"), protocore_spa_route_ex(PATHS[i], &c),
                                      PATHS[i]);
    }
    // A null context degrades to the no-prefix plain route.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_SHELL, protocore_spa_route_ex("/api/state", NULL));
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
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FALLBACK, protocore_spa_route_ex("/", CASES[i]));
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FALLBACK, protocore_spa_route_ex("/dashboard", CASES[i]));
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_SERVE_FILE, protocore_spa_route_ex("/app.js", CASES[i]));
        TEST_ASSERT_EQUAL_INT(PROTOCORE_SPA_PASSTHROUGH, protocore_spa_route_ex("/api/state", CASES[i]));
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
    protocore_ui_stream_begin(&s, frags, count, ctx);
    while (!protocore_ui_stream_done(&s))
    {
        size_t n = protocore_ui_stream_next(&s, chunk, cap);
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
    protocore_ui_stream_begin(&s, FRAGS, 2, &flag);
    size_t n = protocore_ui_stream_next(&s, buf, 4);
    TEST_ASSERT_EQUAL_UINT(4u, n);
    flag = 1;
    size_t m = protocore_ui_stream_next(&s, buf, 64);
    TEST_ASSERT_EQUAL_UINT(4u, m);
    TEST_ASSERT_EQUAL_MEMORY("BBBB", buf, 4);
    TEST_ASSERT_TRUE(protocore_ui_stream_done(&s));
}

// A fragment set with nothing to emit finishes without ever writing an octet.
void test_empty_and_all_skipped_streams_finish_immediately(void)
{
    protocore_ui_stream s;
    char buf[32];

    protocore_ui_stream_begin(&s, PAGE, 0, NULL);
    TEST_ASSERT_TRUE(protocore_ui_stream_done(&s));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ui_stream_next(&s, buf, sizeof(buf)));

    protocore_ui_stream_begin(&s, NULL, 4, NULL);
    TEST_ASSERT_TRUE(protocore_ui_stream_done(&s));

    static const protocore_ui_fragment NONE[] = {
        {"a", "AAAA", always_false},
        {"b", "BBBB", always_false},
    };
    protocore_ui_stream_begin(&s, NONE, 2, NULL);
    TEST_ASSERT_FALSE(protocore_ui_stream_done(&s));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ui_stream_next(&s, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(protocore_ui_stream_done(&s));
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
    protocore_ui_stream_begin(&s, PAGE, 4, NULL);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ui_stream_next(&s, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ui_stream_next(&s, buf, 0));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ui_stream_next(NULL, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(protocore_ui_stream_done(NULL));
}
