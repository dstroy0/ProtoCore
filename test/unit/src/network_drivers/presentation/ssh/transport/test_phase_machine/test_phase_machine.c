// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/phase_machine.c: the order RFC 4253 puts its messages in, as the one place that decides
// what a slot will accept next. sec 4.2 identification, sec 7.1 negotiation, sec 8 exchange,
// sec 7.3 NEWKEYS, sec 10 service request, RFC 4252 authentication, and sec 9 re-exchange - which
// runs the same sequence again without disturbing what sits above it.

#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/phase_machine/phase_machine.h"
#include "network_drivers/presentation/ssh/transport/transport/transport.h"
#include <stdint.h>

#include <unity.h>

static uint8_t phase_machine_work[16]; // the borrow an entry takes; PhaseMachine never reads it

void setUp(void)
{
    ssh_transport_init(0);
    ssh_pkt_init(0);
    PhaseMachineV.reset_args.i = 0;
    PhaseMachine.reset(phase_machine_work);
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
    PhaseMachineV.newkeys_done_args.i = 0;
    PhaseMachine.newkeys_done(phase_machine_work);
}

// Drive a slot to the point where authentication has completed, which is where sec 9 says a
// re-exchange must leave the connection undisturbed.
static void run_to_open(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    newkeys_crossed();
    PhaseMachineV.service_done_args.i = 0;
    PhaseMachine.service_done(phase_machine_work);
    PhaseMachineV.auth_done_args.i = 0;
    PhaseMachine.auth_done(phase_machine_work);
}

// ---------------------------------------------------------------------------
// the sequence, in the order the sections come
// ---------------------------------------------------------------------------

// sec 4.2: the identification string is exchanged "before" anything else, so a fresh slot is
// waiting for it and for nothing else.
void test_sec4_2_a_reset_slot_awaits_the_identification_string(void)
{
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_IDENT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_ident_args.i = 0;
    PhaseMachine.admits_ident(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexinit_args.i = 0;
    PhaseMachine.admits_kexinit(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexdh_init_args.i = 0;
    PhaseMachine.admits_kexdh_init(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_newkeys_args.i = 0;
    PhaseMachine.admits_newkeys(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_service_request_args.i = 0;
    PhaseMachine.admits_service_request(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_userauth_args.i = 0;
    PhaseMachine.admits_userauth(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// sec 7.1: "Key exchange begins by each side sending... SSH_MSG_KEXINIT", which is what the
// identification string being whole opens the door to.
void test_sec7_1_identification_opens_negotiation(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_KEXINIT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexinit_args.i = 0;
    PhaseMachine.admits_kexinit(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexdh_init_args.i = 0;
    PhaseMachine.admits_kexdh_init(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// sec 8: the exchange's own messages follow negotiation, not precede it.
void test_sec8_negotiation_opens_the_exchange(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_DH_INIT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexdh_init_args.i = 0;
    PhaseMachine.admits_kexdh_init(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexinit_args.i = 0;
    PhaseMachine.admits_kexinit(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok); // an exchange is already running (sec 9)
}

// sec 7.3: "Key exchange ends by each side sending an SSH_MSG_NEWKEYS message."
void test_sec7_3_the_exchange_ends_at_newkeys(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_NEWKEYS;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_newkeys_args.i = 0;
    PhaseMachine.admits_newkeys(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_service_request_args.i = 0;
    PhaseMachine.admits_service_request(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok); // not until NEWKEYS crosses
}

// sec 10: "the client sends a service request once a secure transport layer connection has been
// established" - so it is admitted after NEWKEYS and not before.
void test_sec10_newkeys_opens_the_service_request(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    PhaseMachineV.newkeys_done_args.i = 0;
    PhaseMachine.newkeys_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_SERVICE;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_service_request_args.i = 0;
    PhaseMachine.admits_service_request(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_userauth_args.i = 0;
    PhaseMachine.admits_userauth(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// RFC 4252: the authentication protocol "runs over the transport layer protocol", once its service
// has started.
void test_rfc4252_service_opens_authentication(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    PhaseMachineV.newkeys_done_args.i = 0;
    PhaseMachine.newkeys_done(phase_machine_work);
    PhaseMachineV.service_done_args.i = 0;
    PhaseMachine.service_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_AUTH;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_userauth_args.i = 0;
    PhaseMachine.admits_userauth(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.auth_complete_args.i = 0;
    PhaseMachine.auth_complete(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.is_open_args.i = 0;
    PhaseMachine.is_open(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// RFC 4254 runs over the authentication protocol, so the connection protocol opens only after it.
void test_rfc4254_authentication_opens_the_connection_protocol(void)
{
    run_to_open();
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_OPEN;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.is_open_args.i = 0;
    PhaseMachine.is_open(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.auth_complete_args.i = 0;
    PhaseMachine.auth_complete(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// ---------------------------------------------------------------------------
// sec 9  Key Re-Exchange
// ---------------------------------------------------------------------------

// "Key re-exchange is started by sending an SSH_MSG_KEXINIT packet when not already doing a key
// exchange." An open connection is not, so one may start.
void test_sec9_an_open_connection_admits_a_re_exchange(void)
{
    run_to_open();
    PhaseMachineV.admits_rekey_args.i = 0;
    PhaseMachine.admits_rekey(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexinit_args.i = 0;
    PhaseMachine.admits_kexinit(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// "when not already doing a key exchange" - one is running from KEXINIT through NEWKEYS, and a
// second cannot start inside it.
void test_sec9_no_second_exchange_while_one_runs(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.admits_rekey_args.i = 0;
    PhaseMachine.admits_rekey(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok); // DH_INIT: mid-exchange
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    PhaseMachineV.admits_rekey_args.i = 0;
    PhaseMachine.admits_rekey(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok); // NEWKEYS: still mid-exchange
}

// Nor before the identification string, when no exchange can run at all.
void test_sec9_no_re_exchange_before_identification(void)
{
    PhaseMachineV.admits_rekey_args.i = 0;
    PhaseMachine.admits_rekey(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexinit_args.i = 0;
    PhaseMachine.admits_kexinit(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// "Re-exchange is processed identically to the initial key exchange" - it runs the same sequence.
void test_sec9_re_exchange_runs_the_same_sequence(void)
{
    run_to_open();
    PhaseMachineV.rekey_begin_args.i = 0;
    PhaseMachine.rekey_begin(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_KEXINIT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_DH_INIT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_NEWKEYS;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// "key exchange does not affect the protocols that lie above the SSH transport layer." A
// re-exchange from OPEN ends back at OPEN, not at the service request.
void test_sec9_re_exchange_from_open_returns_to_open(void)
{
    run_to_open();
    begin_exchange();
    PhaseMachineV.rekey_begin_args.i = 0;
    PhaseMachine.rekey_begin(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    newkeys_crossed();
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_OPEN;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.is_open_args.i = 0;
    PhaseMachine.is_open(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// The same rule mid-authentication: a re-exchange begun while a userauth request is in flight puts
// the connection back into authentication, not back to the service request it already answered.
void test_sec9_re_exchange_mid_authentication_returns_to_authentication(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    newkeys_crossed();
    PhaseMachineV.service_done_args.i = 0;
    PhaseMachine.service_done(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_AUTH;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);

    begin_exchange();
    PhaseMachineV.rekey_begin_args.i = 0;
    PhaseMachine.rekey_begin(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    newkeys_crossed();
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_AUTH;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_userauth_args.i = 0;
    PhaseMachine.admits_userauth(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// And from the service phase, which the first exchange reaches.
void test_sec9_re_exchange_from_service_returns_to_service(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    newkeys_crossed();
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_SERVICE;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);

    begin_exchange();
    PhaseMachineV.rekey_begin_args.i = 0;
    PhaseMachine.rekey_begin(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    newkeys_crossed();
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_SERVICE;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// A re-exchange does not undo authentication: the connection stays authenticated across it.
void test_sec9_authentication_survives_a_re_exchange(void)
{
    run_to_open();
    PhaseMachineV.rekey_begin_args.i = 0;
    PhaseMachine.rekey_begin(phase_machine_work);
    PhaseMachineV.auth_complete_args.i = 0;
    PhaseMachine.auth_complete(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok); // still authenticated mid-exchange
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    PhaseMachineV.newkeys_done_args.i = 0;
    PhaseMachine.newkeys_done(phase_machine_work);
    PhaseMachineV.auth_complete_args.i = 0;
    PhaseMachine.auth_complete(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// ---------------------------------------------------------------------------
// sec 7.1  answering a KEXINIT
// ---------------------------------------------------------------------------
// "a party MUST respond with its own SSH_MSG_KEXINIT message, except when the received
// SSH_MSG_KEXINIT already was a reply."

void test_sec7_1_a_first_kexinit_is_answered(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_needs_reply_args.i = 0;
    PhaseMachine.kexinit_needs_reply(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok); // this end has not sent one yet
}

void test_sec7_1_a_kexinit_that_was_a_reply_is_not_answered(void)
{
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    ssh_sess[0].kexinit_sent = PROTO_TRUE; // ours is already out, so the peer's is the reply
    PhaseMachineV.kexinit_needs_reply_args.i = 0;
    PhaseMachine.kexinit_needs_reply(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// ---------------------------------------------------------------------------
// reset, and the pool's edge
// ---------------------------------------------------------------------------

// sec 4.2 again: a reset puts the sequence back at the identification string. It moves the phase
// and nothing else - whether the slot is still authenticated belongs to the session the transport
// zeroes when it hands the slot out, not to this machine.
void test_reset_returns_to_the_identification_string(void)
{
    run_to_open();
    PhaseMachineV.reset_args.i = 0;
    PhaseMachine.reset(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_IDENT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.is_open_args.i = 0;
    PhaseMachine.is_open(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_ident_args.i = 0;
    PhaseMachine.admits_ident(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.admits_userauth_args.i = 0;
    PhaseMachine.admits_userauth(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// A reset also drops whatever a re-exchange would have resumed into: the first exchange on a fresh
// slot ends at the sec 10 service request, not at a phase the previous connection reached.
void test_reset_resumes_a_first_exchange_at_the_service_request(void)
{
    run_to_open();
    PhaseMachineV.reset_args.i = 0;
    PhaseMachine.reset(phase_machine_work);
    PhaseMachineV.ident_done_args.i = 0;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = 0;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = 0;
    PhaseMachine.kex_done(phase_machine_work);
    newkeys_crossed();
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_SERVICE;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// Every query answers false for a slot outside the pool rather than reading past it.
void test_slot_past_the_pool_admits_nothing(void)
{
    const uint8_t bad = MAX_SSH_CONNS;
    PhaseMachineV.admits_ident_args.i = bad;
    PhaseMachine.admits_ident(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexinit_args.i = bad;
    PhaseMachine.admits_kexinit(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_kexdh_init_args.i = bad;
    PhaseMachine.admits_kexdh_init(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_newkeys_args.i = bad;
    PhaseMachine.admits_newkeys(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_service_request_args.i = bad;
    PhaseMachine.admits_service_request(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_userauth_args.i = bad;
    PhaseMachine.admits_userauth(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.admits_rekey_args.i = bad;
    PhaseMachine.admits_rekey(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.auth_complete_args.i = bad;
    PhaseMachine.auth_complete(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.is_open_args.i = bad;
    PhaseMachine.is_open(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.kexinit_needs_reply_args.i = bad;
    PhaseMachine.kexinit_needs_reply(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
    PhaseMachineV.is_args.i = bad;
    PhaseMachineV.is_args.p = SSH_PHASE_IDENT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_FALSE(PhaseMachineV.ok);
}

// Advancing a slot outside the pool touches nothing inside it.
void test_advancing_a_bad_slot_is_inert(void)
{
    run_to_open();
    const uint8_t bad = MAX_SSH_CONNS;
    PhaseMachineV.reset_args.i = bad;
    PhaseMachine.reset(phase_machine_work);
    PhaseMachineV.ident_done_args.i = bad;
    PhaseMachine.ident_done(phase_machine_work);
    PhaseMachineV.kexinit_done_args.i = bad;
    PhaseMachine.kexinit_done(phase_machine_work);
    PhaseMachineV.kex_done_args.i = bad;
    PhaseMachine.kex_done(phase_machine_work);
    PhaseMachineV.newkeys_done_args.i = bad;
    PhaseMachine.newkeys_done(phase_machine_work);
    PhaseMachineV.service_done_args.i = bad;
    PhaseMachine.service_done(phase_machine_work);
    PhaseMachineV.auth_done_args.i = bad;
    PhaseMachine.auth_done(phase_machine_work);
    PhaseMachineV.rekey_begin_args.i = bad;
    PhaseMachine.rekey_begin(phase_machine_work);
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_OPEN;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}

// The phases are held per slot, so one connection's progress is not another's.
void test_phases_are_per_slot(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    PhaseMachineV.reset_args.i = 1;
    PhaseMachine.reset(phase_machine_work);
    run_to_open();
    PhaseMachineV.is_args.i = 0;
    PhaseMachineV.is_args.p = SSH_PHASE_OPEN;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
    PhaseMachineV.is_args.i = 1;
    PhaseMachineV.is_args.p = SSH_PHASE_IDENT;
    PhaseMachine.is(phase_machine_work);
    TEST_ASSERT_TRUE(PhaseMachineV.ok);
}
