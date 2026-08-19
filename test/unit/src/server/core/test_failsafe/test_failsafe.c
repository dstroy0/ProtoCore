// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the lifeline watchdog (server/core/failsafe.h).
//
// No standard governs a software watchdog, so every expectation here is either arithmetic derived
// from the module's own published definition of overdue - "the unsigned delta now - last_feed is
// correct across a millis() rollover" - with the derivation written beside it, or a property that
// must hold whatever the implementation: one bit per lifeline, one callback per stuck episode, a
// feed rearming it. The report is the exception: RFC 8259 sec 3 fixes false/true as bare literals
// and sec 4 fixes the object form.
//
// test_overdue_is_a_wrap_safe_unsigned_delta is the load-bearing case. This predicate is what
// drives a machine to its safe state, and 49.7 days after boot the millisecond counter wraps: a
// signed or clamped comparison there reports every lifeline overdue at once, which is a spurious
// emergency stop on a machine that was running correctly.

#include "server/core/failsafe/failsafe.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
    Failsafe.reset(protocore_failsafe_span());
}
void tearDown(void)
{
}

static int add(const char *name, uint32_t deadline_ms, uint32_t now)
{
    Failsafe.args.name = name;
    Failsafe.args.deadline_ms = deadline_ms;
    Failsafe.args.now = now;
    Failsafe.add(protocore_failsafe_span());
    return Failsafe.i32;
}

static proto_bool feed(int id, uint32_t now)
{
    Failsafe.args.id = id;
    Failsafe.args.now = now;
    Failsafe.feed(protocore_failsafe_span());
    return Failsafe.ok;
}

static uint32_t check(uint32_t now)
{
    Failsafe.args.now = now;
    Failsafe.check(protocore_failsafe_span());
    return Failsafe.breached;
}

static char g_json[512];

static const char *report(uint32_t now)
{
    Failsafe.args.now = now;
    Failsafe.out_args.out = g_json;
    Failsafe.out_args.cap = sizeof(g_json);
    Failsafe.json(protocore_failsafe_span());
    return g_json;
}

static int g_fires;
static int g_last_id;
static const char *g_last_name;

static void on_breach(int id, const char *name, void *arg)
{
    (void)arg;
    g_fires++;
    g_last_id = id;
    g_last_name = name;
}

static void install(protocore_failsafe_cb cb)
{
    g_fires = 0;
    g_last_id = -1;
    g_last_name = NULL;
    Failsafe.out_args.cb = cb;
    Failsafe.out_args.arg = NULL;
    Failsafe.on_breach(protocore_failsafe_span());
}

// overdue is (uint32_t)(now - last_feed) > deadline, so the gap is the modular difference.
//
//   last_feed = 0xFFFFFFF6 (2^32 - 10), now = 10
//   now - last_feed = 10 - (2^32 - 10) = 20  (mod 2^32)
//
// so 20 ms have really passed across the wrap. A deadline of 20 is met exactly and 19 is missed;
// a comparison that treated the operands as signed would see -4294967276 and report neither.
void test_overdue_is_a_wrap_safe_unsigned_delta(void)
{
    const uint32_t last = 0xFFFFFFF6u;
    TEST_ASSERT_FALSE(protocore_lifeline_overdue(10u, last, 20u));
    TEST_ASSERT_TRUE(protocore_lifeline_overdue(10u, last, 19u));

    // and away from the wrap, the same rule: equal to the deadline is still fed
    TEST_ASSERT_FALSE(protocore_lifeline_overdue(1100u, 1000u, 100u));
    TEST_ASSERT_TRUE(protocore_lifeline_overdue(1101u, 1000u, 100u));
    TEST_ASSERT_FALSE(protocore_lifeline_overdue(1000u, 1000u, 0u)); // fed this instant
}

// A lifeline is armed as if it had just checked in, so registering one during boot does not fire
// the moment the first check runs.
void test_a_lifeline_starts_fed(void)
{
    const int id = add("motor", 100u, 1000u);
    TEST_ASSERT_EQUAL_INT(0, id);
    TEST_ASSERT_EQUAL_HEX32(0u, check(1100u)); // exactly at the deadline
    TEST_ASSERT_EQUAL_HEX32(1u, check(1101u));
}

// A check-in moves the deadline forward from the moment it happened, not from the previous one.
void test_a_feed_moves_the_deadline(void)
{
    const int id = add("motor", 100u, 1000u);
    TEST_ASSERT_TRUE(feed(id, 1090u));
    TEST_ASSERT_EQUAL_HEX32(0u, check(1190u));
    TEST_ASSERT_EQUAL_HEX32(1u, check(1191u));
}

// The registry is a fixed table, so the slot past the last one is refused rather than written.
void test_the_registry_is_bounded(void)
{
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        TEST_ASSERT_EQUAL_INT(i, add("line", 100u, 0u));
    }
    TEST_ASSERT_EQUAL_INT(-1, add("one too many", 100u, 0u));
}

// The mask carries one bit per lifeline id, so a caller learns which subsystems wedged rather than
// only that something did.
void test_check_reports_one_bit_per_lifeline(void)
{
    const int a = add("a", 100u, 0u);
    const int b = add("b", 100u, 0u);
    const int c = add("c", 1000u, 0u);
    TEST_ASSERT_EQUAL_INT(0, a);
    TEST_ASSERT_EQUAL_INT(1, b);
    TEST_ASSERT_EQUAL_INT(2, c);

    TEST_ASSERT_TRUE(feed(b, 200u));
    const uint32_t mask = check(200u);
    TEST_ASSERT_EQUAL_HEX32((1u << 0), mask); // only a is past its 100 ms
    TEST_ASSERT_EQUAL_HEX32(0u, mask & (1u << 1));
    TEST_ASSERT_EQUAL_HEX32(0u, mask & (1u << 2));
}

// One stuck episode is one alarm: a lifeline that stays wedged keeps its bit set on every check but
// does not re-fire the callback, so a caller is not driven into its safe state once per tick.
void test_a_breach_fires_once_per_episode(void)
{
    install(on_breach);
    const int id = add("motor", 100u, 0u);
    TEST_ASSERT_EQUAL_HEX32(1u, check(500u));
    TEST_ASSERT_EQUAL_INT(1, g_fires);
    TEST_ASSERT_EQUAL_INT(id, g_last_id);
    TEST_ASSERT_EQUAL_STRING("motor", g_last_name);

    TEST_ASSERT_EQUAL_HEX32(1u, check(600u)); // still stuck, still reported
    TEST_ASSERT_EQUAL_HEX32(1u, check(700u));
    TEST_ASSERT_EQUAL_INT(1, g_fires); // and still one alarm
}

// A check-in ends the episode, so the next time it wedges it fires again.
void test_a_feed_rearms_the_callback(void)
{
    install(on_breach);
    const int id = add("motor", 100u, 0u);
    TEST_ASSERT_EQUAL_HEX32(1u, check(500u));
    TEST_ASSERT_EQUAL_INT(1, g_fires);

    TEST_ASSERT_TRUE(feed(id, 500u));
    TEST_ASSERT_EQUAL_HEX32(0u, check(550u));
    TEST_ASSERT_EQUAL_HEX32(1u, check(1000u));
    TEST_ASSERT_EQUAL_INT(2, g_fires);
}

// A feed names a lifeline by id, and one that names no armed lifeline is refused rather than
// silently keeping some other subsystem alive.
void test_a_feed_names_an_armed_lifeline(void)
{
    const int id = add("motor", 100u, 0u);
    TEST_ASSERT_TRUE(feed(id, 10u));
    TEST_ASSERT_FALSE(feed(-1, 10u));
    TEST_ASSERT_FALSE(feed(PROTOCORE_FAILSAFE_MAX_LIFELINES, 10u));
    TEST_ASSERT_FALSE(feed(id + 1, 10u)); // in range, never armed
}

// RFC 8259 sec 3: false and true are bare literals, not strings. Every armed lifeline is reported
// with the age its last check-in gives it and the deadline it was armed with.
void test_the_report_is_an_rfc8259_object(void)
{
    const int motor = add("motor", 100u, 1000u);
    (void)add("loop", 50u, 1000u);
    TEST_ASSERT_TRUE(feed(motor, 1200u));
    // motor: 1260 - 1200 = 60, inside its 100.  loop: 1260 - 1000 = 260, past its 50.
    TEST_ASSERT_EQUAL_STRING("{\"lifelines\":["
                             "{\"name\":\"motor\",\"overdue\":false,\"age_ms\":60,\"deadline_ms\":100},"
                             "{\"name\":\"loop\",\"overdue\":true,\"age_ms\":260,\"deadline_ms\":50}"
                             "]}",
                             report(1260u));
    TEST_ASSERT_EQUAL_INT((int)strlen(g_json), Failsafe.n);
}

// An empty registry still reports a well-formed object, so a panel parses it rather than failing on
// an empty body.
void test_an_empty_registry_still_reports_an_object(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"lifelines\":[]}", report(0u));
}

// A report is always terminated inside the destination, whatever the table holds.
void test_the_report_stays_inside_its_buffer(void)
{
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        (void)add("a name long enough to overrun a small destination", 100u, 0u);
    }
    char small[32];
    memset(small, 'x', sizeof(small));
    Failsafe.args.now = 500u;
    Failsafe.out_args.out = small;
    Failsafe.out_args.cap = sizeof(small);
    Failsafe.json(protocore_failsafe_span());
    TEST_ASSERT_LESS_THAN_size_t(sizeof(small), strlen(small));
    TEST_ASSERT_EQUAL_INT((int)strlen(small), Failsafe.n);
}

void test_the_report_refuses_null_and_zero_capacity(void)
{
    (void)add("motor", 100u, 0u);
    char buf[64];
    buf[0] = 'x';
    Failsafe.args.now = 0u;
    Failsafe.out_args.out = NULL;
    Failsafe.out_args.cap = sizeof(buf);
    Failsafe.json(protocore_failsafe_span());
    TEST_ASSERT_EQUAL_INT(0, Failsafe.n);

    Failsafe.out_args.out = buf;
    Failsafe.out_args.cap = 0;
    Failsafe.json(protocore_failsafe_span());
    TEST_ASSERT_EQUAL_INT(0, Failsafe.n);
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
}

// A reset disarms every lifeline and drops the callback, so nothing left over from a previous
// configuration can fire.
void test_reset_empties_the_registry_and_drops_the_callback(void)
{
    install(on_breach);
    const int id = add("motor", 100u, 0u);
    Failsafe.reset(protocore_failsafe_span());
    TEST_ASSERT_EQUAL_HEX32(0u, check(100000u));
    TEST_ASSERT_EQUAL_INT(0, g_fires);
    TEST_ASSERT_FALSE(feed(id, 0u));
    TEST_ASSERT_EQUAL_INT(0, add("first again", 100u, 0u)); // the table starts from slot zero
}
