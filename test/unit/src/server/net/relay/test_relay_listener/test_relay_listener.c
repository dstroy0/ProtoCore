// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the relay / DNAT listener's bind table (server/net/relay/relay_listener.h).
//
// No standard governs a DNAT bind table, so every expectation here comes from exactly two places:
// the contract relay_listener.h publishes for RelayListener.publish - "false if the origin host is
// null/too long or the bind table is full (PROTOCORE_RELAY_MAX_PUBLISH)" - and the bounds
// protocore_config.h states (PROTOCORE_RELAY_MAX_PUBLISH, PROTOCORE_RELAY_MAX_CONNS,
// PROTOCORE_RELAY_HOST_MAX). Everything else asserted below is a property that holds whatever the
// implementation: the table never exceeds its bound, a refused publish consumes no slot, and reset
// returns the table to empty.
//
// The module source is included rather than linked so the bind lookup and the bridge allocator -
// both file-static, and both the part a caller cannot reach - are testable directly. The env
// therefore must not also build relay_listener.c.
//
// test_the_borrow_covers_the_context is the load-bearing case: every entry and every helper reads
// its state through RELAY_LISTENER_CTX(work), so a PROTOCORE_RELAY_LISTENER_BORROW short of
// sizeof(RelayListenerCtx) would corrupt whatever the arena placed after it. The static_assert in
// relay_listener.c proves the size at compile time; this proves the span is actually handed out.

#include "server/net/relay/relay_listener.c"

#include <unity.h>

static uint8_t *work; // the module's own span, the borrow every entry runs out of

void setUp(void)
{
    work = protocore_relay_listener_span();
    RelayListener.reset(work);
}

void tearDown(void)
{
}

// The accessor hands out the persistent span, and it covers the context the offsets carve.
void test_the_borrow_covers_the_context(void)
{
    TEST_ASSERT_NOT_NULL(work);
    TEST_ASSERT_EQUAL_PTR(work, protocore_relay_listener_span()); // taken once, same bytes after
    TEST_ASSERT_TRUE(sizeof(RelayListenerCtx) <= PROTOCORE_RELAY_LISTENER_BORROW);
    TEST_ASSERT_EQUAL_PTR(work, (uint8_t *)RELAY_LISTENER_CTX(work));
}

// Header: publish binds a listener id to an origin and returns true.
void test_publish_binds_a_listener_to_an_origin(void)
{
    RelayListener.publish_args.listener_id = 3;
    RelayListener.publish_args.origin_host = "192.168.1.60";
    RelayListener.publish_args.origin_port = 80;
    RelayListener.publish(work);
    TEST_ASSERT_TRUE(RelayListener.ok);

    RelayBind *b = bind_by_listener(work, 3);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(b->active);
    TEST_ASSERT_EQUAL_UINT8(3, b->listener_id);
    TEST_ASSERT_EQUAL_UINT16(80, b->port);
    TEST_ASSERT_EQUAL_STRING("192.168.1.60", b->host);
}

// A listener id with no bind has no entry: the lookup fails closed rather than returning slot 0.
void test_lookup_of_an_unpublished_listener_finds_nothing(void)
{
    TEST_ASSERT_NULL(bind_by_listener(work, 7));
    RelayListener.publish_args.listener_id = 3;
    RelayListener.publish_args.origin_host = "10.0.0.1";
    RelayListener.publish_args.origin_port = 8080;
    RelayListener.publish(work);
    TEST_ASSERT_NOT_NULL(bind_by_listener(work, 3));
    TEST_ASSERT_NULL(bind_by_listener(work, 4)); // a neighbouring id is still unbound
}

// Header: "false if the origin host is null/too long". Both refusals, and neither takes a slot.
void test_a_bad_origin_host_is_refused_and_takes_no_slot(void)
{
    char toolong[PROTOCORE_RELAY_HOST_MAX + 8];
    for (size_t i = 0; i < sizeof(toolong) - 1; i++)
    {
        toolong[i] = 'a';
    }
    toolong[sizeof(toolong) - 1] = '\0';

    RelayListener.publish_args.listener_id = 1;
    RelayListener.publish_args.origin_host = NULL;
    RelayListener.publish_args.origin_port = 80;
    RelayListener.publish(work);
    TEST_ASSERT_FALSE(RelayListener.ok);

    RelayListener.publish_args.listener_id = 1;
    RelayListener.publish_args.origin_host = "";
    RelayListener.publish(work);
    TEST_ASSERT_FALSE(RelayListener.ok); // an empty host names no origin

    RelayListener.publish_args.listener_id = 1;
    RelayListener.publish_args.origin_host = toolong;
    RelayListener.publish(work);
    TEST_ASSERT_FALSE(RelayListener.ok);

    TEST_ASSERT_NULL(bind_by_listener(work, 1)); // none of the three consumed a slot
}

// Header: PROTOCORE_RELAY_HOST_MAX is the length "incl. NUL", so HOST_MAX-1 characters is the
// longest host that fits and HOST_MAX characters is the first that does not.
void test_the_host_length_boundary(void)
{
    char host[PROTOCORE_RELAY_HOST_MAX + 1];
    for (size_t i = 0; i < PROTOCORE_RELAY_HOST_MAX; i++)
    {
        host[i] = 'h';
    }
    host[PROTOCORE_RELAY_HOST_MAX] = '\0'; // exactly HOST_MAX characters: one too many

    RelayListener.publish_args.listener_id = 1;
    RelayListener.publish_args.origin_host = host;
    RelayListener.publish_args.origin_port = 80;
    RelayListener.publish(work);
    TEST_ASSERT_FALSE(RelayListener.ok);

    host[PROTOCORE_RELAY_HOST_MAX - 1] = '\0'; // HOST_MAX-1 characters plus the NUL: the longest fit
    RelayListener.publish_args.origin_host = host;
    RelayListener.publish(work);
    TEST_ASSERT_TRUE(RelayListener.ok);
    TEST_ASSERT_EQUAL_STRING(host, bind_by_listener(work, 1)->host);
}

// Header: "false if ... the bind table is full (PROTOCORE_RELAY_MAX_PUBLISH)".
void test_the_table_fills_at_its_bound_and_then_refuses(void)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        RelayListener.publish_args.listener_id = (uint8_t)i;
        RelayListener.publish_args.origin_host = "10.0.0.1";
        RelayListener.publish_args.origin_port = (uint16_t)(1000 + i);
        RelayListener.publish(work);
        TEST_ASSERT_TRUE_MESSAGE(RelayListener.ok, "a bind inside the bound was refused");
    }
    RelayListener.publish_args.listener_id = 99;
    RelayListener.publish_args.origin_host = "10.0.0.2";
    RelayListener.publish_args.origin_port = 9999;
    RelayListener.publish(work);
    TEST_ASSERT_FALSE(RelayListener.ok); // one past the bound

    TEST_ASSERT_NULL(bind_by_listener(work, 99));
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        TEST_ASSERT_NOT_NULL(bind_by_listener(work, (uint8_t)i)); // the refusal disturbed none of them
    }
}

// Header: reset "clear[s] all published binds and active bridges (start from empty)".
void test_reset_returns_the_table_to_empty(void)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        RelayListener.publish_args.listener_id = (uint8_t)i;
        RelayListener.publish_args.origin_host = "10.0.0.1";
        RelayListener.publish_args.origin_port = 80;
        RelayListener.publish(work);
    }
    RELAY_LISTENER_CTX(work)->bridges[0].active = PROTO_TRUE;

    RelayListener.reset(work);

    for (int i = 0; i < PROTOCORE_RELAY_MAX_PUBLISH; i++)
    {
        TEST_ASSERT_NULL(bind_by_listener(work, (uint8_t)i));
    }
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
    {
        TEST_ASSERT_FALSE(RELAY_LISTENER_CTX(work)->bridges[i].active);
    }
    TEST_ASSERT_EQUAL_INT(0, bridge_find_free(work)); // the whole bridge table is free again
}

// The bridge allocator hands out each free slot once and reports -1 when there are none, so two
// inbound connections never land on one bridge.
void test_the_bridge_allocator_hands_out_each_slot_once(void)
{
    for (int i = 0; i < PROTOCORE_RELAY_MAX_CONNS; i++)
    {
        int idx = bridge_find_free(work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i, idx, "a free bridge slot was handed out twice");
        RELAY_LISTENER_CTX(work)->bridges[idx].active = PROTO_TRUE;
    }
    TEST_ASSERT_EQUAL_INT(-1, bridge_find_free(work)); // full

    RELAY_LISTENER_CTX(work)->bridges[2].active = PROTO_FALSE;
    TEST_ASSERT_EQUAL_INT(2, bridge_find_free(work)); // the freed slot is the next one out
}

// A bridge is found by the inbound connection slot it was opened for, not by its index, so the
// close and poll callbacks reach the right one.
void test_a_bridge_is_found_by_its_connection_slot(void)
{
    TEST_ASSERT_NULL(bridge_by_conn(work, 5));

    RELAY_LISTENER_CTX(work)->bridges[1].active = PROTO_TRUE;
    RELAY_LISTENER_CTX(work)->bridges[1].conn_slot = 5;
    RelayBridge *br = bridge_by_conn(work, 5);
    TEST_ASSERT_NOT_NULL(br);
    TEST_ASSERT_EQUAL_PTR(&RELAY_LISTENER_CTX(work)->bridges[1], br);

    RELAY_LISTENER_CTX(work)->bridges[1].active = PROTO_FALSE;
    TEST_ASSERT_NULL(bridge_by_conn(work, 5)); // an inactive bridge is not a match
}

// Two listener ids may forward to the same origin, and one id is bound once: publishing the same
// id twice takes a second slot rather than overwriting, which is what the table's fill order says.
void test_each_publish_takes_its_own_slot(void)
{
    RelayListener.publish_args.origin_host = "10.0.0.1";
    RelayListener.publish_args.origin_port = 80;
    RelayListener.publish_args.listener_id = 2;
    RelayListener.publish(work);
    TEST_ASSERT_TRUE(RelayListener.ok);
    RelayListener.publish_args.listener_id = 2;
    RelayListener.publish(work);
    TEST_ASSERT_TRUE(RelayListener.ok);

    TEST_ASSERT_TRUE(RELAY_LISTENER_CTX(work)->binds[0].active);
    TEST_ASSERT_TRUE(RELAY_LISTENER_CTX(work)->binds[1].active);
    // the lookup answers with the first match, so the earlier bind is the live one
    TEST_ASSERT_EQUAL_PTR(&RELAY_LISTENER_CTX(work)->binds[0], bind_by_listener(work, 2));
}
