// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the abstract logging macros (shared/log/log.h), built at PROTOCORE_LOG_LEVEL_INFO
// so DEBUG sits below the floor.
//
// No standard governs this module, so every case is a PROPERTY of the contract log.h states.
//
// The load-bearing pair is test_a_discarded_call_emits_nothing and
// test_a_discarded_call_does_not_evaluate_its_arguments. The whole reason the filter is the
// preprocessor rather than a runtime comparison is that a call below the floor must cost nothing -
// no sink call, no ring entry, and no evaluation of the arguments it names. A macro that quietly
// evaluated its arguments would reintroduce exactly the cost the design exists to remove, and
// nothing but a side-effecting argument can detect it.

#include "shared/log/log.h"

#include "mmgr/protoframe/protoframe.h"
#include "server/core/logbuf/logbuf.h"
#include <string.h>

#include <unity.h>

// "iface <name> up" - a literal, a string field, a literal.
static const protocore_field IFACE[] = {
    {PROTOCORE_FK_LIT, 0, 6, "iface "},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 3, " up"},
    PROTOCORE_END,
};

// "rx <n>" - a literal and an unsigned decimal.
static const protocore_field RX[] = {
    {PROTOCORE_FK_LIT, 0, 3, "rx "},
    PROTOCORE_U32,
    PROTOCORE_END,
};

// What the sink last received.
static int g_sink_calls;
static uint8_t g_sink_level;
static char g_sink_line[PROTOCORE_LOG_LINE_LEN];

static void capture(uint8_t level, const char *line)
{
    g_sink_calls++;
    g_sink_level = level;
    g_sink_line[0] = '\0';
    if (line)
    {
        size_t n = strlen(line);
        if (n >= sizeof(g_sink_line))
        {
            n = sizeof(g_sink_line) - 1;
        }
        memcpy(g_sink_line, line, n);
        g_sink_line[n] = '\0';
    }
}

static void install(protocore_log_sink_fn fn)
{
    LogV.sink = fn;
    Log.set_sink(protocore_log_span());
}

// Counts every evaluation of an argument handed to a log macro.
static int g_evaluated;

static const protocore_field *spec_probe(void)
{
    g_evaluated++;
    return IFACE;
}

static const protocore_fval *value_probe(const protocore_fval *v)
{
    g_evaluated++;
    return v;
}

static uint16_t held(void)
{
    Logbuf.held(protocore_logbuf_span());
    return Logbuf.count;
}

static const char *line_at(uint16_t i)
{
    Logbuf.read.i = i;
    Logbuf.at(protocore_logbuf_span());
    return Logbuf.text;
}

void setUp(void)
{
    Logbuf.trap.threshold = 0xFF;
    Logbuf.trap.cb = NULL;
    Logbuf.set_trap(protocore_logbuf_span());
    Logbuf.reset(protocore_logbuf_span());
    install(capture);
    g_sink_calls = 0;
    g_sink_level = 0xFF;
    g_sink_line[0] = '\0';
    g_evaluated = 0;
}

void tearDown(void)
{
    install(NULL);
}

// DEBUG is below the floor this build was compiled at, so the call reaches neither sink nor ring.
void test_a_discarded_call_emits_nothing(void)
{
    static const protocore_fval V[] = {PROTOCORE_VSTR("eth0")};
    PROTOCORE_LOGD(IFACE, V, 1);

    TEST_ASSERT_EQUAL_INT(0, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(0u, held());
}

// ...and it does not evaluate what it names, which a runtime level check could not promise.
void test_a_discarded_call_does_not_evaluate_its_arguments(void)
{
    static const protocore_fval V[] = {PROTOCORE_VSTR("eth0")};
    PROTOCORE_LOGD(spec_probe(), value_probe(V), 1);

    TEST_ASSERT_EQUAL_INT(0, g_evaluated);
    TEST_ASSERT_EQUAL_INT(0, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(0u, held());

    // the same probes on an emitted level ARE evaluated, so the probes themselves work
    PROTOCORE_LOGI(spec_probe(), value_probe(V), 1);
    TEST_ASSERT_EQUAL_INT(2, g_evaluated);
    TEST_ASSERT_EQUAL_INT(1, g_sink_calls);
}

// Each emitted level reaches the sink carrying its own severity.
void test_each_level_emits_with_its_own_severity(void)
{
    static const protocore_fval V[] = {PROTOCORE_VSTR("eth0")};

    PROTOCORE_LOGI(IFACE, V, 1);
    TEST_ASSERT_EQUAL_INT(1, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_LEVEL_INFO, g_sink_level);

    PROTOCORE_LOGW(IFACE, V, 1);
    TEST_ASSERT_EQUAL_INT(2, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_LEVEL_WARN, g_sink_level);

    PROTOCORE_LOGE(IFACE, V, 1);
    TEST_ASSERT_EQUAL_INT(3, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_LEVEL_ERROR, g_sink_level);
}

// The line handed to the sink is the frame the spec builds, with its values in spec order.
void test_the_emitted_line_is_the_built_frame(void)
{
    static const protocore_fval IF_V[] = {PROTOCORE_VSTR("eth0")};
    static const protocore_fval RX_V[] = {PROTOCORE_VU32(4096)};

    PROTOCORE_LOGI(IFACE, IF_V, 1);
    TEST_ASSERT_EQUAL_STRING("iface eth0 up", g_sink_line);

    PROTOCORE_LOGW(RX, RX_V, 1);
    TEST_ASSERT_EQUAL_STRING("rx 4096", g_sink_line);
}

// A NULL string field renders as empty rather than as "(null)" or a crash.
void test_a_null_string_field_renders_empty(void)
{
    static const protocore_fval V[] = {PROTOCORE_VSTR(NULL)};
    PROTOCORE_LOGI(IFACE, V, 1);
    TEST_ASSERT_EQUAL_INT(1, g_sink_calls);
    TEST_ASSERT_EQUAL_STRING("iface  up", g_sink_line);
}

// An emitted line also lands in the ring, stored as its severity letter, a space, then the message.
void test_an_emitted_line_reaches_the_ring(void)
{
    static const protocore_fval V[] = {PROTOCORE_VSTR("eth0")};

    PROTOCORE_LOGI(IFACE, V, 1);
    PROTOCORE_LOGE(IFACE, V, 1);

    TEST_ASSERT_EQUAL_UINT16(2u, held());
    TEST_ASSERT_EQUAL_STRING("I iface eth0 up", line_at(0));
    TEST_ASSERT_EQUAL_STRING("E iface eth0 up", line_at(1));
}

// The severity the macros pass and the severity the ring letters are keyed on are the same numbers:
// a divergence would file an error under the wrong letter.
void test_the_log_and_ring_severity_scales_agree(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_DEBUG, PROTOCORE_LOG_LEVEL_DEBUG);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_INFO, PROTOCORE_LOG_LEVEL_INFO);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_WARN, PROTOCORE_LOG_LEVEL_WARN);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_ERROR, PROTOCORE_LOG_LEVEL_ERROR);

    // and the scale is ordered low to high, with NONE above every emitting level
    TEST_ASSERT_TRUE(PROTOCORE_LOG_LEVEL_DEBUG < PROTOCORE_LOG_LEVEL_INFO);
    TEST_ASSERT_TRUE(PROTOCORE_LOG_LEVEL_INFO < PROTOCORE_LOG_LEVEL_WARN);
    TEST_ASSERT_TRUE(PROTOCORE_LOG_LEVEL_WARN < PROTOCORE_LOG_LEVEL_ERROR);
    TEST_ASSERT_TRUE(PROTOCORE_LOG_LEVEL_ERROR < PROTOCORE_LOG_LEVEL_NONE);

    // this build's floor is INFO, which is what makes DEBUG the discarded level here
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_LEVEL_INFO, PROTOCORE_LOG_LEVEL);
}

// Clearing the sink stops the sink calls; the ring keeps taking lines.
void test_clearing_the_sink(void)
{
    static const protocore_fval V[] = {PROTOCORE_VSTR("eth0")};

    install(NULL);
    PROTOCORE_LOGI(IFACE, V, 1);
    TEST_ASSERT_EQUAL_INT(0, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(1u, held());

    install(capture);
    PROTOCORE_LOGI(IFACE, V, 1);
    TEST_ASSERT_EQUAL_INT(1, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(2u, held());
}

// A frame with no spec has no shape to build, so nothing is emitted at all.
void test_a_null_spec_emits_nothing(void)
{
    static const protocore_fval V[] = {PROTOCORE_VSTR("eth0")};
    PROTOCORE_LOGI(NULL, V, 1);

    TEST_ASSERT_EQUAL_INT(0, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(0u, held());
}

// Values that do not match the spec are refused by the builder, so the line comes out empty rather
// than carrying a string reinterpreted as a number.
void test_mismatched_values_yield_an_empty_line(void)
{
    static const protocore_fval WRONG_KIND[] = {PROTOCORE_VU32(7)}; // IFACE declares a string field
    static const protocore_fval V[] = {PROTOCORE_VSTR("eth0")};

    PROTOCORE_LOGI(IFACE, WRONG_KIND, 1);
    TEST_ASSERT_EQUAL_INT(1, g_sink_calls);
    TEST_ASSERT_EQUAL_STRING("", g_sink_line);

    // and the wrong arity is refused the same way
    PROTOCORE_LOGI(IFACE, V, 0);
    TEST_ASSERT_EQUAL_INT(2, g_sink_calls);
    TEST_ASSERT_EQUAL_STRING("", g_sink_line);
}

// A line longer than the buffer is emitted empty, never truncated: half a log line reads as a
// different event.
void test_a_line_that_does_not_fit_is_emitted_empty(void)
{
    static char big[PROTOCORE_LOG_LINE_LEN * 2];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    const protocore_fval v[] = {PROTOCORE_VSTR(big)};
    PROTOCORE_LOGI(IFACE, v, 1);

    TEST_ASSERT_EQUAL_INT(1, g_sink_calls);
    TEST_ASSERT_EQUAL_STRING("", g_sink_line);
}
