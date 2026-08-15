// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// network/network.c (RFC 4253 sec 4): the binding between an SSH slot and the byte stream under it.
//
// "SSH works over any 8-bit clean, binary-transparent transport." Nothing here is one of the three
// components of RFC 4251 sec 1 - this is the stream they run on. The two roles get their streams
// from different places, so a handle alone does not say which pool it indexes, and every outbound
// path has to ask before it writes.

#include "network_drivers/presentation/ssh/network/network.h"
#include "network_drivers/presentation/ssh/ssh.h"
#include <stdint.h>

#include <unity.h>

// The SSH slot-to-stream binding, reached through its namespace: set the members a call takes,
// invoke it, read the outcome off the same handle.
static uint8_t net_slot_free(void)
{
    SshNetwork.slot_free(SshNetwork.internal);
    return SshNetwork.u8;
}

static int net_claim(uint8_t ssh_slot, int handle, SshStreamKind kind)
{
    SshNetwork.ssh_slot = ssh_slot;
    SshNetwork.handle = handle;
    SshNetwork.stream.kind = kind;
    SshNetwork.claim(SshNetwork.internal);
    return SshNetwork.i32;
}

static void net_release(uint8_t ssh_slot)
{
    SshNetwork.ssh_slot = ssh_slot;
    SshNetwork.release(SshNetwork.internal);
}

static proto_bool net_owns(uint8_t ssh_slot, uint8_t conn_slot)
{
    SshNetwork.ssh_slot = ssh_slot;
    SshNetwork.conn_slot = conn_slot;
    SshNetwork.owns(SshNetwork.internal);
    return SshNetwork.ok;
}

static int net_write_msg(uint8_t ssh_slot, const uint8_t *msg, size_t len)
{
    SshNetwork.ssh_slot = ssh_slot;
    SshNetwork.msg.payload = msg;
    SshNetwork.msg.len = len;
    SshNetwork.write_msg(SshNetwork.internal);
    return SshNetwork.i32;
}

static int net_write_msg_at(uint8_t ssh_slot, size_t plen)
{
    SshNetwork.ssh_slot = ssh_slot;
    SshNetwork.msg.plen = plen;
    SshNetwork.write_msg_at(SshNetwork.internal);
    return SshNetwork.i32;
}

static uint8_t *net_payload_region(uint8_t ssh_slot, size_t *cap)
{
    SshNetwork.ssh_slot = ssh_slot;
    SshNetwork.payload_region(SshNetwork.internal);
    if (cap)
    {
        *cap = SshNetwork.read_args.cap;
    }
    return SshNetwork.region;
}


void setUp(void)
{
    for (uint8_t i = 0; i < MAX_SSH_CONNS; i++)
    {
        net_release(i);
    }
}
void tearDown(void)
{
    for (uint8_t i = 0; i < MAX_SSH_CONNS; i++)
    {
        net_release(i);
    }
}

// ---------------------------------------------------------------------------
// claiming a slot for a stream
// ---------------------------------------------------------------------------

// A free pool hands out the lowest slot, and keeps handing out the next one as they are taken.
static void test_free_slots_are_handed_out_lowest_first(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, net_slot_free());
    TEST_ASSERT_EQUAL_INT(0, net_claim(0, 3, SSH_STREAM_ACCEPTED));
    if (MAX_SSH_CONNS > 1)
    {
        TEST_ASSERT_EQUAL_UINT8(1u, net_slot_free());
    }
}

// A full pool has no slot to give, and says so rather than returning one in use.
static void test_a_full_pool_reports_no_free_slot(void)
{
    for (uint8_t i = 0; i < MAX_SSH_CONNS; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, net_claim(i, (int)(i + 1), SSH_STREAM_ACCEPTED));
    }
    TEST_ASSERT_EQUAL_UINT8(0xFFu, net_slot_free());
}

// Releasing puts the slot back.
static void test_release_returns_the_slot_to_the_pool(void)
{
    for (uint8_t i = 0; i < MAX_SSH_CONNS; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, net_claim(i, (int)(i + 1), SSH_STREAM_ACCEPTED));
    }
    TEST_ASSERT_EQUAL_UINT8(0xFFu, net_slot_free());
    net_release(0);
    TEST_ASSERT_EQUAL_UINT8(0u, net_slot_free());
}

// A slot already bound is not re-bound underneath the connection using it.
static void test_a_bound_slot_cannot_be_claimed_again(void)
{
    TEST_ASSERT_EQUAL_INT(0, net_claim(0, 3, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_EQUAL_INT(-1, net_claim(0, 4, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_TRUE(net_owns(0, 3)); // still the first stream
}

// 0xFF is the free marker, so a handle that would collide with it cannot be bound.
static void test_the_free_marker_is_not_a_usable_handle(void)
{
    TEST_ASSERT_EQUAL_INT(-1, net_claim(0, 0xFF, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_EQUAL_INT(-1, net_claim(0, 0x100, SSH_STREAM_ACCEPTED));
}

// A negative handle is not a stream.
static void test_a_negative_handle_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(-1, net_claim(0, -1, SSH_STREAM_ACCEPTED));
}

// A slot outside the pool binds nothing.
static void test_slot_past_the_pool_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(-1, net_claim(MAX_SSH_CONNS, 3, SSH_STREAM_ACCEPTED));
    net_release(MAX_SSH_CONNS); // inert
}

// ---------------------------------------------------------------------------
// which stream a slot is bound to
// ---------------------------------------------------------------------------

static void test_owns_answers_only_for_the_bound_stream(void)
{
    TEST_ASSERT_EQUAL_INT(0, net_claim(0, 7, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_TRUE(net_owns(0, 7));
    TEST_ASSERT_FALSE(net_owns(0, 8));
}

static void test_owns_is_false_for_an_unbound_slot(void)
{
    TEST_ASSERT_FALSE(net_owns(0, 7));
}

static void test_owns_is_false_past_the_pool(void)
{
    TEST_ASSERT_FALSE(net_owns(MAX_SSH_CONNS, 7));
}

// Bindings are per slot: two connections on two streams do not answer for each other.
static void test_bindings_are_per_slot(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    TEST_ASSERT_EQUAL_INT(0, net_claim(0, 5, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_EQUAL_INT(0, net_claim(1, 6, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_TRUE(net_owns(0, 5));
    TEST_ASSERT_TRUE(net_owns(1, 6));
    TEST_ASSERT_FALSE(net_owns(0, 6));
    TEST_ASSERT_FALSE(net_owns(1, 5));
}

// The same handle number means different streams in the two pools, so releasing one binding does
// not disturb another slot's.
static void test_release_disturbs_only_its_own_slot(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    TEST_ASSERT_EQUAL_INT(0, net_claim(0, 5, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_EQUAL_INT(0, net_claim(1, 6, SSH_STREAM_ACCEPTED));
    net_release(0);
    TEST_ASSERT_FALSE(net_owns(0, 5));
    TEST_ASSERT_TRUE(net_owns(1, 6));
}

// A released slot can be bound again, which is what makes the pool a pool.
static void test_a_released_slot_can_be_rebound(void)
{
    TEST_ASSERT_EQUAL_INT(0, net_claim(0, 5, SSH_STREAM_ACCEPTED));
    net_release(0);
    TEST_ASSERT_EQUAL_INT(0, net_claim(0, 9, SSH_STREAM_ACCEPTED));
    TEST_ASSERT_TRUE(net_owns(0, 9));
    TEST_ASSERT_FALSE(net_owns(0, 5));
}

// ---------------------------------------------------------------------------
// nothing is written to a stream that is not there
// ---------------------------------------------------------------------------
// Every outbound path asks first: a slot with no stream bound has nowhere to put bytes, and a
// packet counted but never sent would desynchronize the peer's MAC for the rest of the connection
// (sec 6.4).

static void test_no_payload_region_without_a_stream(void)
{
    size_t cap = 0xFFFFu;
    TEST_ASSERT_NULL(net_payload_region(0, &cap));
}

static void test_no_write_without_a_stream(void)
{
    const uint8_t msg[] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT(-1, net_write_msg(0, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(-1, net_write_msg_at(0, sizeof(msg)));
}

static void test_no_write_past_the_pool(void)
{
    const uint8_t msg[] = {1, 2, 3, 4, 5};
    size_t cap = 0;
    TEST_ASSERT_NULL(net_payload_region(MAX_SSH_CONNS, &cap));
    TEST_ASSERT_EQUAL_INT(-1, net_write_msg(MAX_SSH_CONNS, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(-1, net_write_msg_at(MAX_SSH_CONNS, sizeof(msg)));
}

// A null capacity pointer is refused rather than written through.
static void test_payload_region_requires_somewhere_to_report_the_capacity(void)
{
    TEST_ASSERT_NULL(net_payload_region(0, NULL));
}

// ---------------------------------------------------------------------------
// the slot's own storage
// ---------------------------------------------------------------------------

// The wire a slot frames into is its own span, so two slots never share one.
static void test_each_slot_has_its_own_storage(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    uint8_t *a = ssh_conn_slot(0);
    uint8_t *b = ssh_conn_slot(1);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);
}

static void test_storage_past_the_pool_is_null(void)
{
    TEST_ASSERT_NULL(ssh_conn_slot(MAX_SSH_CONNS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_free_slots_are_handed_out_lowest_first);
    RUN_TEST(test_a_full_pool_reports_no_free_slot);
    RUN_TEST(test_release_returns_the_slot_to_the_pool);
    RUN_TEST(test_a_bound_slot_cannot_be_claimed_again);
    RUN_TEST(test_the_free_marker_is_not_a_usable_handle);
    RUN_TEST(test_a_negative_handle_is_refused);
    RUN_TEST(test_slot_past_the_pool_is_refused);
    RUN_TEST(test_owns_answers_only_for_the_bound_stream);
    RUN_TEST(test_owns_is_false_for_an_unbound_slot);
    RUN_TEST(test_owns_is_false_past_the_pool);
    RUN_TEST(test_bindings_are_per_slot);
    RUN_TEST(test_release_disturbs_only_its_own_slot);
    RUN_TEST(test_a_released_slot_can_be_rebound);
    RUN_TEST(test_no_payload_region_without_a_stream);
    RUN_TEST(test_no_write_without_a_stream);
    RUN_TEST(test_no_write_past_the_pool);
    RUN_TEST(test_payload_region_requires_somewhere_to_report_the_capacity);
    RUN_TEST(test_each_slot_has_its_own_storage);
    RUN_TEST(test_storage_past_the_pool_is_null);
    return UNITY_END();
}
