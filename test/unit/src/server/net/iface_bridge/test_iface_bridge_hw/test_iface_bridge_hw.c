// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the interface-bridge glue's bind table (server/net/iface_bridge/iface_bridge_hw.h).
//
// No standard governs a device-server bind table, so every expectation comes from two places: the
// contract iface_bridge_hw.h publishes for IfaceBridgeHw.publish - "false if the target is null,
// the rule table is full, or the bind table is full" - and the bounds protocore_config.h states
// (PROTOCORE_BRIDGE_MAX_RULES, PROTOCORE_BRIDGE_STREAM_CHUNK). The rest are properties that hold
// whatever the implementation: a refused publish consumes no slot, reset returns the table to
// empty, and a slot resolves to the rule it was published with.
//
// The module source is included rather than linked so rule_for_slot - file-static, and the lookup
// every accept and poll callback runs through - is testable directly. The env therefore must not
// also build iface_bridge_hw.c.
//
// test_publish_walks_the_pure_table is the load-bearing case: publish is the seam between the pure
// rule table (iface_bridge.c) and the glue's bind table, and a bind pointing at the wrong rule
// sends a socket's bytes to the wrong bus.

#include "server/net/iface_bridge/iface_bridge_hw/iface_bridge_hw.c"

#include <unity.h>

static uint8_t *work; // the glue's own span, the borrow every entry runs out of

static BridgeTarget uart_target(uint32_t baud)
{
    BridgeTarget t;
    mem.set(&t, 0, sizeof(t));
    t.bus = BRIDGE_BUS_UART;
    t.mode = BRIDGE_MODE_STREAM;
    t.rate = baud;
    return t;
}

void setUp(void)
{
    work = protocore_iface_bridge_hw_span();
    IfaceBridgeHw.reset(work);
    IfaceBridge.clear(protocore_iface_bridge_span());
}

void tearDown(void)
{
}

// The accessor hands out the persistent span, and it covers the context the offsets carve.
void test_the_borrow_covers_the_context(void)
{
    TEST_ASSERT_NOT_NULL(work);
    TEST_ASSERT_EQUAL_PTR(work, protocore_iface_bridge_hw_span()); // taken once, same bytes after
    TEST_ASSERT_TRUE(sizeof(BridgeGlueCtx) <= PROTOCORE_IFACE_BRIDGE_HW_BORROW);
    TEST_ASSERT_EQUAL_PTR(work, (uint8_t *)IFACE_BRIDGE_HW_CTX(work));
}

// publish stores the rule in the pure table and binds the listener to the rule it just stored.
void test_publish_walks_the_pure_table(void)
{
    BridgeTarget t = uart_target(115200);
    IfaceBridgeHwV.publish_args.listener_id = 2;
    IfaceBridgeHwV.publish_args.port = 4001;
    IfaceBridgeHwV.publish_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeHwV.publish_args.target = &t;
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_TRUE(IfaceBridgeHwV.ok);

    // the pure table holds it
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(1, IfaceBridgeV.u8);
    IfaceBridgeV.find_args.port = 4001;
    IfaceBridgeV.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    TEST_ASSERT_NOT_NULL(IfaceBridgeV.rule);

    // and the bind points at that same rule
    TEST_ASSERT_TRUE(IFACE_BRIDGE_HW_CTX(work)->binds[0].active);
    TEST_ASSERT_EQUAL_UINT8(2, IFACE_BRIDGE_HW_CTX(work)->binds[0].listener_id);
    TEST_ASSERT_EQUAL_PTR(IfaceBridgeV.rule, IFACE_BRIDGE_HW_CTX(work)->binds[0].rule);
    TEST_ASSERT_EQUAL_UINT32(115200u, IFACE_BRIDGE_HW_CTX(work)->binds[0].rule->target.rate);
}

// Header: "false if the target is null". The refusal takes no slot in either table.
void test_a_null_target_is_refused_and_takes_no_slot(void)
{
    IfaceBridgeHwV.publish_args.listener_id = 1;
    IfaceBridgeHwV.publish_args.port = 4001;
    IfaceBridgeHwV.publish_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeHwV.publish_args.target = NULL;
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_FALSE(IfaceBridgeHwV.ok);

    TEST_ASSERT_FALSE(IFACE_BRIDGE_HW_CTX(work)->binds[0].active);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridgeV.u8);
}

// The pure table refuses a duplicate port+proto, and publish reports that refusal rather than
// binding a listener to a rule that was never stored.
void test_a_duplicate_port_is_refused(void)
{
    BridgeTarget t = uart_target(9600);
    IfaceBridgeHwV.publish_args.listener_id = 1;
    IfaceBridgeHwV.publish_args.port = 4001;
    IfaceBridgeHwV.publish_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeHwV.publish_args.target = &t;
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_TRUE(IfaceBridgeHwV.ok);

    IfaceBridgeHwV.publish_args.listener_id = 2; // same port + proto
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_FALSE(IfaceBridgeHwV.ok);

    TEST_ASSERT_FALSE(IFACE_BRIDGE_HW_CTX(work)->binds[1].active); // no second bind
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(1, IfaceBridgeV.u8);
}

// The same port on the other protocol is a different rule, so it binds.
void test_the_protocol_is_part_of_the_key(void)
{
    BridgeTarget t = uart_target(9600);
    IfaceBridgeHwV.publish_args.listener_id = 1;
    IfaceBridgeHwV.publish_args.port = 4001;
    IfaceBridgeHwV.publish_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeHwV.publish_args.target = &t;
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_TRUE(IfaceBridgeHwV.ok);

    IfaceBridgeHwV.publish_args.listener_id = 2;
    IfaceBridgeHwV.publish_args.proto = BRIDGE_PROTO_UDP;
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_TRUE(IfaceBridgeHwV.ok);

    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(2, IfaceBridgeV.u8);
    TEST_ASSERT_TRUE(IFACE_BRIDGE_HW_CTX(work)->binds[1].active);
}

// Header: the bind table is bounded by PROTOCORE_BRIDGE_MAX_RULES, and the publish past it is
// refused rather than overwriting a live bind.
void test_the_bind_table_fills_at_its_bound(void)
{
    BridgeTarget t = uart_target(9600);
    for (int i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        IfaceBridgeHwV.publish_args.listener_id = (uint8_t)i;
        IfaceBridgeHwV.publish_args.port = (uint16_t)(5000 + i);
        IfaceBridgeHwV.publish_args.proto = BRIDGE_PROTO_TCP;
        IfaceBridgeHwV.publish_args.target = &t;
        IfaceBridgeHw.publish(work);
        TEST_ASSERT_TRUE_MESSAGE(IfaceBridgeHwV.ok, "a publish inside the bound was refused");
    }
    IfaceBridgeHwV.publish_args.listener_id = 99;
    IfaceBridgeHwV.publish_args.port = 6000;
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_FALSE(IfaceBridgeHwV.ok); // one past the bound

    for (int i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        TEST_ASSERT_TRUE(IFACE_BRIDGE_HW_CTX(work)->binds[i].active); // none disturbed
    }
}

// Header: reset clears "all listener bindings and rules (start from empty)" - both tables, so a
// re-provision cannot inherit a rule whose listener is gone.
void test_reset_clears_every_bind_and_every_rule(void)
{
    BridgeTarget t = uart_target(9600);
    IfaceBridgeHwV.publish_args.listener_id = 1;
    IfaceBridgeHwV.publish_args.port = 4001;
    IfaceBridgeHwV.publish_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeHwV.publish_args.target = &t;
    IfaceBridgeHw.publish(work);
    TEST_ASSERT_TRUE(IFACE_BRIDGE_HW_CTX(work)->binds[0].active);

    IfaceBridgeHw.reset(work);

    for (int i = 0; i < PROTOCORE_BRIDGE_MAX_RULES; i++)
    {
        TEST_ASSERT_FALSE(IFACE_BRIDGE_HW_CTX(work)->binds[i].active);
    }
    IfaceBridge.count(protocore_iface_bridge_span()); // "and rules": the pure table goes too
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridgeV.u8);
}

// The stream chunk lives in the borrow, so it is covered by the same assert the context is, and
// carries the size protocore_config.h states.
void test_the_stream_chunk_is_in_the_borrow(void)
{
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_STREAM_CHUNK, sizeof(IFACE_BRIDGE_HW_CTX(work)->stream));
    uint8_t *chunk = IFACE_BRIDGE_HW_CTX(work)->stream;
    TEST_ASSERT_TRUE(chunk >= work);
    TEST_ASSERT_TRUE(chunk + PROTOCORE_BRIDGE_STREAM_CHUNK <= work + PROTOCORE_IFACE_BRIDGE_HW_BORROW);
}
