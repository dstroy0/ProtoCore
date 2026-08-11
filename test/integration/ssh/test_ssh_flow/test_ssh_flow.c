// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for ssh/connection/ssh_flow_control.c - the RFC 4254 sec 5.2 channel window.
//
// The oracle is RFC 4254 sec 5.2 (rfc-editor.org), quoted at each check:
//
//   "The window size specifies how many bytes the other party can send before it must wait for the
//    window to be adjusted."
//   "After receiving this message, the recipient MAY send the given number of bytes more than it was
//    previously allowed to send; the window size is incremented."
//   "Implementations MUST correctly handle window sizes of up to 2^32 - 1 bytes. The window MUST NOT
//    be increased above 2^32 - 1 bytes."
//   "The maximum amount of data allowed is determined by the maximum packet size for the channel,
//    and the current window size, whichever is smaller. The window size is decremented by the amount
//    of data sent."

#include "network_drivers/presentation/ssh/connection/ssh_flow_control.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static SshFlow open_flow(uint32_t local, uint32_t peer, uint32_t maxpkt)
{
    SshFlow f;
    pc_ssh_flow_init(&f, local, peer, maxpkt);
    return f;
}

// The two windows are independent and start where each side advertised.
static void test_init_sets_both_windows(void)
{
    SshFlow f = open_flow(32768, 16384, 4096);
    TEST_ASSERT_EQUAL_UINT32(16384, pc_ssh_flow_peer_window(&f));
    TEST_ASSERT_EQUAL_UINT32(32768, f.local_window);
    TEST_ASSERT_EQUAL_UINT32(32768, f.local_max);
    TEST_ASSERT_EQUAL_UINT32(4096, f.peer_max_pkt);
}

// "the maximum packet size ... and the current window size, whichever is smaller".
static void test_send_cap_is_the_smaller_of_window_and_max_packet(void)
{
    SshFlow f = open_flow(32768, 10000, 4096);
    TEST_ASSERT_EQUAL_UINT32(4096, pc_ssh_flow_send_cap(&f, 8000));  // max packet binds
    TEST_ASSERT_EQUAL_UINT32(1000, pc_ssh_flow_send_cap(&f, 1000));  // the ask binds

    SshFlow g = open_flow(32768, 500, 4096);
    TEST_ASSERT_EQUAL_UINT32(500, pc_ssh_flow_send_cap(&g, 8000)); // the window binds
}

// "The window size is decremented by the amount of data sent", and an empty window sends nothing.
static void test_send_decrements_the_window_to_empty(void)
{
    SshFlow f = open_flow(32768, 1000, 4096);
    pc_ssh_flow_send_take(&f, 400);
    TEST_ASSERT_EQUAL_UINT32(600, pc_ssh_flow_peer_window(&f));
    pc_ssh_flow_send_take(&f, 600);
    TEST_ASSERT_EQUAL_UINT32(0, pc_ssh_flow_peer_window(&f));
    TEST_ASSERT_EQUAL_UINT32(0, pc_ssh_flow_send_cap(&f, 1000)); // must wait for an adjust
    TEST_ASSERT_FALSE(pc_ssh_flow_send_allows(&f, 1));
}

// "the recipient MAY send the given number of bytes more than it was previously allowed to send;
// the window size is incremented."
static void test_window_adjust_increments(void)
{
    SshFlow f = open_flow(32768, 100, 4096);
    pc_ssh_flow_send_take(&f, 100);
    TEST_ASSERT_EQUAL_UINT32(0, pc_ssh_flow_peer_window(&f));
    pc_ssh_flow_peer_add(&f, 2048);
    TEST_ASSERT_EQUAL_UINT32(2048, pc_ssh_flow_peer_window(&f));
    TEST_ASSERT_TRUE(pc_ssh_flow_send_allows(&f, 2048));
}

// "The window MUST NOT be increased above 2^32 - 1 bytes." A peer that asks for more is the one
// violating the rule; our window stops at the ceiling rather than wrapping past it.
static void test_window_never_exceeds_the_rfc_ceiling(void)
{
    SshFlow f = open_flow(32768, 0xFFFFFF00u, 4096);
    pc_ssh_flow_peer_add(&f, 0x1000u); // would carry past 2^32-1
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, pc_ssh_flow_peer_window(&f));

    pc_ssh_flow_peer_add(&f, 0xFFFFFFFFu); // and stays there
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, pc_ssh_flow_peer_window(&f));
}

// "Implementations MUST correctly handle window sizes of up to 2^32 - 1 bytes."
static void test_handles_the_maximum_window(void)
{
    SshFlow f = open_flow(32768, 0xFFFFFFFFu, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, pc_ssh_flow_send_cap(&f, 0xFFFFFFFFu));
    pc_ssh_flow_send_take(&f, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_UINT32(0, pc_ssh_flow_peer_window(&f));
}

// Our own window drains as the peer sends, and refuses more than it advertised.
static void test_receive_take_drains_the_local_window(void)
{
    SshFlow f = open_flow(1000, 16384, 4096);
    TEST_ASSERT_TRUE(pc_ssh_flow_recv_take(&f, 600));
    TEST_ASSERT_EQUAL_UINT32(400, f.local_window);
    TEST_ASSERT_TRUE(pc_ssh_flow_recv_take(&f, 400));
    TEST_ASSERT_EQUAL_UINT32(0, f.local_window);
    TEST_ASSERT_FALSE(pc_ssh_flow_recv_take(&f, 1)); // past what we allowed
}

// The replenish is the adjust we owe the peer, and crediting it restores the advertised window.
static void test_replenish_and_credit_restore_the_advertised_window(void)
{
    SshFlow f = open_flow(1000, 16384, 4096);
    uint32_t add = 0;
    TEST_ASSERT_FALSE(pc_ssh_flow_replenish_due(&f, &add)); // nothing consumed yet

    TEST_ASSERT_TRUE(pc_ssh_flow_recv_take(&f, 900));
    TEST_ASSERT_TRUE(pc_ssh_flow_replenish_due(&f, &add));
    TEST_ASSERT_EQUAL_UINT32(900, add); // back up to what we advertised

    pc_ssh_flow_local_credit(&f, add);
    TEST_ASSERT_EQUAL_UINT32(1000, f.local_window);
    TEST_ASSERT_FALSE(pc_ssh_flow_replenish_due(&f, &add));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_both_windows);
    RUN_TEST(test_send_cap_is_the_smaller_of_window_and_max_packet);
    RUN_TEST(test_send_decrements_the_window_to_empty);
    RUN_TEST(test_window_adjust_increments);
    RUN_TEST(test_window_never_exceeds_the_rfc_ceiling);
    RUN_TEST(test_handles_the_maximum_window);
    RUN_TEST(test_receive_take_drains_the_local_window);
    RUN_TEST(test_replenish_and_credit_restore_the_advertised_window);
    return UNITY_END();
}
