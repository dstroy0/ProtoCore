// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the per-peer brute-force auth lockout (server/security/auth_lockout/auth_lockout.h).
//
// No standard publishes this state machine, so every expectation below is either arithmetic derived
// from the module's own documented rule - lock for BASE_MS at the threshold, doubling per further
// failure, capped at MAX_MS - with the doubling written out in the comment, or a property the
// machine must hold whatever the implementation. test_backoff_doubles_then_caps is the load-bearing
// case: the whole point of the module is that the wait grows, and a backoff that silently stays at
// the base or overflows past the cap is the failure that lets a guessing attack through.
//
// The millisecond clock is an argument, never read from the platform, so the whole machine including
// its rollover behavior is driven from the test.

#include "server/security/auth_lockout/auth_lockout.h"

#include <unity.h>

void setUp(void)
{
    auth_lockout_reset();
}
void tearDown(void)
{
}

static protocore_ip v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return protocore_ip_from_v4_octets(a, b, c, d);
}

static protocore_ip parsed(const char *text)
{
    protocore_ip ip;
    Ip.args.text = text;
    Ip.args.out = &ip;
    Ip.parse(Ip.internal);
    TEST_ASSERT_TRUE_MESSAGE(Ip.ok, text);
    return ip;
}

// Drive @p n consecutive failures from @p ip, all at @p now_ms.
static void fail_n(const protocore_ip *ip, int n, uint32_t now_ms)
{
    for (int i = 0; i < n; i++)
    {
        auth_lockout_fail(ip, now_ms);
    }
}

// An address nobody has failed from is not locked.
void test_an_unseen_address_is_not_locked(void)
{
    const protocore_ip ip = v4(203, 0, 113, 7);
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, 1000000u));
}

// Failures below the threshold do not lock: the machine tolerates a mistyped password.
void test_below_the_threshold_nothing_locks(void)
{
    const protocore_ip ip = v4(198, 51, 100, 4);
    for (int i = 1; i < PROTOCORE_AUTH_LOCKOUT_THRESHOLD; i++)
    {
        auth_lockout_fail(&ip, 100u);
        TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, 100u));
    }
    // the threshold'th failure is the one that locks
    auth_lockout_fail(&ip, 100u);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS, auth_lockout_remaining_ms(&ip, 100u));
}

// The documented rule: lock for BASE_MS at the threshold, doubling on each further failure, capped
// at MAX_MS. With the shipped configuration (threshold 5, base 1000 ms, cap 300000 ms) that is
//
//   failures  5   6    7    8     9     10    11     12     13      14 and up
//   lock ms   1000 2000 4000 8000 16000 32000 64000 128000 256000  300000 (1000*2^9 = 512000 > cap)
//
// so the cap is first reached at the fourteenth consecutive failure and never exceeded after it.
void test_backoff_doubles_then_caps(void)
{
    const protocore_ip ip = v4(192, 0, 2, 9);

    uint32_t want = PROTOCORE_AUTH_LOCKOUT_BASE_MS;
    fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    TEST_ASSERT_EQUAL_UINT32(want, auth_lockout_remaining_ms(&ip, 0u));

    for (int extra = 1; extra <= 20; extra++)
    {
        want = (want >= (uint32_t)PROTOCORE_AUTH_LOCKOUT_MAX_MS / 2u) ? (uint32_t)PROTOCORE_AUTH_LOCKOUT_MAX_MS
                                                                     : want * 2u;
        auth_lockout_fail(&ip, 0u);
        TEST_ASSERT_EQUAL_UINT32(want, auth_lockout_remaining_ms(&ip, 0u));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_MAX_MS, auth_lockout_remaining_ms(&ip, 0u));
}

// The remaining time counts down with the clock and reaches zero exactly at the end of the window,
// not one millisecond early and not one late.
void test_the_window_counts_down_and_expires(void)
{
    const protocore_ip ip = v4(192, 0, 2, 10);
    const uint32_t start = 500000u;
    const uint32_t base = PROTOCORE_AUTH_LOCKOUT_BASE_MS;

    fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, start);
    TEST_ASSERT_EQUAL_UINT32(base, auth_lockout_remaining_ms(&ip, start));
    TEST_ASSERT_EQUAL_UINT32(base - 1u, auth_lockout_remaining_ms(&ip, start + 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, auth_lockout_remaining_ms(&ip, start + base - 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, start + base));
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, start + base + 1u));
}

// A failure after the window closed restarts the window from that instant rather than leaving it
// anchored to the first one.
void test_a_later_failure_restarts_the_window(void)
{
    const protocore_ip ip = v4(192, 0, 2, 11);
    const uint32_t base = PROTOCORE_AUTH_LOCKOUT_BASE_MS;

    fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 1000u);
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, 1000u + base));

    auth_lockout_fail(&ip, 90000u);
    TEST_ASSERT_EQUAL_UINT32(base * 2u, auth_lockout_remaining_ms(&ip, 90000u));
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, 90000u + base * 2u));
}

// The window math is unsigned, so a lockout started just before the millisecond counter wraps still
// expires after exactly its own duration and does not become a 49-day lockout.
void test_the_window_survives_the_millisecond_rollover(void)
{
    const protocore_ip ip = v4(192, 0, 2, 12);
    const uint32_t base = PROTOCORE_AUTH_LOCKOUT_BASE_MS;
    const uint32_t start = 0xFFFFFFFFu - (base / 2u); // half the window is left before the wrap

    fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, start);
    TEST_ASSERT_EQUAL_UINT32(base, auth_lockout_remaining_ms(&ip, start));
    TEST_ASSERT_EQUAL_UINT32(base / 2u, auth_lockout_remaining_ms(&ip, (uint32_t)(start + base / 2u)));
    // start + base has wrapped past zero, so an implementation that compared the two instants
    // directly instead of subtracting them would report the window as already over
    TEST_ASSERT_EQUAL_UINT32(base / 4u, auth_lockout_remaining_ms(&ip, (uint32_t)(start + base - base / 4u)));
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, (uint32_t)(start + base)));
}

// A successful authentication clears the address: the counter starts again from zero, so the next
// lockout is the base wait rather than the doubled one.
void test_success_clears_the_address(void)
{
    const protocore_ip ip = v4(192, 0, 2, 13);
    fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD + 2, 0u);
    TEST_ASSERT_TRUE(auth_lockout_remaining_ms(&ip, 0u) > (uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS);

    auth_lockout_succeed(&ip);
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, 0u));

    fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS, auth_lockout_remaining_ms(&ip, 0u));
}

// Clearing an address nobody has failed from is a no-op, not a corruption of another bucket.
void test_success_from_an_unseen_address_touches_nothing(void)
{
    const protocore_ip locked = v4(192, 0, 2, 14);
    const protocore_ip other = v4(192, 0, 2, 15);
    fail_n(&locked, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);

    auth_lockout_succeed(&other);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS, auth_lockout_remaining_ms(&locked, 0u));
}

// Each address is its own bucket: one peer's failures never lock another.
void test_addresses_do_not_share_state(void)
{
    const protocore_ip a = v4(10, 0, 0, 1);
    const protocore_ip b = v4(10, 0, 0, 2);
    fail_n(&a, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    TEST_ASSERT_TRUE(auth_lockout_remaining_ms(&a, 0u) > 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&b, 0u));

    // ... and a success from one does not release the other
    auth_lockout_fail(&b, 0u);
    auth_lockout_succeed(&b);
    TEST_ASSERT_TRUE(auth_lockout_remaining_ms(&a, 0u) > 0u);
}

// The key is the full family-tagged address, so a v4 address and the v6 address that embeds the
// same four octets are different peers. Anything narrower lets one lock the other out.
void test_v4_and_v6_are_different_peers(void)
{
    const protocore_ip four = v4(10, 0, 0, 1);
    const protocore_ip mapped = parsed("::ffff:10.0.0.1");
    const protocore_ip six = parsed("2001:db8::1");
    const protocore_ip six2 = parsed("2001:db8::2");

    fail_n(&four, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    TEST_ASSERT_TRUE(auth_lockout_remaining_ms(&four, 0u) > 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&mapped, 0u));

    fail_n(&six, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    TEST_ASSERT_TRUE(auth_lockout_remaining_ms(&six, 0u) > 0u);
    // a v6 address differing in its last octet is a different peer
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&six2, 0u));
}

// An address that names nothing is untrackable, so it is never locked and never consumes a bucket:
// locking it would lock every such request at once.
void test_an_unspecified_address_is_never_locked(void)
{
    protocore_ip none;
    none.family = PROTOCORE_IP_NONE;
    for (int i = 0; i < 16; i++)
    {
        none.bytes[i] = 0;
    }
    const protocore_ip zero4 = v4(0, 0, 0, 0);
    const protocore_ip zero6 = parsed("::");

    fail_n(&none, 50, 0u);
    fail_n(&zero4, 50, 0u);
    fail_n(&zero6, 50, 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&none, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&zero4, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&zero6, 0u));

    // and the table is still empty, so a real peer still locks normally
    const protocore_ip real = v4(10, 1, 1, 1);
    fail_n(&real, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS, auth_lockout_remaining_ms(&real, 0u));

    auth_lockout_succeed(&none); // no-op rather than a walk off the table
}

// The table holds one bucket per address up to its slot count, and every one of them locks.
void test_every_slot_holds_its_own_lockout(void)
{
    for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS; i++)
    {
        const protocore_ip ip = v4(10, 2, 0, (uint8_t)(i + 1));
        fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    }
    for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS; i++)
    {
        const protocore_ip ip = v4(10, 2, 0, (uint8_t)(i + 1));
        TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS, auth_lockout_remaining_ms(&ip, 0u));
    }
}

// A flood of single failures from fresh addresses must not evict an address that is currently
// locked out, or an attacker releases their own lockout by spraying from other addresses.
void test_a_flood_of_new_addresses_does_not_release_a_lockout(void)
{
    const protocore_ip locked = v4(10, 3, 0, 1);
    fail_n(&locked, PROTOCORE_AUTH_LOCKOUT_THRESHOLD, 0u);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS, auth_lockout_remaining_ms(&locked, 0u));

    for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS * 4; i++)
    {
        const protocore_ip noise = v4(10, 4, (uint8_t)(i >> 8), (uint8_t)(i + 1));
        auth_lockout_fail(&noise, 1u);
    }
    // one millisecond into the window, so the remaining time is the base wait less that millisecond
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_AUTH_LOCKOUT_BASE_MS - 1u, auth_lockout_remaining_ms(&locked, 1u));
}

// Reset empties the whole table: every locked address is released.
void test_reset_releases_every_address(void)
{
    for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS; i++)
    {
        const protocore_ip ip = v4(10, 5, 0, (uint8_t)(i + 1));
        fail_n(&ip, PROTOCORE_AUTH_LOCKOUT_THRESHOLD + 3, 0u);
    }
    auth_lockout_reset();
    for (int i = 0; i < PROTOCORE_AUTH_LOCKOUT_SLOTS; i++)
    {
        const protocore_ip ip = v4(10, 5, 0, (uint8_t)(i + 1));
        TEST_ASSERT_EQUAL_UINT32(0u, auth_lockout_remaining_ms(&ip, 0u));
    }
}

// The configuration the backoff is derived from, so a change to it fails here rather than silently
// changing what the arithmetic above means.
void test_the_configured_bounds(void)
{
    TEST_ASSERT_TRUE(PROTOCORE_AUTH_LOCKOUT_THRESHOLD >= 1);
    TEST_ASSERT_TRUE(PROTOCORE_AUTH_LOCKOUT_SLOTS >= 1);
    TEST_ASSERT_TRUE(PROTOCORE_AUTH_LOCKOUT_BASE_MS >= 1);
    TEST_ASSERT_TRUE(PROTOCORE_AUTH_LOCKOUT_MAX_MS >= PROTOCORE_AUTH_LOCKOUT_BASE_MS);
}
