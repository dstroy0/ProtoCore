// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// server/server.c (RFC 4253 sec 4.1, RFC 4254 sec 7.1): the listening role - the handler the
// session loop installs for an accepted SSH connection, and the sockets a sec 7.1 binding accepts
// on.
//
// "When used over TCP/IP, the server normally listens for connections on port 22." Listening is
// this role's; which bindings exist is the connection protocol's decision, so the socket comes from
// here and the binding asks for it by port.

#include "network_drivers/presentation/ssh/server/server.h"
#include "server/system/proto_handler.h"
#include <stdint.h>

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// sec 4.1  the handler the session loop installs
// ---------------------------------------------------------------------------

// The accessor exists so the builtins list can bind this role without this file naming the session
// layer. It has to hand back a handler, or nothing dispatches to SSH at all.
static void test_sec4_1_the_ssh_handler_is_published(void)
{
    const ProtoHandler *h = ssh_protocore_handler();
    TEST_ASSERT_NOT_NULL(h);
}

// An accepted connection has to be able to start, carry bytes, and be torn down, so the three
// events the session loop raises all have somewhere to go.
static void test_sec4_1_the_handler_covers_accept_data_and_close(void)
{
    const ProtoHandler *h = ssh_protocore_handler();
    TEST_ASSERT_NOT_NULL(h->on_accept);
    TEST_ASSERT_NOT_NULL(h->on_data);
    TEST_ASSERT_NOT_NULL(h->on_close);
}

// The role also runs work that no inbound byte triggers - the sec 9 re-exchange budget, a deferred
// password change, the forward pump - so it takes the poll the loop offers.
static void test_sec4_1_the_handler_takes_the_poll(void)
{
    const ProtoHandler *h = ssh_protocore_handler();
    TEST_ASSERT_NOT_NULL(h->on_poll);
}

// The accessor is a view of one instance, not a fresh handler per call.
static void test_sec4_1_the_handler_is_one_instance(void)
{
    TEST_ASSERT_EQUAL_PTR(ssh_protocore_handler(), ssh_protocore_handler());
}

#if PROTOCORE_SSH_PORT_FORWARD
// ---------------------------------------------------------------------------
// RFC 4254 sec 7.2  sockets accepted on a forwarded port
// ---------------------------------------------------------------------------

// A connection arriving on a bound port is not an SSH connection - it is the thing being forwarded -
// so it gets its own handler rather than the one above.
static void test_sec7_2_the_forward_handler_is_published_and_distinct(void)
{
    const ProtoHandler *rf = ssh_protocore_rfwd_handler();
    TEST_ASSERT_NOT_NULL(rf);
    TEST_ASSERT_TRUE(rf != ssh_protocore_handler());
}

static void test_sec7_2_the_forward_handler_covers_its_events(void)
{
    const ProtoHandler *rf = ssh_protocore_rfwd_handler();
    TEST_ASSERT_NOT_NULL(rf->on_accept);
    TEST_ASSERT_NOT_NULL(rf->on_data);
    TEST_ASSERT_NOT_NULL(rf->on_close);
    TEST_ASSERT_NOT_NULL(rf->on_poll);
}

static void test_sec7_2_the_forward_handler_is_one_instance(void)
{
    TEST_ASSERT_EQUAL_PTR(ssh_protocore_rfwd_handler(), ssh_protocore_rfwd_handler());
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 7.1  the socket a binding accepts on
// ---------------------------------------------------------------------------

// "The 'address to bind' and 'port number to bind' specify the IP address... and port on which
// connections for forwarding are to be accepted." The caller names a port and gets a handle back.
static void test_sec7_1_a_binding_gets_a_listener(void)
{
    const int h = ssh_rfwd_listener_open(48080);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    ssh_rfwd_listener_close(h);
}

// Closing gives the listener back, so the next binding can have one.
static void test_sec7_1_closing_returns_the_listener(void)
{
    const int a = ssh_rfwd_listener_open(48081);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, a);
    ssh_rfwd_listener_close(a);

    const int b = ssh_rfwd_listener_open(48082);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, b);
    ssh_rfwd_listener_close(b);
}

// Two bindings at once are two different sockets, or one would accept the other's connections.
static void test_sec7_1_two_bindings_get_two_listeners(void)
{
    const int a = ssh_rfwd_listener_open(48083);
    const int b = ssh_rfwd_listener_open(48084);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, a);
    if (b >= 0)
    {
        TEST_ASSERT_TRUE(a != b);
        ssh_rfwd_listener_close(b);
    }
    ssh_rfwd_listener_close(a);
}

// A handle that was never opened, or one outside the pool, closes nothing rather than stopping
// somebody else's listener.
static void test_sec7_1_closing_a_bad_handle_is_inert(void)
{
    ssh_rfwd_listener_close(-1);
    ssh_rfwd_listener_close(0x7FFFFFFF);

    const int h = ssh_rfwd_listener_open(48085);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, h);
    ssh_rfwd_listener_close(h - 1000); // not this one
    ssh_rfwd_listener_close(h);
}
#endif // PROTOCORE_SSH_PORT_FORWARD

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec4_1_the_ssh_handler_is_published);
    RUN_TEST(test_sec4_1_the_handler_covers_accept_data_and_close);
    RUN_TEST(test_sec4_1_the_handler_takes_the_poll);
    RUN_TEST(test_sec4_1_the_handler_is_one_instance);
#if PROTOCORE_SSH_PORT_FORWARD
    RUN_TEST(test_sec7_2_the_forward_handler_is_published_and_distinct);
    RUN_TEST(test_sec7_2_the_forward_handler_covers_its_events);
    RUN_TEST(test_sec7_2_the_forward_handler_is_one_instance);
    RUN_TEST(test_sec7_1_a_binding_gets_a_listener);
    RUN_TEST(test_sec7_1_closing_returns_the_listener);
    RUN_TEST(test_sec7_1_two_bindings_get_two_listeners);
    RUN_TEST(test_sec7_1_closing_a_bad_handle_is_inert);
#endif
    return UNITY_END();
}
