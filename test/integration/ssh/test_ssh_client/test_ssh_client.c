// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for the SSH client role (ssh_client.c). Before this env the file was in no
// build_src_filter in the matrix, so nothing compiled it.
//
// The checks here need no relay: the tunnel's reported state before begin(), the cfg guard, and the
// provisioning key derivation - which runs through cli_crypto_work() into the connection's
// compile-time storage, so it also exercises the client's half of the memory map.

#include "crypto/asymmetric/ed25519.h"
#include "network_drivers/presentation/ssh/ssh_client.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include <string.h>

#include <unity.h>

static uint8_t tw[4096]; // test-side working bytes for the reference derivation

void setUp()
{
}
void tearDown()
{
}

// A fresh tunnel reports idle and down.
static void test_tunnel_state_starts_idle(void)
{
    TEST_ASSERT_EQUAL(PC_TUN_IDLE, pc_ssh_tunnel_state_get());
    TEST_ASSERT_FALSE(pc_ssh_tunnel_up());
}

// begin() refuses a null cfg and every null member of one, before it touches the network.
static void test_begin_rejects_incomplete_cfg(void)
{
    static const uint8_t seed[32] = {1};
    static const uint8_t pin[32] = {2};
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(NULL));

    pc_ssh_tunnel_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&cfg)); // no host

    cfg.host = "127.0.0.1";
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&cfg)); // no user

    cfg.user = "dev";
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&cfg)); // no auth seed

    cfg.auth_seed = seed;
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&cfg)); // no host pin

    cfg.host_pin = pin;
    (void)cfg; // a complete cfg would dial out, which this env has no peer for
}

// The provisioning helper derives the seed's ed25519 public half, and works out of the connection's
// storage rather than a borrow of its own.
static void test_tunnel_pubkey_matches_ed25519(void)
{
    static const uint8_t seed[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                     0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                     0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
    uint8_t want[32];
    pc_ed25519_pubkey(tw, want, seed);

    uint8_t got[32];
    memset(got, 0, sizeof(got));
    pc_ssh_tunnel_pubkey(seed, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

// end() is idempotent and leaves the tunnel idle whether or not one was ever begun.
static void test_end_is_idempotent(void)
{
    pc_ssh_tunnel_end();
    pc_ssh_tunnel_end();
    TEST_ASSERT_EQUAL(PC_TUN_IDLE, pc_ssh_tunnel_state_get());
    TEST_ASSERT_FALSE(pc_ssh_tunnel_up());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_tunnel_state_starts_idle);
    RUN_TEST(test_begin_rejects_incomplete_cfg);
    RUN_TEST(test_tunnel_pubkey_matches_ed25519);
    RUN_TEST(test_end_is_idempotent);
    return UNITY_END();
}
