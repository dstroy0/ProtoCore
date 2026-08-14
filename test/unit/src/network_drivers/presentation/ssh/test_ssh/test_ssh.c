// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ssh.c: the per-slot storage handed out by ssh_conn_slot(), and the region map
// common.h names over it (RFC 4253 sec 6.1 sizing).

#include "network_drivers/presentation/ssh/ssh.h"
#include <stdint.h>

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// ssh_conn_slot: the two branches
// ---------------------------------------------------------------------------

static void test_every_slot_in_range_has_storage(void)
{
    for (uint8_t i = 0; i < MAX_SSH_CONNS; i++)
    {
        TEST_ASSERT_NOT_NULL(ssh_conn_slot(i));
    }
}

// Out of range is null rather than a wild pointer, so a bad slot index fails closed.
static void test_slot_past_the_pool_is_null(void)
{
    TEST_ASSERT_NULL(ssh_conn_slot(MAX_SSH_CONNS));
    TEST_ASSERT_NULL(ssh_conn_slot((uint8_t)(MAX_SSH_CONNS + 1u)));
    TEST_ASSERT_NULL(ssh_conn_slot(0xFFu));
}

// The same slot always answers with the same base: the storage is fixed at build time, not handed
// out from a pool that could move it between calls.
static void test_the_same_slot_answers_the_same_base(void)
{
    for (uint8_t i = 0; i < MAX_SSH_CONNS; i++)
    {
        TEST_ASSERT_EQUAL_PTR(ssh_conn_slot(i), ssh_conn_slot(i));
    }
}

// Distinct slots are distinct spans, a full borrow apart, so one connection cannot read or write
// another's bytes by running off the end of its own.
static void test_slots_are_distinct_and_one_borrow_apart(void)
{
    if (MAX_SSH_CONNS < 2u)
    {
        TEST_IGNORE_MESSAGE("pool holds one slot; nothing to separate");
    }
    for (uint8_t i = 1; i < MAX_SSH_CONNS; i++)
    {
        const uint8_t *prev = ssh_conn_slot((uint8_t)(i - 1u));
        const uint8_t *cur = ssh_conn_slot(i);
        TEST_ASSERT_NOT_EQUAL(prev, cur);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)SSH_SLOT_BORROW, (uint32_t)(size_t)(cur - prev));
    }
}

// Writing a slot end to end touches nothing outside it.
static void test_writing_a_whole_slot_leaves_its_neighbour_alone(void)
{
    if (MAX_SSH_CONNS < 2u)
    {
        TEST_IGNORE_MESSAGE("pool holds one slot; nothing to separate");
    }
    uint8_t *a = ssh_conn_slot(0);
    uint8_t *b = ssh_conn_slot(1);

    for (size_t k = 0; k < SSH_SLOT_BORROW; k++)
    {
        b[k] = 0xA5u;
    }
    for (size_t k = 0; k < SSH_SLOT_BORROW; k++)
    {
        a[k] = 0x5Au;
    }
    for (size_t k = 0; k < SSH_SLOT_BORROW; k++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xA5u, b[k]);
    }
}

// ---------------------------------------------------------------------------
// The slot map: every region inside the borrow
// ---------------------------------------------------------------------------

static void test_every_region_ends_within_the_borrow(void)
{
    TEST_ASSERT_TRUE(SSH_OFF_WIRE + SSH_WIRE_CAP <= SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_V_C < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_V_S < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_IDENT < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_I_C < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_I_S < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_KEXINIT < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_CPUB < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_SESSION_ID < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_EPOCH_0 < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_EPOCH_1 < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_ECDH_SK < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_ECDH_PK < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_MAC_WORK < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_CRYPTO_WORK < SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_RX_READ + RX_BUF_SIZE <= SSH_SLOT_BORROW);
    TEST_ASSERT_TRUE(SSH_OFF_RX_ASM + SSH_RFC_MAX_PAYLOAD <= SSH_SLOT_BORROW);
}

// The map is laid out in one direction, each offset starting where the one before it ended, so no
// two can alias.
static void test_offsets_are_laid_out_in_ascending_order(void)
{
    // wire, then session: what outlives a single key exchange.
    TEST_ASSERT_TRUE(SSH_OFF_WIRE < SSH_OFF_V_C);
    TEST_ASSERT_TRUE(SSH_OFF_V_C < SSH_OFF_V_S);
    TEST_ASSERT_TRUE(SSH_OFF_V_S < SSH_OFF_SESSION_ID);
    TEST_ASSERT_TRUE(SSH_OFF_SESSION_ID < SSH_OFF_EPOCH_0);
    TEST_ASSERT_TRUE(SSH_OFF_EPOCH_0 < SSH_OFF_EPOCH_1);
    // exchange: live only from KEXINIT to NEWKEYS.
    TEST_ASSERT_TRUE(SSH_OFF_EPOCH_1 < SSH_OFF_IDENT);
    TEST_ASSERT_TRUE(SSH_OFF_IDENT < SSH_OFF_I_C);
    TEST_ASSERT_TRUE(SSH_OFF_I_C < SSH_OFF_I_S);
    TEST_ASSERT_TRUE(SSH_OFF_I_S < SSH_OFF_KEXINIT);
    TEST_ASSERT_TRUE(SSH_OFF_KEXINIT < SSH_OFF_CPUB);
    TEST_ASSERT_TRUE(SSH_OFF_CPUB < SSH_OFF_DH_Y);
    TEST_ASSERT_TRUE(SSH_OFF_DH_Y < SSH_OFF_DH_F);
    TEST_ASSERT_TRUE(SSH_OFF_DH_F < SSH_OFF_DH_K);
    TEST_ASSERT_TRUE(SSH_OFF_DH_K < SSH_OFF_ECDH_SK);
    TEST_ASSERT_TRUE(SSH_OFF_ECDH_SK < SSH_OFF_ECDH_PK);
    TEST_ASSERT_TRUE(SSH_OFF_ECDH_PK < SSH_OFF_CRYPTO_WORK);
    // packet, then rx.
    TEST_ASSERT_TRUE(SSH_OFF_CRYPTO_WORK < SSH_OFF_MAC_WORK);
    TEST_ASSERT_TRUE(SSH_OFF_MAC_WORK < SSH_OFF_RX_READ);
    TEST_ASSERT_TRUE(SSH_OFF_RX_READ < SSH_OFF_RX_ASM);
}

// The five regions partition the slot: each begins where the last ended, and together they are the
// whole span. A region that grew without its neighbours moving would break one of these.
static void test_regions_partition_the_slot(void)
{
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SSH_OFF_V_C, (uint32_t)(SSH_OFF_WIRE + SSH_WIRE_CAP));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SSH_OFF_IDENT, (uint32_t)SSH_SESSION_END);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SSH_OFF_MAC_WORK, (uint32_t)SSH_EXCHANGE_END);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SSH_OFF_RX_READ, (uint32_t)SSH_PACKET_END);

    const size_t sum = (size_t)SSH_WIRE_CAP + SSH_SESSION_SIZE + SSH_EXCHANGE_SIZE + SSH_PACKET_SIZE + SSH_RX_SIZE;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SSH_SLOT_BORROW, (uint32_t)sum);
}

// The regions that only live for one exchange or one message sit together, so what a per-worker
// copy would cost is one span rather than a scatter.
static void test_the_transient_regions_are_contiguous(void)
{
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(SSH_EXCHANGE_SIZE + SSH_PACKET_SIZE),
                             (uint32_t)(SSH_PACKET_END - SSH_OFF_IDENT));
}

// The wire is the first region. An overrun of the buffer bytes arrive in therefore runs forward
// through the rest of the wire, not backwards into the key epochs.
static void test_the_wire_is_first_and_key_material_is_behind_it(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)SSH_OFF_WIRE);
    TEST_ASSERT_TRUE(SSH_OFF_WIRE + SSH_WIRE_CAP <= SSH_OFF_EPOCH_0);
    TEST_ASSERT_TRUE(SSH_OFF_WIRE + SSH_WIRE_CAP <= SSH_OFF_ECDH_SK);
}

// The two key epochs are the same size and adjacent, so selecting one is an index times a stride
// rather than a table.
static void test_the_two_key_epochs_are_one_stride_apart(void)
{
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SSH_EPOCH_STRIDE, (uint32_t)(SSH_OFF_EPOCH_1 - SSH_OFF_EPOCH_0));
    TEST_ASSERT_TRUE(SSH_OFF_EPOCH_1 + SSH_EPOCH_STRIDE <= SSH_SLOT_BORROW);
}

// RFC 4253 sec 6.1: "All implementations MUST be able to process packets with an uncompressed
// payload length of 32768 bytes or less". The reassembly region is sized to hold one.
static void test_sec6_1_reassembly_region_holds_a_full_payload(void)
{
    TEST_ASSERT_TRUE(SSH_RFC_MAX_PAYLOAD >= 32768u);
    TEST_ASSERT_TRUE(SSH_OFF_RX_ASM + SSH_RFC_MAX_PAYLOAD <= SSH_SLOT_BORROW);
}

// The wire holds two whole packets, so a reply framed while one is still draining has somewhere to
// go, and it is a power of two so the index arithmetic is a mask.
static void test_the_wire_holds_two_packets_and_is_a_power_of_two(void)
{
    TEST_ASSERT_TRUE(SSH_WIRE_CAP >= 2u * SSH_RFC_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)(SSH_WIRE_CAP & (SSH_WIRE_CAP - 1u)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_slot_in_range_has_storage);
    RUN_TEST(test_slot_past_the_pool_is_null);
    RUN_TEST(test_the_same_slot_answers_the_same_base);
    RUN_TEST(test_slots_are_distinct_and_one_borrow_apart);
    RUN_TEST(test_writing_a_whole_slot_leaves_its_neighbour_alone);
    RUN_TEST(test_every_region_ends_within_the_borrow);
    RUN_TEST(test_offsets_are_laid_out_in_ascending_order);
    RUN_TEST(test_regions_partition_the_slot);
    RUN_TEST(test_the_transient_regions_are_contiguous);
    RUN_TEST(test_the_wire_is_first_and_key_material_is_behind_it);
    RUN_TEST(test_the_two_key_epochs_are_one_stride_apart);
    RUN_TEST(test_sec6_1_reassembly_region_holds_a_full_payload);
    RUN_TEST(test_the_wire_holds_two_packets_and_is_a_power_of_two);
    return UNITY_END();
}
