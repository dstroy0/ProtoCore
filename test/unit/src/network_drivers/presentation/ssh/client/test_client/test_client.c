// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// client/client.c (RFC 4253 sec 4, RFC 4252 sec 7): the initiating role - what it reports
// before it has a relay, and the slot scratch its handshake works out of.

#include "network_drivers/presentation/ssh/app/client/client.h"
#include "network_drivers/presentation/ssh/client/client.h"
#include <stdint.h>
#include <unity.h>

#if PROTOCORE_ENABLE_SSH_CLIENT

// The client's lifecycle calls, reached through its namespace.
static void client_poll(void)
{
    SshClient.poll(protocore_ssh_client_span());
}

static void client_end(void)
{
    SshClient.end(protocore_ssh_client_span());
}

// The client, reached through its namespace. Inside the gate: client.h declares SshClient only when
// the feature is compiled in, and this file is empty without it.
static proto_bool client_begin(const protocore_ssh_client_cfg *cfg)
{
    SshClient.cfg = cfg;
    SshClient.begin(protocore_ssh_client_span());
    return SshClient.ok;
}

void setUp(void)
{

    client_end(); // whatever a previous case left
}
void tearDown(void)
{

    client_end();
}

// Before anything is asked of it, the role has not started.
static void test_the_role_starts_idle(void)
{
    TEST_ASSERT_EQUAL(PROTOCORE_SSH_CLIENT_IDLE, (SshClient.state(protocore_ssh_client_span()), SshClient.state_of));
    TEST_ASSERT_EQUAL(PROTOCORE_SSH_CLIENT_IDLE, (SshClient.state(protocore_ssh_client_span()), SshClient.state_of));
    TEST_ASSERT_FALSE((SshClient.state(protocore_ssh_client_span()), SshClient.state_of == PROTOCORE_SSH_CLIENT_UP));
}

// A configuration with no relay to dial cannot start, and the role says so rather than reporting
// a connection it does not have.
static void test_a_configuration_without_a_host_does_not_start(void)
{
    static const uint8_t seed[32] = {1};
    protocore_ssh_client_cfg cfg = {0};
    cfg.host = NULL;
    cfg.user = "device";
    cfg.auth_seed = seed;
    cfg.bind_port = 8022;
    cfg.local_port = 80;

    TEST_ASSERT_FALSE(client_begin(&cfg));
    TEST_ASSERT_NOT_EQUAL(PROTOCORE_SSH_CLIENT_UP, (SshClient.state(protocore_ssh_client_span()), SshClient.state_of));
    TEST_ASSERT_FALSE((SshClient.state(protocore_ssh_client_span()), SshClient.state_of == PROTOCORE_SSH_CLIENT_UP));
}

// RFC 4252 sec 7 authenticates with a key, so a configuration without one cannot authenticate.
static void test_a_configuration_without_a_key_does_not_start(void)
{
    protocore_ssh_client_cfg cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.user = "device";
    cfg.auth_seed = NULL;
    cfg.bind_port = 8022;
    cfg.local_port = 80;

    TEST_ASSERT_FALSE(client_begin(&cfg));
    TEST_ASSERT_FALSE((SshClient.state(protocore_ssh_client_span()), SshClient.state_of == PROTOCORE_SSH_CLIENT_UP));
}

// A null configuration is not one.
static void test_a_null_configuration_does_not_start(void)
{
    TEST_ASSERT_FALSE(client_begin(NULL));
    TEST_ASSERT_FALSE((SshClient.state(protocore_ssh_client_span()), SshClient.state_of == PROTOCORE_SSH_CLIENT_UP));
}

// Ending returns the role to where it began, so a slot is not left claimed behind it.
static void test_end_returns_the_role_to_idle(void)
{
    client_end();
    TEST_ASSERT_EQUAL(PROTOCORE_SSH_CLIENT_IDLE, (SshClient.state(protocore_ssh_client_span()), SshClient.state_of));
    TEST_ASSERT_FALSE((SshClient.state(protocore_ssh_client_span()), SshClient.state_of == PROTOCORE_SSH_CLIENT_UP));
}

// Ending twice, or ending something never started, is not an error.
static void test_end_is_idempotent(void)
{
    client_end();
    client_end();
    TEST_ASSERT_EQUAL(PROTOCORE_SSH_CLIENT_IDLE, (SshClient.state(protocore_ssh_client_span()), SshClient.state_of));
}

// Polling an idle role does nothing and does not invent a connection.
static void test_polling_an_idle_role_is_inert(void)
{
    client_poll();
    client_poll();
    TEST_ASSERT_EQUAL(PROTOCORE_SSH_CLIENT_IDLE, (SshClient.state(protocore_ssh_client_span()), SshClient.state_of));
}

// The role works out of its slot's own bytes - the same borrow the wire and the packet MAC come
// from - so the scratch is there whenever the pool covers the slot.
static void test_crypto_work_comes_from_the_slot(void)
{
    uint8_t *work = (SshClient.crypto_work(protocore_ssh_client_span()), SshClient.work);
    TEST_ASSERT_NOT_NULL(work);
    TEST_ASSERT_EQUAL_PTR(work, (SshClient.crypto_work(protocore_ssh_client_span()), SshClient.work)); // the same bytes each time
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_role_starts_idle);
    RUN_TEST(test_a_configuration_without_a_host_does_not_start);
    RUN_TEST(test_a_configuration_without_a_key_does_not_start);
    RUN_TEST(test_a_null_configuration_does_not_start);
    RUN_TEST(test_end_returns_the_role_to_idle);
    RUN_TEST(test_end_is_idempotent);
    RUN_TEST(test_polling_an_idle_role_is_inert);
    RUN_TEST(test_crypto_work_comes_from_the_slot);
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
