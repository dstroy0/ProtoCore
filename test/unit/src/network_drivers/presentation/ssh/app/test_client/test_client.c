// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// app/client.c (RFC 4252 sec 7): what an application calls to drive and provision the
// outbound role - where the connection has got to, and the public half of a seed for
// authorized_keys, which needs no connection at all.

#include "network_drivers/presentation/ssh/app/client.h"
#include "network_drivers/presentation/ssh/client/client.h"
#include <stdint.h>
#include <unity.h>

#if PROTOCORE_ENABLE_SSH_CLIENT

void setUp(void)
{

    protocore_ssh_client_end(); // whatever a previous case left
}
void tearDown(void)
{

    protocore_ssh_client_end();
}

// The application-facing accessor and the engine's own report the same thing: one is a view of the
// other, not a second copy of the state.
static void test_the_app_accessor_reports_the_engine_state(void)
{
    TEST_ASSERT_EQUAL(SshClient.state(), protocore_ssh_client_state_get());
}

// "up" is one specific phase, not "anything but idle".
static void test_up_means_the_forward_is_established(void)
{
    TEST_ASSERT_FALSE(protocore_ssh_client_up()); // idle is not up
    TEST_ASSERT_EQUAL(PROTOCORE_SSH_CLIENT_UP == SshClient.state(), protocore_ssh_client_up());
}

// The public half of a seed, derived without a connection: this is what goes in the relay's
// authorized_keys before the device has ever dialled it.
static void test_sec7_public_key_is_derived_from_the_seed(void)
{
    static const uint8_t seed[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                     0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                     0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
    uint8_t pub[32];
    protocore_ssh_client_pubkey(seed, pub);

    // A derived key is not the seed, and is not zero.
    uint8_t acc = 0;
    proto_bool same_as_seed = PROTO_TRUE;
    for (int k = 0; k < 32; k++)
    {
        acc |= pub[k];
        if (pub[k] != seed[k])
        {
            same_as_seed = PROTO_FALSE;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, acc);
    TEST_ASSERT_FALSE(same_as_seed);
}

// The derivation is a function of the seed: the same seed always gives the same public half, or the
// key in authorized_keys stops matching the device.
static void test_sec7_the_derivation_is_deterministic(void)
{
    static const uint8_t seed[32] = {0x4c, 0xcd, 0x08, 0x9b, 0x28, 0xff, 0x96, 0xda, 0x9d, 0xb6, 0xc3,
                                     0x46, 0xec, 0x11, 0x4e, 0x0f, 0x5b, 0x8a, 0x31, 0x9f, 0x35, 0xab,
                                     0xa6, 0x24, 0xda, 0x8c, 0xf6, 0xed, 0x4f, 0xb8, 0xa6, 0xfb};
    uint8_t a[32], b[32];
    protocore_ssh_client_pubkey(seed, a);
    protocore_ssh_client_pubkey(seed, b);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 32);
}

// Different seeds are different devices.
static void test_sec7_different_seeds_give_different_keys(void)
{
    static const uint8_t s1[32] = {1};
    static const uint8_t s2[32] = {2};
    uint8_t a[32], b[32];
    protocore_ssh_client_pubkey(s1, a);
    protocore_ssh_client_pubkey(s2, b);

    proto_bool same = PROTO_TRUE;
    for (int k = 0; k < 32; k++)
    {
        if (a[k] != b[k])
        {
            same = PROTO_FALSE;
            break;
        }
    }
    TEST_ASSERT_FALSE(same);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_app_accessor_reports_the_engine_state);
    RUN_TEST(test_up_means_the_forward_is_established);
    RUN_TEST(test_sec7_public_key_is_derived_from_the_seed);
    RUN_TEST(test_sec7_the_derivation_is_deterministic);
    RUN_TEST(test_sec7_different_seeds_give_different_keys);
    return UNITY_END();
}

#else // PROTOCORE_ENABLE_SSH_CLIENT

void setUp(void)
{
}
void tearDown(void)
{
}

static void test_this_configuration_does_not_build_it(void)
{
    TEST_IGNORE_MESSAGE("PROTOCORE_ENABLE_SSH_CLIENT is off");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_this_configuration_does_not_build_it);
    return UNITY_END();
}

#endif
