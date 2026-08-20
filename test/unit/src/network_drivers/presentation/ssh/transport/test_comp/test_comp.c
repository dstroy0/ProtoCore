// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/comp.c (RFC 4253 sec 6.2): when each direction's compression starts, and when it starts
// over. "none" and "zlib" are the RFC's two methods; "zlib@openssh.com" is the same codec holding
// off until authentication has succeeded, so nothing before that point is compressed.

#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/comp/comp.h"
#include "network_drivers/presentation/ssh/transport/transport/transport.h"
#include <stdint.h>

#include <unity.h>

#if PROTOCORE_ENABLE_SSH_ZLIB

void setUp(void)
{
    Comp.reset_args.i = 0;
    Comp.reset(protocore_ssh_comp_span());
}
void tearDown(void)
{
    Comp.reset_args.i = 0;
    Comp.reset(protocore_ssh_comp_span());
}

// ---------------------------------------------------------------------------
// sec 6.2  "Initially, compression MUST be "none""
// ---------------------------------------------------------------------------

static void test_sec6_2_neither_direction_starts_compressed(void)
{
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
}

// Negotiating "none" leaves both directions uncompressed however many key exchanges run.
static void test_sec6_2_none_never_activates(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_NONE;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.set_c2s_args.i = 0;
    Comp.set_c2s_args.alg = SSH_COMP_NONE;
    Comp.set_c2s(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.on_auth_success_args.i = 0;
    Comp.on_auth_success(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
}

// ---------------------------------------------------------------------------
// sec 6.2  "zlib"
// ---------------------------------------------------------------------------
// "The compression context is initialized after each key exchange, and is passed from one packet to
// the next" - so the stream opens when the key exchange ends, not when the algorithm is negotiated.

static void test_sec6_2_zlib_starts_at_newkeys_not_at_negotiation(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.set_c2s_args.i = 0;
    Comp.set_c2s_args.alg = SSH_COMP_ZLIB;
    Comp.set_c2s(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok); // negotiated, not yet in use
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);

    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_TRUE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_TRUE(Comp.ok);
}

// The two directions are negotiated separately (sec 7.1 lists compression per direction), so one
// may be compressed while the other is not.
static void test_sec7_1_the_two_directions_are_independent(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.set_c2s_args.i = 0;
    Comp.set_c2s_args.alg = SSH_COMP_NONE;
    Comp.set_c2s(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_TRUE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
}

// "The compression context is initialized after each key exchange." A re-exchange starts the stream
// over rather than carrying the previous window across it.
static void test_sec6_2_each_key_exchange_reinitializes_the_context(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());

    static const uint8_t msg[] = "a payload long enough to be worth a back reference, twice over";
    const size_t len = sizeof(msg) - 1;
    uint8_t a[2048], b[2048], c[2048];
    size_t na = 0, nb = 0, nc = 0;

    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = a;
    Comp.s2c_args.dst_cap = sizeof(a);
    Comp.s2c_args.out_len = &na;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = b;
    Comp.s2c_args.dst_cap = sizeof(b);
    Comp.s2c_args.out_len = &nb;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);
    TEST_ASSERT_LESS_THAN_size_t(na, nb); // the second was matched out of the window

    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span()); // a re-exchange completes
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = c;
    Comp.s2c_args.dst_cap = sizeof(c);
    Comp.s2c_args.out_len = &nc;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);

    // The window went with the old context, so this costs what the first one did, not what the
    // second one did.
    TEST_ASSERT_EQUAL_size_t(na, nc);
}

// ---------------------------------------------------------------------------
// zlib@openssh.com  the same codec, held until authentication succeeds
// ---------------------------------------------------------------------------

static void test_delayed_does_not_start_at_newkeys(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB_DELAYED;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.set_c2s_args.i = 0;
    Comp.set_c2s_args.alg = SSH_COMP_ZLIB_DELAYED;
    Comp.set_c2s(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
}

static void test_delayed_starts_at_authentication_success(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB_DELAYED;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.set_c2s_args.i = 0;
    Comp.set_c2s_args.alg = SSH_COMP_ZLIB_DELAYED;
    Comp.set_c2s(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.on_auth_success_args.i = 0;
    Comp.on_auth_success(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_TRUE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_TRUE(Comp.ok);
}

// Authentication succeeding does not start plain "zlib" over: that one is the key exchange's.
static void test_auth_success_does_not_disturb_plain_zlib(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());

    static const uint8_t msg[] = "a payload long enough to be worth a back reference, twice over";
    const size_t len = sizeof(msg) - 1;
    uint8_t a[2048], b[2048];
    size_t na = 0, nb = 0;
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = a;
    Comp.s2c_args.dst_cap = sizeof(a);
    Comp.s2c_args.out_len = &na;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);

    Comp.on_auth_success_args.i = 0;
    Comp.on_auth_success(protocore_ssh_comp_span());
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = b;
    Comp.s2c_args.dst_cap = sizeof(b);
    Comp.s2c_args.out_len = &nb;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);
    TEST_ASSERT_LESS_THAN_size_t(na, nb); // the window survived, so this is still the cheap copy
}

// A second USERAUTH_SUCCESS cannot restart a delayed stream underneath a running connection.
static void test_delayed_start_is_idempotent(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB_DELAYED;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.on_auth_success_args.i = 0;
    Comp.on_auth_success(protocore_ssh_comp_span());

    static const uint8_t msg[] = "a payload long enough to be worth a back reference, twice over";
    const size_t len = sizeof(msg) - 1;
    uint8_t a[2048], b[2048];
    size_t na = 0, nb = 0;
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = a;
    Comp.s2c_args.dst_cap = sizeof(a);
    Comp.s2c_args.out_len = &na;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);

    Comp.on_auth_success_args.i = 0;
    Comp.on_auth_success(protocore_ssh_comp_span()); // again
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = b;
    Comp.s2c_args.dst_cap = sizeof(b);
    Comp.s2c_args.out_len = &nb;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);
    TEST_ASSERT_LESS_THAN_size_t(na, nb); // the stream continued rather than restarting
}

// ---------------------------------------------------------------------------
// the payload itself
// ---------------------------------------------------------------------------

// An inactive direction compresses nothing and says so: sec 6.2's "none" is the absence of this
// codec, not a pass through it, so the caller asks whether the direction is active and sends the
// payload as it stands when it is not.
static void test_inactive_direction_refuses_rather_than_transforming(void)
{
    static const uint8_t msg[] = "uncompressed payload";
    const size_t len = sizeof(msg) - 1;
    uint8_t out[256];
    size_t n = 0;

    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = msg;
    Comp.s2c_args.src_len = len;
    Comp.s2c_args.dst = out;
    Comp.s2c_args.dst_cap = sizeof(out);
    Comp.s2c_args.out_len = &n;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(-1, Comp.n);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_args.i = 0;
    Comp.c2s_args.src = msg;
    Comp.c2s_args.src_len = len;
    Comp.c2s_args.dst = out;
    Comp.c2s_args.dst_cap = sizeof(out);
    Comp.c2s_args.out_len = &n;
    Comp.c2s(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(-1, Comp.n);
}

// A compressed payload is not the payload, which is the only observable difference that matters.
static void test_active_direction_transforms_the_payload(void)
{
    uint8_t src[512];
    for (size_t k = 0; k < sizeof(src); k++)
    {
        src[k] = (uint8_t)('A' + (k % 4u));
    }
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());

    uint8_t out[2048];
    size_t n = 0;
    Comp.s2c_args.i = 0;
    Comp.s2c_args.src = src;
    Comp.s2c_args.src_len = sizeof(src);
    Comp.s2c_args.dst = out;
    Comp.s2c_args.dst_cap = sizeof(out);
    Comp.s2c_args.out_len = &n;
    Comp.s2c(protocore_ssh_comp_span());
    TEST_ASSERT_EQUAL_INT(0, Comp.n);
    TEST_ASSERT_LESS_THAN_size_t(sizeof(src), n);
}

// ---------------------------------------------------------------------------
// reset, and the pool's edge
// ---------------------------------------------------------------------------

// A slot handed back to the pool carries no algorithm and no stream into the next connection.
static void test_reset_clears_both_directions(void)
{
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.set_c2s_args.i = 0;
    Comp.set_c2s_args.alg = SSH_COMP_ZLIB;
    Comp.set_c2s(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_TRUE(Comp.ok);

    Comp.reset_args.i = 0;
    Comp.reset(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);

    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span()); // no algorithm negotiated on the new connection yet
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_active_args.i = 0;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
}

// The state is per slot, so one connection's compression is not another's.
static void test_state_is_per_slot(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    Comp.reset_args.i = 1;
    Comp.reset(protocore_ssh_comp_span());
    Comp.set_s2c_args.i = 0;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = 0;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = 0;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_TRUE(Comp.ok);
    Comp.s2c_active_args.i = 1;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
}

// A slot past the pool answers inactive and takes no state.
static void test_slot_past_the_pool_is_inert(void)
{
    const uint8_t bad = MAX_SSH_CONNS;
    Comp.reset_args.i = bad;
    Comp.reset(protocore_ssh_comp_span());
    Comp.set_s2c_args.i = bad;
    Comp.set_s2c_args.alg = SSH_COMP_ZLIB;
    Comp.set_s2c(protocore_ssh_comp_span());
    Comp.on_newkeys_args.i = bad;
    Comp.on_newkeys(protocore_ssh_comp_span());
    Comp.on_auth_success_args.i = bad;
    Comp.on_auth_success(protocore_ssh_comp_span());
    Comp.s2c_active_args.i = bad;
    Comp.s2c_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
    Comp.c2s_active_args.i = bad;
    Comp.c2s_active(protocore_ssh_comp_span());
    TEST_ASSERT_FALSE(Comp.ok);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec6_2_neither_direction_starts_compressed);
    RUN_TEST(test_sec6_2_none_never_activates);
    RUN_TEST(test_sec6_2_zlib_starts_at_newkeys_not_at_negotiation);
    RUN_TEST(test_sec7_1_the_two_directions_are_independent);
    RUN_TEST(test_sec6_2_each_key_exchange_reinitializes_the_context);
    RUN_TEST(test_delayed_does_not_start_at_newkeys);
    RUN_TEST(test_delayed_starts_at_authentication_success);
    RUN_TEST(test_auth_success_does_not_disturb_plain_zlib);
    RUN_TEST(test_delayed_start_is_idempotent);
    RUN_TEST(test_inactive_direction_refuses_rather_than_transforming);
    RUN_TEST(test_active_direction_transforms_the_payload);
    RUN_TEST(test_reset_clears_both_directions);
    RUN_TEST(test_state_is_per_slot);
    RUN_TEST(test_slot_past_the_pool_is_inert);
    return UNITY_END();
}

#else // PROTOCORE_ENABLE_SSH_ZLIB

void setUp(void)
{
}
void tearDown(void)
{
}

static void test_compression_is_not_built(void)
{
    TEST_IGNORE_MESSAGE("PROTOCORE_ENABLE_SSH_ZLIB is off");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_compression_is_not_built);
    return UNITY_END();
}

#endif
