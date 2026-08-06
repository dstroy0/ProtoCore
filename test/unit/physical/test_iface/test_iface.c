// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The layer 1 interface registry (Physical.iface). An interface is an id, a kind, and the callback
// that puts bytes on the wire; a device carries several, of mixed kind. Every forwarded byte leaves
// through send() here, so this covers the registry directly rather than through the forwarding
// plane that reads it.
//
// The env sizes PC_PHY_MAX_IFACES = 4.

#include "network_drivers/physical/physical.h"
#include <string.h>
#include <unity.h>

#define IFACES 8
#define FRAMES 4
#define FRAME_MAX 32

typedef struct
{
    uint8_t buf[FRAMES][FRAME_MAX];
    uint16_t len[FRAMES];
    uint8_t saw_id[FRAMES];
    void *saw_ctx[FRAMES];
    size_t count;
    proto_bool accept;
} Cap;
static Cap g_cap[IFACES];

static void cap_reset(void)
{
    memset(g_cap, 0, sizeof(g_cap));
    for (size_t i = 0; i < IFACES; i++)
    {
        g_cap[i].accept = PROTO_TRUE;
    }
}

// Records what the registry handed it: the id, the ctx, and the bytes.
static proto_bool cap_send(uint8_t id, const uint8_t *d, uint16_t n, void *ctx)
{
    if (id >= IFACES)
    {
        return PROTO_FALSE;
    }
    Cap *c = &g_cap[id];
    if (!c->accept)
    {
        return PROTO_FALSE;
    }
    if (c->count < FRAMES)
    {
        uint16_t k = (n < FRAME_MAX) ? n : FRAME_MAX;
        memcpy(c->buf[c->count], d, k);
        c->len[c->count] = k;
        c->saw_id[c->count] = id;
        c->saw_ctx[c->count] = ctx;
        c->count++;
    }
    return PROTO_TRUE;
}

void setUp()
{
    cap_reset();
    Physical.iface->reset();
}
void tearDown()
{
    Physical.iface->reset();
}

void test_add_registers_and_reports()
{
    TEST_ASSERT_EQUAL_UINT8(0, Physical.iface->count());
    TEST_ASSERT_TRUE(Physical.iface->add(3, PC_IF_ETH, cap_send, NULL));
    TEST_ASSERT_EQUAL_UINT8(1, Physical.iface->count());
    TEST_ASSERT_TRUE(Physical.iface->present(3));
    TEST_ASSERT_EQUAL_INT((int)PC_IF_ETH, (int)Physical.iface->kind(3));
}

void test_unregistered_id_reads_as_absent()
{
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_WIFI_STA, cap_send, NULL));
    TEST_ASSERT_FALSE(Physical.iface->present(2));
    // An id nobody registered has no kind, which is what PC_IF_ANY means here.
    TEST_ASSERT_EQUAL_INT((int)PC_IF_ANY, (int)Physical.iface->kind(2));
}

void test_duplicate_id_is_refused()
{
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_ETH, cap_send, NULL));
    TEST_ASSERT_FALSE(Physical.iface->add(1, PC_IF_BUS, cap_send, NULL));
    TEST_ASSERT_EQUAL_UINT8(1, Physical.iface->count());
    TEST_ASSERT_EQUAL_INT((int)PC_IF_ETH, (int)Physical.iface->kind(1)); // the first registration stands
}

void test_null_send_is_refused()
{
    TEST_ASSERT_FALSE(Physical.iface->add(1, PC_IF_ETH, NULL, NULL));
    TEST_ASSERT_FALSE(Physical.iface->present(1));
    TEST_ASSERT_EQUAL_UINT8(0, Physical.iface->count());
}

void test_table_full_is_fail_closed()
{
    for (uint8_t i = 1; i <= PC_PHY_MAX_IFACES; i++)
    {
        TEST_ASSERT_TRUE(Physical.iface->add(i, PC_IF_ETH, cap_send, NULL));
    }
    TEST_ASSERT_FALSE(Physical.iface->add((uint8_t)(PC_PHY_MAX_IFACES + 1), PC_IF_ETH, cap_send, NULL));
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_MAX_IFACES, Physical.iface->count());
}

void test_reset_empties_the_registry()
{
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_ETH, cap_send, NULL));
    TEST_ASSERT_TRUE(Physical.iface->add(2, PC_IF_BUS, cap_send, NULL));
    Physical.iface->reset();
    TEST_ASSERT_EQUAL_UINT8(0, Physical.iface->count());
    TEST_ASSERT_FALSE(Physical.iface->present(1));
    TEST_ASSERT_FALSE(Physical.iface->present(2));
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_ETH, cap_send, NULL)); // the row is reusable
}

// The registry hands the callback the id it was registered under and the ctx it was given, so a
// shared callback can tell its interfaces apart.
void test_send_carries_the_id_and_the_ctx()
{
    int tag_a = 0, tag_b = 0;
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_ETH, cap_send, &tag_a));
    TEST_ASSERT_TRUE(Physical.iface->add(2, PC_IF_BUS, cap_send, &tag_b));

    TEST_ASSERT_TRUE(Physical.iface->send(2, (const uint8_t *)"xy", 2));

    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count); // only the addressed interface is touched
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT8(2, g_cap[2].saw_id[0]);
    TEST_ASSERT_EQUAL_PTR(&tag_b, g_cap[2].saw_ctx[0]);
    TEST_ASSERT_EQUAL_UINT16(2, g_cap[2].len[0]);
    TEST_ASSERT_EQUAL_MEMORY("xy", g_cap[2].buf[0], 2);
}

void test_send_to_an_unregistered_id_fails()
{
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_ETH, cap_send, NULL));
    TEST_ASSERT_FALSE(Physical.iface->send(7, (const uint8_t *)"x", 1));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[7].count);
}

void test_send_reports_a_refusing_interface()
{
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_ETH, cap_send, NULL));
    g_cap[1].accept = PROTO_FALSE;
    TEST_ASSERT_FALSE(Physical.iface->send(1, (const uint8_t *)"x", 1));
}

// at() walks rows, not ids: an empty row reads PC_IF_NONE so a caller can iterate the whole table.
void test_at_walks_rows_and_marks_the_empty_ones()
{
    TEST_ASSERT_EQUAL_INT16(PC_IF_NONE, Physical.iface->at(0));
    TEST_ASSERT_TRUE(Physical.iface->add(9, PC_IF_ETH, cap_send, NULL));
    TEST_ASSERT_EQUAL_INT16(9, Physical.iface->at(0));
    TEST_ASSERT_EQUAL_INT16(PC_IF_NONE, Physical.iface->at(1));
    TEST_ASSERT_EQUAL_INT16(PC_IF_NONE, Physical.iface->at(PC_PHY_MAX_IFACES)); // past the end
    TEST_ASSERT_EQUAL_INT16(PC_IF_NONE, Physical.iface->at(255));
}

// The point of one registry: a wired port, a radio and a bridged bus coexist and are addressed the
// same way. This is what makes a uart bridged onto the network an interface like any other.
void test_mixed_kinds_coexist()
{
    TEST_ASSERT_TRUE(Physical.iface->add(1, PC_IF_ETH, cap_send, NULL));
    TEST_ASSERT_TRUE(Physical.iface->add(2, PC_IF_BUS, cap_send, NULL));
    TEST_ASSERT_TRUE(Physical.iface->add(3, PC_IF_WIFI_AP, cap_send, NULL));

    TEST_ASSERT_EQUAL_UINT8(3, Physical.iface->count());
    TEST_ASSERT_EQUAL_INT((int)PC_IF_ETH, (int)Physical.iface->kind(1));
    TEST_ASSERT_EQUAL_INT((int)PC_IF_BUS, (int)Physical.iface->kind(2));
    TEST_ASSERT_EQUAL_INT((int)PC_IF_WIFI_AP, (int)Physical.iface->kind(3));

    TEST_ASSERT_TRUE(Physical.iface->send(2, (const uint8_t *)"bus", 3));
    TEST_ASSERT_EQUAL_MEMORY("bus", g_cap[2].buf[0], 3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_registers_and_reports);
    RUN_TEST(test_unregistered_id_reads_as_absent);
    RUN_TEST(test_duplicate_id_is_refused);
    RUN_TEST(test_null_send_is_refused);
    RUN_TEST(test_table_full_is_fail_closed);
    RUN_TEST(test_reset_empties_the_registry);
    RUN_TEST(test_send_carries_the_id_and_the_ctx);
    RUN_TEST(test_send_to_an_unregistered_id_fails);
    RUN_TEST(test_send_reports_a_refusing_interface);
    RUN_TEST(test_at_walks_rows_and_marks_the_empty_ones);
    RUN_TEST(test_mixed_kinds_coexist);
    return UNITY_END();
}
