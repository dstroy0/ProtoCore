// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/comp.c (RFC 4253 sec 6.2): when each direction's compression starts, and when it starts
// over. "none" and "zlib" are the RFC's two methods; "zlib@openssh.com" is the same codec holding
// off until authentication has succeeded, so nothing before that point is compressed.

#include "network_drivers/presentation/ssh/common/common.h"
#include "network_drivers/presentation/ssh/transport/comp/comp.h"
#include "network_drivers/presentation/ssh/transport/transport/transport.h"
#include <stdint.h>

#include <unity.h>

#if PROTOCORE_ENABLE_SSH_ZLIB

void setUp(void)
{
    Comp.reset(protocore_ssh_comp_span(), 0);
}
void tearDown(void)
{
    Comp.reset(protocore_ssh_comp_span(), 0);
}

// ---------------------------------------------------------------------------
// sec 6.2  "Initially, compression MUST be "none""
// ---------------------------------------------------------------------------

static void test_sec6_2_neither_direction_starts_compressed(void)
{
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
}

// Negotiating "none" leaves both directions uncompressed however many key exchanges run.
static void test_sec6_2_none_never_activates(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_NONE);
    Comp.set_c2s(protocore_ssh_comp_span(), 0, SSH_COMP_NONE);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    Comp.on_auth_success(protocore_ssh_comp_span(), 0);
    comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
}

// ---------------------------------------------------------------------------
// sec 6.2  "zlib"
// ---------------------------------------------------------------------------
// "The compression context is initialized after each key exchange, and is passed from one packet to
// the next" - so the stream opens when the key exchange ends, not when the algorithm is negotiated.

static void test_sec6_2_zlib_starts_at_newkeys_not_at_negotiation(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.set_c2s(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok); // negotiated, not yet in use
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);

    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_TRUE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_TRUE(comp_ok);
}

// The two directions are negotiated separately (sec 7.1 lists compression per direction), so one
// may be compressed while the other is not.
static void test_sec7_1_the_two_directions_are_independent(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.set_c2s(protocore_ssh_comp_span(), 0, SSH_COMP_NONE);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_TRUE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
}

// "The compression context is initialized after each key exchange." A re-exchange starts the stream
// over rather than carrying the previous window across it.
static void test_sec6_2_each_key_exchange_reinitializes_the_context(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);

    static const uint8_t msg[] = "a payload long enough to be worth a back reference, twice over";
    const size_t len = sizeof(msg) - 1;
    uint8_t a[2048], b[2048], c[2048];
    size_t na = 0, nb = 0, nc = 0;

    int comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, a, sizeof(a), &na);
    TEST_ASSERT_EQUAL_INT(0, comp_n);
    comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, b, sizeof(b), &nb);
    TEST_ASSERT_EQUAL_INT(0, comp_n);
    TEST_ASSERT_LESS_THAN_size_t(na, nb); // the second was matched out of the window

    Comp.on_newkeys(protocore_ssh_comp_span(), 0); // a re-exchange completes
    comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, c, sizeof(c), &nc);
    TEST_ASSERT_EQUAL_INT(0, comp_n);

    // The window went with the old context, so this costs what the first one did, not what the
    // second one did.
    TEST_ASSERT_EQUAL_size_t(na, nc);
}

// ---------------------------------------------------------------------------
// zlib@openssh.com  the same codec, held until authentication succeeds
// ---------------------------------------------------------------------------

static void test_delayed_does_not_start_at_newkeys(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB_DELAYED);
    Comp.set_c2s(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB_DELAYED);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
}

static void test_delayed_starts_at_authentication_success(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB_DELAYED);
    Comp.set_c2s(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB_DELAYED);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    Comp.on_auth_success(protocore_ssh_comp_span(), 0);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_TRUE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_TRUE(comp_ok);
}

// Authentication succeeding does not start plain "zlib" over: that one is the key exchange's.
static void test_auth_success_does_not_disturb_plain_zlib(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);

    static const uint8_t msg[] = "a payload long enough to be worth a back reference, twice over";
    const size_t len = sizeof(msg) - 1;
    uint8_t a[2048], b[2048];
    size_t na = 0, nb = 0;
    int comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, a, sizeof(a), &na);
    TEST_ASSERT_EQUAL_INT(0, comp_n);

    Comp.on_auth_success(protocore_ssh_comp_span(), 0);
    comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, b, sizeof(b), &nb);
    TEST_ASSERT_EQUAL_INT(0, comp_n);
    TEST_ASSERT_LESS_THAN_size_t(na, nb); // the window survived, so this is still the cheap copy
}

// A second USERAUTH_SUCCESS cannot restart a delayed stream underneath a running connection.
static void test_delayed_start_is_idempotent(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB_DELAYED);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    Comp.on_auth_success(protocore_ssh_comp_span(), 0);

    static const uint8_t msg[] = "a payload long enough to be worth a back reference, twice over";
    const size_t len = sizeof(msg) - 1;
    uint8_t a[2048], b[2048];
    size_t na = 0, nb = 0;
    int comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, a, sizeof(a), &na);
    TEST_ASSERT_EQUAL_INT(0, comp_n);

    Comp.on_auth_success(protocore_ssh_comp_span(), 0); // again
    comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, b, sizeof(b), &nb);
    TEST_ASSERT_EQUAL_INT(0, comp_n);
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

    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    int comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, msg, len, out, sizeof(out), &n);
    TEST_ASSERT_EQUAL_INT(-1, comp_n);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    comp_n = Comp.c2s(protocore_ssh_comp_span(), 0, msg, len, out, sizeof(out), &n);
    TEST_ASSERT_EQUAL_INT(-1, comp_n);
}

// A compressed payload is not the payload, which is the only observable difference that matters.
static void test_active_direction_transforms_the_payload(void)
{
    uint8_t src[512];
    for (size_t k = 0; k < sizeof(src); k++)
    {
        src[k] = (uint8_t)('A' + (k % 4u));
    }
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);

    uint8_t out[2048];
    size_t n = 0;
    int comp_n = Comp.s2c(protocore_ssh_comp_span(), 0, src, sizeof(src), out, sizeof(out), &n);
    TEST_ASSERT_EQUAL_INT(0, comp_n);
    TEST_ASSERT_LESS_THAN_size_t(sizeof(src), n);
}

// ---------------------------------------------------------------------------
// reset, and the pool's edge
// ---------------------------------------------------------------------------

// A slot handed back to the pool carries no algorithm and no stream into the next connection.
static void test_reset_clears_both_directions(void)
{
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.set_c2s(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_TRUE(comp_ok);

    Comp.reset(protocore_ssh_comp_span(), 0);
    comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);

    Comp.on_newkeys(protocore_ssh_comp_span(), 0); // no algorithm negotiated on the new connection yet
    comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_FALSE(comp_ok);
}

// The state is per slot, so one connection's compression is not another's.
static void test_state_is_per_slot(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    Comp.reset(protocore_ssh_comp_span(), 1);
    Comp.set_s2c(protocore_ssh_comp_span(), 0, SSH_COMP_ZLIB);
    Comp.on_newkeys(protocore_ssh_comp_span(), 0);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 0);
    TEST_ASSERT_TRUE(comp_ok);
    comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), 1);
    TEST_ASSERT_FALSE(comp_ok);
}

// A slot past the pool answers inactive and takes no state.
static void test_slot_past_the_pool_is_inert(void)
{
    const uint8_t bad = MAX_SSH_CONNS;
    Comp.reset(protocore_ssh_comp_span(), bad);
    Comp.set_s2c(protocore_ssh_comp_span(), bad, SSH_COMP_ZLIB);
    Comp.on_newkeys(protocore_ssh_comp_span(), bad);
    Comp.on_auth_success(protocore_ssh_comp_span(), bad);
    proto_bool comp_ok = Comp.s2c_active(protocore_ssh_comp_span(), bad);
    TEST_ASSERT_FALSE(comp_ok);
    comp_ok = Comp.c2s_active(protocore_ssh_comp_span(), bad);
    TEST_ASSERT_FALSE(comp_ok);
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
