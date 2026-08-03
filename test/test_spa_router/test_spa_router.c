// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/spa_router: the single-page-app routing decision.

#include "services/web/spa_router/spa_router.h"
#include <string>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_has_extension(void)
{
    TEST_ASSERT_TRUE(pc_spa_has_extension("/app.js"));
    TEST_ASSERT_TRUE(pc_spa_has_extension("/assets/style.css"));
    TEST_ASSERT_TRUE(pc_spa_has_extension("/x/y.min.js"));
    TEST_ASSERT_FALSE(pc_spa_has_extension("/dashboard"));
    TEST_ASSERT_FALSE(pc_spa_has_extension("/devices/42"));
    TEST_ASSERT_FALSE(pc_spa_has_extension("/")); // no segment
    // A dotfile directory in the path but an extensionless final segment is still a route.
    TEST_ASSERT_FALSE(pc_spa_has_extension("/a.b/c"));
    // Trailing dot is not an extension.
    TEST_ASSERT_FALSE(pc_spa_has_extension("/weird."));
    // Null path: bail out before touching it.
    TEST_ASSERT_FALSE(pc_spa_has_extension(NULL));
    // No '/' at all: the whole path is the segment (ternary's non-slash branch).
    TEST_ASSERT_TRUE(pc_spa_has_extension("file.txt"));
    // Segment starts with the dot (dotfile): the dot is the segment, not an extension.
    TEST_ASSERT_FALSE(pc_spa_has_extension("/.hidden"));
}

void test_route(void)
{
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route("/", "/api/"));
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route("", "/api/"));
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route("/dashboard", "/api/"));
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route("/devices/42", "/api/"));
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FILE, pc_spa_route("/app.js", "/api/"));
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FILE, pc_spa_route("/assets/logo.svg", "/api/"));
    TEST_ASSERT_EQUAL_INT(PC_SPA_PASSTHROUGH, pc_spa_route("/api/state", "/api/"));
    TEST_ASSERT_EQUAL_INT(PC_SPA_PASSTHROUGH, pc_spa_route("/api/devices/42", "/api/"));
    // No API prefix configured: an /api path is just a route.
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route("/api/state", NULL));
    // Null path: bail out before touching it.
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route(NULL, "/api/"));
    // A path that doesn't start with '/' is neither the empty/root case nor under the prefix.
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FILE, pc_spa_route("relative.txt", "/api/"));
    // Non-null but empty API prefix: treated as "none configured".
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route("/dashboard", ""));
}

// --- fallback HMI ---------------------------------------------------------

static pc_spa_ctx healthy_ctx(void)
{
    pc_spa_ctx c;
    c.api_prefix = "/api/";
    c.shell_available = PROTO_TRUE;
    c.client_scripting = PROTO_TRUE;
    c.degraded = PROTO_FALSE;
    return c;
}

void test_route_ex_healthy_matches_the_plain_router(void)
{
    pc_spa_ctx c = healthy_ctx();
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route_ex("/dashboard", &c));
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FILE, pc_spa_route_ex("/app.js", &c));
    TEST_ASSERT_EQUAL_INT(PC_SPA_PASSTHROUGH, pc_spa_route_ex("/api/state", &c));
}

void test_missing_shell_falls_back(void)
{
    pc_spa_ctx c = healthy_ctx();
    c.shell_available = PROTO_FALSE; // half-finished upload, wiped filesystem
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FALLBACK, pc_spa_route_ex("/dashboard", &c));
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FALLBACK, pc_spa_route_ex("/", &c));
}

void test_non_scripting_client_falls_back(void)
{
    pc_spa_ctx c = healthy_ctx();
    c.client_scripting = PROTO_FALSE; // curl, a text browser, scripting disabled
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FALLBACK, pc_spa_route_ex("/devices/42", &c));
}

void test_degraded_device_falls_back(void)
{
    pc_spa_ctx c = healthy_ctx();
    c.degraded = PROTO_TRUE; // recovery mode / failsafe / low memory
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FALLBACK, pc_spa_route_ex("/dashboard", &c));
}

void test_api_still_passes_through_in_fallback(void)
{
    // The property that makes the fallback worth having: its own controls POST to these endpoints,
    // so cutting them off would leave an operator looking at a page that cannot actuate anything.
    pc_spa_ctx c = healthy_ctx();
    c.shell_available = PROTO_FALSE;
    c.client_scripting = PROTO_FALSE;
    c.degraded = PROTO_TRUE;
    TEST_ASSERT_EQUAL_INT(PC_SPA_PASSTHROUGH, pc_spa_route_ex("/api/stop", &c));
    TEST_ASSERT_EQUAL_INT(PC_SPA_PASSTHROUGH, pc_spa_route_ex("/api/state", &c));
}

void test_assets_are_unaffected_by_degradation(void)
{
    // An asset request stays an asset request; a real 404 is the caller's to report. Rewriting it to
    // the fallback page would hand the browser HTML where it asked for CSS.
    pc_spa_ctx c = healthy_ctx();
    c.shell_available = PROTO_FALSE;
    c.degraded = PROTO_TRUE;
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_FILE, pc_spa_route_ex("/style.css", &c));
}

void test_route_ex_null_ctx_degrades_to_the_plain_router(void)
{
    TEST_ASSERT_EQUAL_INT(PC_SPA_SERVE_SHELL, pc_spa_route_ex("/dashboard", NULL));
}

// --- conditional UI streaming ---------------------------------------------

static proto_bool when_true(void *)
{
    return PROTO_TRUE;
}
static proto_bool when_false(void *)
{
    return PROTO_FALSE;
}
static proto_bool when_flag(void *ctx)
{
    return *(proto_bool *)ctx;
}

static const pc_ui_fragment FRAGS[] = {
    {"header", "<h1>HMI</h1>", NULL},
    {"alarm", "<p>ALARM</p>", when_flag},
    {"controls", "<button>stop</button>", when_true},
    {"debug", "<pre>debug</pre>", when_false},
};

// Drain a stream through a buffer of exactly `chunk` bytes.
static string drain(pc_ui_stream *s, size_t chunk)
{
    string out;
    char buf[64];
    size_t n;
    while ((n = pc_ui_stream_next(s, buf, chunk < sizeof(buf) ? chunk : sizeof(buf))) > 0)
    {
        out.append(buf, n);
    }
    return out;
}

void test_stream_includes_only_passing_fragments(void)
{
    proto_bool alarm = PROTO_FALSE;
    pc_ui_stream s;
    pc_ui_stream_begin(&s, FRAGS, 4, &alarm);
    TEST_ASSERT_EQUAL_STRING("<h1>HMI</h1><button>stop</button>", drain(&s, 64).c_str());
    TEST_ASSERT_TRUE(pc_ui_stream_done(&s));
}

void test_stream_reflects_the_predicate_state(void)
{
    proto_bool alarm = PROTO_TRUE;
    pc_ui_stream s;
    pc_ui_stream_begin(&s, FRAGS, 4, &alarm);
    TEST_ASSERT_EQUAL_STRING("<h1>HMI</h1><p>ALARM</p><button>stop</button>", drain(&s, 64).c_str());
}

void test_stream_is_chunk_size_independent(void)
{
    // The point of the cursor: a buffer smaller than a single fragment must still produce the exact
    // same bytes, resuming mid-fragment across calls.
    proto_bool alarm = PROTO_TRUE;
    for (size_t chunk = 1; chunk <= 40; chunk++)
    {
        pc_ui_stream s;
        pc_ui_stream_begin(&s, FRAGS, 4, &alarm);
        TEST_ASSERT_EQUAL_STRING("<h1>HMI</h1><p>ALARM</p><button>stop</button>", drain(&s, chunk).c_str());
        TEST_ASSERT_TRUE(pc_ui_stream_done(&s));
    }
}

void test_stream_all_excluded_emits_nothing(void)
{
    static const pc_ui_fragment none[] = {{"a", "<p>a</p>", when_false}, {"b", "<p>b</p>", when_false}};
    pc_ui_stream s;
    pc_ui_stream_begin(&s, none, 2, NULL);
    char buf[32];
    TEST_ASSERT_EQUAL_UINT32(0, pc_ui_stream_next(&s, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(pc_ui_stream_done(&s));
}

void test_stream_empty_set_is_done_immediately(void)
{
    pc_ui_stream s;
    pc_ui_stream_begin(&s, FRAGS, 0, NULL);
    TEST_ASSERT_TRUE(pc_ui_stream_done(&s));
    char buf[8];
    TEST_ASSERT_EQUAL_UINT32(0, pc_ui_stream_next(&s, buf, sizeof(buf)));
}

void test_stream_skips_a_null_body(void)
{
    static const pc_ui_fragment withnull[] = {{"a", NULL, NULL}, {"b", "<p>b</p>", NULL}};
    pc_ui_stream s;
    pc_ui_stream_begin(&s, withnull, 2, NULL);
    TEST_ASSERT_EQUAL_STRING("<p>b</p>", drain(&s, 64).c_str());
}

void test_stream_bad_args_do_not_crash(void)
{
    char buf[8];
    pc_ui_stream s;
    pc_ui_stream_begin(&s, FRAGS, 4, NULL);
    TEST_ASSERT_EQUAL_UINT32(0, pc_ui_stream_next(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_ui_stream_next(&s, NULL, 8));
    TEST_ASSERT_EQUAL_UINT32(0, pc_ui_stream_next(&s, buf, 0));
    pc_ui_stream_begin(NULL, FRAGS, 4, NULL); // must not crash
    TEST_ASSERT_TRUE(pc_ui_stream_done(NULL));
    pc_ui_stream n;
    pc_ui_stream_begin(&n, NULL, 5, NULL); // null set with a nonzero count
    TEST_ASSERT_TRUE(pc_ui_stream_done(&n));
}

void test_stream_not_done_mid_stream(void)
{
    // A valid, non-null stream that still has fragments left must report not-done - the counterpart
    // to the null-stream and already-finished cases covered elsewhere.
    proto_bool alarm = PROTO_TRUE;
    pc_ui_stream s;
    pc_ui_stream_begin(&s, FRAGS, 4, &alarm);
    TEST_ASSERT_FALSE(pc_ui_stream_done(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_has_extension);
    RUN_TEST(test_route);
    RUN_TEST(test_route_ex_healthy_matches_the_plain_router);
    RUN_TEST(test_missing_shell_falls_back);
    RUN_TEST(test_non_scripting_client_falls_back);
    RUN_TEST(test_degraded_device_falls_back);
    RUN_TEST(test_api_still_passes_through_in_fallback);
    RUN_TEST(test_assets_are_unaffected_by_degradation);
    RUN_TEST(test_route_ex_null_ctx_degrades_to_the_plain_router);
    RUN_TEST(test_stream_includes_only_passing_fragments);
    RUN_TEST(test_stream_reflects_the_predicate_state);
    RUN_TEST(test_stream_is_chunk_size_independent);
    RUN_TEST(test_stream_all_excluded_emits_nothing);
    RUN_TEST(test_stream_empty_set_is_done_immediately);
    RUN_TEST(test_stream_skips_a_null_body);
    RUN_TEST(test_stream_bad_args_do_not_crash);
    RUN_TEST(test_stream_not_done_mid_stream);
    return UNITY_END();
}
