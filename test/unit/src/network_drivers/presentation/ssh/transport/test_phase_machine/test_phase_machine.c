// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/phase_machine.c: the order RFC 4253 puts its messages in, as the one place that decides
// what a slot will accept next. sec 4.2 identification, sec 7.1 negotiation, sec 8 exchange,
// sec 7.3 NEWKEYS, sec 10 service request, RFC 4252 authentication, and sec 9 re-exchange - which
// runs the same sequence again without disturbing what sits above it.

#include "network_drivers/presentation/ssh/transport/phase_machine.h"
#include "network_drivers/presentation/ssh/transport/transport.h"
#include <stdint.h>

#include <unity.h>

void setUp(void)
{
    ssh_transport_init(0);
    ssh_pkt_init(0);
    ssh_phase_reset(0);
}
void tearDown(void)
{
}

// An exchange is running from the moment the slot opens (ssh_transport_init) and stops when NEWKEYS
// crosses. That flag lives in the session, not in this machine, and ssh_phase_admits_rekey reads
// both - so these two mirror what the transport does either side of the phase advance.
static void begin_exchange(void)
{
    ssh_sess[0].kex_active = PROTO_TRUE;
}
static void newkeys_crossed(void)
{
    ssh_sess[0].kex_active = PROTO_FALSE;
    ssh_phase_newkeys_done(0);
}

// Drive a slot to the point where authentication has completed, which is where sec 9 says a
// re-exchange must leave the connection undisturbed.
static void run_to_open(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    newkeys_crossed();
    ssh_phase_service_done(0);
    ssh_phase_auth_done(0);
}

// ---------------------------------------------------------------------------
// the sequence, in the order the sections come
// ---------------------------------------------------------------------------

// sec 4.2: the identification string is exchanged "before" anything else, so a fresh slot is
// waiting for it and for nothing else.
static void test_sec4_2_a_reset_slot_awaits_the_identification_string(void)
{
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_IDENT));
    TEST_ASSERT_TRUE(ssh_phase_admits_ident(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_kexinit(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_kexdh_init(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_newkeys(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_service_request(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_userauth(0));
}

// sec 7.1: "Key exchange begins by each side sending... SSH_MSG_KEXINIT", which is what the
// identification string being whole opens the door to.
static void test_sec7_1_identification_opens_negotiation(void)
{
    ssh_phase_ident_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_KEXINIT));
    TEST_ASSERT_TRUE(ssh_phase_admits_kexinit(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_kexdh_init(0));
}

// sec 8: the exchange's own messages follow negotiation, not precede it.
static void test_sec8_negotiation_opens_the_exchange(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_DH_INIT));
    TEST_ASSERT_TRUE(ssh_phase_admits_kexdh_init(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_kexinit(0)); // an exchange is already running (sec 9)
}

// sec 7.3: "Key exchange ends by each side sending an SSH_MSG_NEWKEYS message."
static void test_sec7_3_the_exchange_ends_at_newkeys(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_NEWKEYS));
    TEST_ASSERT_TRUE(ssh_phase_admits_newkeys(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_service_request(0)); // not until NEWKEYS crosses
}

// sec 10: "the client sends a service request once a secure transport layer connection has been
// established" - so it is admitted after NEWKEYS and not before.
static void test_sec10_newkeys_opens_the_service_request(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    ssh_phase_newkeys_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_SERVICE));
    TEST_ASSERT_TRUE(ssh_phase_admits_service_request(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_userauth(0));
}

// RFC 4252: the authentication protocol "runs over the transport layer protocol", once its service
// has started.
static void test_rfc4252_service_opens_authentication(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    ssh_phase_newkeys_done(0);
    ssh_phase_service_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_AUTH));
    TEST_ASSERT_TRUE(ssh_phase_admits_userauth(0));
    TEST_ASSERT_FALSE(ssh_phase_auth_complete(0));
    TEST_ASSERT_FALSE(ssh_phase_is_open(0));
}

// RFC 4254 runs over the authentication protocol, so the connection protocol opens only after it.
static void test_rfc4254_authentication_opens_the_connection_protocol(void)
{
    run_to_open();
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_OPEN));
    TEST_ASSERT_TRUE(ssh_phase_is_open(0));
    TEST_ASSERT_TRUE(ssh_phase_auth_complete(0));
}

// ---------------------------------------------------------------------------
// sec 9  Key Re-Exchange
// ---------------------------------------------------------------------------

// "Key re-exchange is started by sending an SSH_MSG_KEXINIT packet when not already doing a key
// exchange." An open connection is not, so one may start.
static void test_sec9_an_open_connection_admits_a_re_exchange(void)
{
    run_to_open();
    TEST_ASSERT_TRUE(ssh_phase_admits_rekey(0));
    TEST_ASSERT_TRUE(ssh_phase_admits_kexinit(0));
}

// "when not already doing a key exchange" - one is running from KEXINIT through NEWKEYS, and a
// second cannot start inside it.
static void test_sec9_no_second_exchange_while_one_runs(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    TEST_ASSERT_FALSE(ssh_phase_admits_rekey(0)); // DH_INIT: mid-exchange
    ssh_phase_kex_done(0);
    TEST_ASSERT_FALSE(ssh_phase_admits_rekey(0)); // NEWKEYS: still mid-exchange
}

// Nor before the identification string, when no exchange can run at all.
static void test_sec9_no_re_exchange_before_identification(void)
{
    TEST_ASSERT_FALSE(ssh_phase_admits_rekey(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_kexinit(0));
}

// "Re-exchange is processed identically to the initial key exchange" - it runs the same sequence.
static void test_sec9_re_exchange_runs_the_same_sequence(void)
{
    run_to_open();
    ssh_phase_rekey_begin(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_KEXINIT));
    ssh_phase_kexinit_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_DH_INIT));
    ssh_phase_kex_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_NEWKEYS));
}

// "key exchange does not affect the protocols that lie above the SSH transport layer." A
// re-exchange from OPEN ends back at OPEN, not at the service request.
static void test_sec9_re_exchange_from_open_returns_to_open(void)
{
    run_to_open();
    begin_exchange();
    ssh_phase_rekey_begin(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    newkeys_crossed();
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_OPEN));
    TEST_ASSERT_TRUE(ssh_phase_is_open(0));
}

// The same rule mid-authentication: a re-exchange begun while a userauth request is in flight puts
// the connection back into authentication, not back to the service request it already answered.
static void test_sec9_re_exchange_mid_authentication_returns_to_authentication(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    newkeys_crossed();
    ssh_phase_service_done(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_AUTH));

    begin_exchange();
    ssh_phase_rekey_begin(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    newkeys_crossed();
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_AUTH));
    TEST_ASSERT_TRUE(ssh_phase_admits_userauth(0));
}

// And from the service phase, which the first exchange reaches.
static void test_sec9_re_exchange_from_service_returns_to_service(void)
{
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    newkeys_crossed();
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_SERVICE));

    begin_exchange();
    ssh_phase_rekey_begin(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    newkeys_crossed();
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_SERVICE));
}

// A re-exchange does not undo authentication: the connection stays authenticated across it.
static void test_sec9_authentication_survives_a_re_exchange(void)
{
    run_to_open();
    ssh_phase_rekey_begin(0);
    TEST_ASSERT_TRUE(ssh_phase_auth_complete(0)); // still authenticated mid-exchange
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    ssh_phase_newkeys_done(0);
    TEST_ASSERT_TRUE(ssh_phase_auth_complete(0));
}

// ---------------------------------------------------------------------------
// sec 7.1  answering a KEXINIT
// ---------------------------------------------------------------------------
// "a party MUST respond with its own SSH_MSG_KEXINIT message, except when the received
// SSH_MSG_KEXINIT already was a reply."

static void test_sec7_1_a_first_kexinit_is_answered(void)
{
    ssh_phase_ident_done(0);
    TEST_ASSERT_TRUE(ssh_kexinit_needs_reply(0)); // this end has not sent one yet
}

static void test_sec7_1_a_kexinit_that_was_a_reply_is_not_answered(void)
{
    ssh_phase_ident_done(0);
    ssh_sess[0].kexinit_sent = PROTO_TRUE; // ours is already out, so the peer's is the reply
    TEST_ASSERT_FALSE(ssh_kexinit_needs_reply(0));
}

// ---------------------------------------------------------------------------
// reset, and the pool's edge
// ---------------------------------------------------------------------------

// sec 4.2 again: a reset puts the sequence back at the identification string. It moves the phase
// and nothing else - whether the slot is still authenticated belongs to the session the transport
// zeroes when it hands the slot out, not to this machine.
static void test_reset_returns_to_the_identification_string(void)
{
    run_to_open();
    ssh_phase_reset(0);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_IDENT));
    TEST_ASSERT_FALSE(ssh_phase_is_open(0));
    TEST_ASSERT_TRUE(ssh_phase_admits_ident(0));
    TEST_ASSERT_FALSE(ssh_phase_admits_userauth(0));
}

// A reset also drops whatever a re-exchange would have resumed into: the first exchange on a fresh
// slot ends at the sec 10 service request, not at a phase the previous connection reached.
static void test_reset_resumes_a_first_exchange_at_the_service_request(void)
{
    run_to_open();
    ssh_phase_reset(0);
    ssh_phase_ident_done(0);
    ssh_phase_kexinit_done(0);
    ssh_phase_kex_done(0);
    newkeys_crossed();
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_SERVICE));
}

// Every query answers false for a slot outside the pool rather than reading past it.
static void test_slot_past_the_pool_admits_nothing(void)
{
    const uint8_t bad = MAX_SSH_CONNS;
    TEST_ASSERT_FALSE(ssh_phase_admits_ident(bad));
    TEST_ASSERT_FALSE(ssh_phase_admits_kexinit(bad));
    TEST_ASSERT_FALSE(ssh_phase_admits_kexdh_init(bad));
    TEST_ASSERT_FALSE(ssh_phase_admits_newkeys(bad));
    TEST_ASSERT_FALSE(ssh_phase_admits_service_request(bad));
    TEST_ASSERT_FALSE(ssh_phase_admits_userauth(bad));
    TEST_ASSERT_FALSE(ssh_phase_admits_rekey(bad));
    TEST_ASSERT_FALSE(ssh_phase_auth_complete(bad));
    TEST_ASSERT_FALSE(ssh_phase_is_open(bad));
    TEST_ASSERT_FALSE(ssh_kexinit_needs_reply(bad));
    TEST_ASSERT_FALSE(ssh_phase_is(bad, SSH_PHASE_IDENT));
}

// Advancing a slot outside the pool touches nothing inside it.
static void test_advancing_a_bad_slot_is_inert(void)
{
    run_to_open();
    const uint8_t bad = MAX_SSH_CONNS;
    ssh_phase_reset(bad);
    ssh_phase_ident_done(bad);
    ssh_phase_kexinit_done(bad);
    ssh_phase_kex_done(bad);
    ssh_phase_newkeys_done(bad);
    ssh_phase_service_done(bad);
    ssh_phase_auth_done(bad);
    ssh_phase_rekey_begin(bad);
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_OPEN));
}

// The phases are held per slot, so one connection's progress is not another's.
static void test_phases_are_per_slot(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    ssh_phase_reset(1);
    run_to_open();
    TEST_ASSERT_TRUE(ssh_phase_is(0, SSH_PHASE_OPEN));
    TEST_ASSERT_TRUE(ssh_phase_is(1, SSH_PHASE_IDENT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec4_2_a_reset_slot_awaits_the_identification_string);
    RUN_TEST(test_sec7_1_identification_opens_negotiation);
    RUN_TEST(test_sec8_negotiation_opens_the_exchange);
    RUN_TEST(test_sec7_3_the_exchange_ends_at_newkeys);
    RUN_TEST(test_sec10_newkeys_opens_the_service_request);
    RUN_TEST(test_rfc4252_service_opens_authentication);
    RUN_TEST(test_rfc4254_authentication_opens_the_connection_protocol);
    RUN_TEST(test_sec9_an_open_connection_admits_a_re_exchange);
    RUN_TEST(test_sec9_no_second_exchange_while_one_runs);
    RUN_TEST(test_sec9_no_re_exchange_before_identification);
    RUN_TEST(test_sec9_re_exchange_runs_the_same_sequence);
    RUN_TEST(test_sec9_re_exchange_from_open_returns_to_open);
    RUN_TEST(test_sec9_re_exchange_mid_authentication_returns_to_authentication);
    RUN_TEST(test_sec9_re_exchange_from_service_returns_to_service);
    RUN_TEST(test_sec9_authentication_survives_a_re_exchange);
    RUN_TEST(test_sec7_1_a_first_kexinit_is_answered);
    RUN_TEST(test_sec7_1_a_kexinit_that_was_a_reply_is_not_answered);
    RUN_TEST(test_reset_returns_to_the_identification_string);
    RUN_TEST(test_reset_resumes_a_first_exchange_at_the_service_request);
    RUN_TEST(test_slot_past_the_pool_admits_nothing);
    RUN_TEST(test_advancing_a_bad_slot_is_inert);
    RUN_TEST(test_phases_are_per_slot);
    return UNITY_END();
}
