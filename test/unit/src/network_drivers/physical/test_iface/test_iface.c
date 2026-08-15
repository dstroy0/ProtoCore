// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the layer 1 interface registry (network_drivers/physical/physical.h).
//
// No standard publishes the layout of an interface table, so every expectation here is a PROPERTY
// the container must hold whatever its implementation: registration is an identity (what goes in
// under an id comes back out under that id), a second registration of a live id is refused, a full
// table refuses rather than overwrites, and a row walk marks the empty rows.
//
// test_send_reaches_only_the_addressed_interface is the load-bearing case: every forwarded octet
// leaves through this send, and a registry that keys a row wrongly puts the frame on the wrong wire
// while still reporting success. The kinds themselves are the vocabulary of protocore_config.h,
// which separates a wired route (RFC 894 framing) from an IEEE 802 wireless one (RFC 1042).

#include "network_drivers/physical/physical.h"
#include <string.h>

#include <unity.h>

#define IFACES 8
#define FRAMES 4
#define FRAME_MAX 32

// What one registered interface was handed: the octets, the id, and the ctx.
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

static proto_bool add(uint8_t id, protocore_if_kind kind, protocore_if_send_fn fn, void *ctx)
{
    Physical.iface.id = id;
    Physical.iface.kind = kind;
    Physical.iface.send = fn;
    Physical.iface.ctx = ctx;
    Physical.iface_add(Physical.internal);
    return Physical.ok;
}

static uint8_t count(void)
{
    Physical.iface_count(Physical.internal);
    return Physical.u8;
}

static proto_bool present(uint8_t id)
{
    Physical.iface.id = id;
    Physical.iface_present(Physical.internal);
    return Physical.ok;
}

static protocore_if_kind kind_of(uint8_t id)
{
    Physical.iface.id = id;
    Physical.iface_kind(Physical.internal);
    return Physical.if_kind;
}

static int16_t at(uint8_t i)
{
    Physical.iface.i = i;
    Physical.iface_at(Physical.internal);
    return Physical.i16;
}

static proto_bool send(uint8_t id, const char *text, uint16_t n)
{
    Physical.iface.id = id;
    Physical.iface.data = (const uint8_t *)text;
    Physical.iface.len = n;
    Physical.iface_send(Physical.internal);
    return Physical.ok;
}

void setUp(void)
{
    memset(g_cap, 0, sizeof(g_cap));
    for (size_t i = 0; i < IFACES; i++)
    {
        g_cap[i].accept = PROTO_TRUE;
    }
    Physical.iface_reset(Physical.internal);
}

void tearDown(void)
{
    Physical.iface_reset(Physical.internal);
}

// An empty table answers every query with its "nothing here" value.
void test_empty_registry_reports_nothing(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, count());
    TEST_ASSERT_FALSE(present(0));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ANY, kind_of(0));
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_IF_NONE, at(0));
}

// What went in under an id comes back out under that id.
void test_add_then_lookup_is_the_identity(void)
{
    TEST_ASSERT_TRUE(add(3, PROTOCORE_IF_ETH, cap_send, NULL));
    TEST_ASSERT_EQUAL_UINT8(1, count());
    TEST_ASSERT_TRUE(present(3));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, kind_of(3));
}

// An id nobody registered has no kind, which is what PROTOCORE_IF_ANY means in the registry.
void test_unregistered_id_reads_as_absent(void)
{
    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_WIFI_STA, cap_send, NULL));
    TEST_ASSERT_FALSE(present(2));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ANY, kind_of(2));
}

// A live id cannot be re-registered, so a later caller cannot redirect an interface already in use.
void test_duplicate_id_is_refused(void)
{
    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_ETH, cap_send, NULL));
    TEST_ASSERT_FALSE(add(1, PROTOCORE_IF_BUS, cap_send, NULL));
    TEST_ASSERT_EQUAL_UINT8(1, count());
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, kind_of(1));
}

// An interface with no way to put octets on it is not an interface.
void test_null_send_is_refused(void)
{
    TEST_ASSERT_FALSE(add(1, PROTOCORE_IF_ETH, NULL, NULL));
    TEST_ASSERT_FALSE(present(1));
    TEST_ASSERT_EQUAL_UINT8(0, count());
}

// The table is fixed at PROTOCORE_PHY_MAX_IFACES rows and refuses past it rather than evicting.
void test_table_full_is_fail_closed(void)
{
    for (uint8_t i = 1; i <= PROTOCORE_PHY_MAX_IFACES; i++)
    {
        TEST_ASSERT_TRUE(add(i, PROTOCORE_IF_ETH, cap_send, NULL));
    }
    TEST_ASSERT_FALSE(add((uint8_t)(PROTOCORE_PHY_MAX_IFACES + 1), PROTOCORE_IF_ETH, cap_send, NULL));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_MAX_IFACES, count());
    TEST_ASSERT_FALSE(present((uint8_t)(PROTOCORE_PHY_MAX_IFACES + 1)));
}

// Reset returns the table to empty, and a freed row takes a new interface.
void test_reset_empties_the_registry(void)
{
    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_ETH, cap_send, NULL));
    TEST_ASSERT_TRUE(add(2, PROTOCORE_IF_BUS, cap_send, NULL));
    Physical.iface_reset(Physical.internal);
    TEST_ASSERT_EQUAL_UINT8(0, count());
    TEST_ASSERT_FALSE(present(1));
    TEST_ASSERT_FALSE(present(2));
    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_ETH, cap_send, NULL));
}

// The frame reaches the addressed row alone, carrying the id and ctx that row was registered with,
// so one shared callback can tell its interfaces apart.
void test_send_reaches_only_the_addressed_interface(void)
{
    int tag_a = 0;
    int tag_b = 0;

    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_ETH, cap_send, &tag_a));
    TEST_ASSERT_TRUE(add(2, PROTOCORE_IF_BUS, cap_send, &tag_b));

    TEST_ASSERT_TRUE(send(2, "xy", 2));

    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count);
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT8(2, g_cap[2].saw_id[0]);
    TEST_ASSERT_EQUAL_PTR(&tag_b, g_cap[2].saw_ctx[0]);
    TEST_ASSERT_EQUAL_UINT16(2, g_cap[2].len[0]);
    TEST_ASSERT_EQUAL_MEMORY("xy", g_cap[2].buf[0], 2);
}

// An id with no row has nowhere to send, and nothing is put on the wire.
void test_send_to_an_unregistered_id_fails(void)
{
    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_ETH, cap_send, NULL));
    TEST_ASSERT_FALSE(send(7, "x", 1));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[7].count);
}

// A refusal by the interface itself is reported, not swallowed.
void test_send_reports_a_refusing_interface(void)
{
    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_ETH, cap_send, NULL));
    g_cap[1].accept = PROTO_FALSE;
    TEST_ASSERT_FALSE(send(1, "x", 1));
}

// at() walks rows, not ids, so an empty or out-of-range row reads PROTOCORE_IF_NONE and a caller can
// iterate the whole table without knowing which ids are in it.
void test_at_walks_rows_and_marks_the_empty_ones(void)
{
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_IF_NONE, at(0));
    TEST_ASSERT_TRUE(add(9, PROTOCORE_IF_ETH, cap_send, NULL));
    TEST_ASSERT_EQUAL_INT16(9, at(0));
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_IF_NONE, at(1));
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_IF_NONE, at((uint8_t)PROTOCORE_PHY_MAX_IFACES));
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_IF_NONE, at(255));
}

// One table holds a wired port, a bridged bus and a softAP at once, each addressed the same way.
void test_mixed_kinds_coexist(void)
{
    TEST_ASSERT_TRUE(add(1, PROTOCORE_IF_ETH, cap_send, NULL));
    TEST_ASSERT_TRUE(add(2, PROTOCORE_IF_BUS, cap_send, NULL));
    TEST_ASSERT_TRUE(add(3, PROTOCORE_IF_WIFI_AP, cap_send, NULL));

    TEST_ASSERT_EQUAL_UINT8(3, count());
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_ETH, kind_of(1));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_BUS, kind_of(2));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IF_WIFI_AP, kind_of(3));

    TEST_ASSERT_TRUE(send(2, "bus", 3));
    TEST_ASSERT_EQUAL_MEMORY("bus", g_cap[2].buf[0], 3);
}
