// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for ssh/ssh_server.c - the message dispatcher.
//
// This is the phase gate: it decides which message types a peer may have acted on at each point in
// the connection, so a missing arm here is a message handled earlier than the protocol allows. Each
// case is named for the section that governs it.
//
// The oracle is quoted verbatim at each check.
//
// RFC 4253 sec 10, Service Request:
//   "After the key exchange, the client requests a service."
//   "If the server rejects the service request, it SHOULD send an appropriate SSH_MSG_DISCONNECT
//    message and MUST disconnect."
//   byte SSH_MSG_SERVICE_REQUEST | string service name
//
// RFC 4253 sec 11, Additional Messages: "Either party may send any of the following messages at any
// time."
//
//   sec 11.1  byte SSH_MSG_DISCONNECT | uint32 reason code | string description | string language
//             "This message causes immediate termination of the connection.  All implementations
//              MUST be able to process this message ... the recipient MUST NOT accept any data
//              after receiving this message."
//   sec 11.2  "All implementations MUST understand (and ignore) this message [SSH_MSG_IGNORE] at
//              any time (after receiving the identification string)."
//   sec 11.3  "All implementations MUST understand this message [SSH_MSG_DEBUG], but they are
//              allowed to ignore it."
//   sec 11.4  "An implementation MUST respond to all unrecognized messages with an
//              SSH_MSG_UNIMPLEMENTED message in the order in which the messages were received.
//              Such messages MUST be otherwise ignored."
//             byte SSH_MSG_UNIMPLEMENTED | uint32 packet sequence number of rejected message
//
// RFC 4252 sec 5.1: "SSH_MSG_USERAUTH_SUCCESS MUST be sent only once.  When
// SSH_MSG_USERAUTH_SUCCESS has been sent, any further authentication requests received after that
// SHOULD be silently ignored."
//
// RFC 4252 sec 5.3: "After sending SSH_MSG_USERAUTH_SUCCESS, the server starts the requested
// service." Until then the RFC 4254 connection protocol is not running.

#include "network_drivers/presentation/ssh/auth/ssh_auth.h"               // the SSH_MSG_USERAUTH_* numbers
#include "network_drivers/presentation/ssh/connection/ssh_flow_control.h" // the SSH_MSG_CHANNEL_* numbers
#include "network_drivers/presentation/ssh/ssh_server.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

// Everything the dispatcher emitted for this case, concatenated. A reply is one emit, so the length
// alone distinguishes "answered" from "understood and ignored".
static uint8_t g_out[512];
static size_t g_out_len;
static int g_emits;

static void capture_emit(uint8_t slot, const uint8_t *payload, size_t len)
{
    (void)slot;
    size_t take = len;
    if (g_out_len + take > sizeof(g_out))
    {
        take = sizeof(g_out) - g_out_len;
    }
    memcpy(g_out + g_out_len, payload, take);
    g_out_len += take;
    g_emits++;
}

void setUp()
{
    ssh_transport_init(0);
    ssh_pkt_init(0);
    pc_ssh_server_set_emit_cb(capture_emit);
    memset(g_out, 0, sizeof(g_out));
    g_out_len = 0;
    g_emits = 0;
}
void tearDown()
{
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 10 - Service Request
// ---------------------------------------------------------------------------

// "After the key exchange, the client requests a service." NEWKEYS is what advances the session to
// the service phase, so a request arriving before it is one that skipped the key exchange - and
// with it the host-key verification - and is refused.
static void test_s10_service_request_before_the_key_exchange_is_refused(void)
{
    const uint8_t svc[] = {SSH_MSG_SERVICE_REQUEST, 0, 0, 0, 12, 's', 's', 'h', '-', 'u',
                           's',                     'e', 'r', 'a', 'u', 't', 'h'};

    const uint8_t phases[] = {SSH_PHASE_BANNER, SSH_PHASE_KEXINIT, SSH_PHASE_DH_INIT, SSH_PHASE_NEWKEYS};
    for (size_t k = 0; k < sizeof(phases) / sizeof(phases[0]); k++)
    {
        ssh_sess[0].phase = (SshPhase)phases[k];
        g_out_len = 0;
        g_emits = 0;
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, svc, sizeof(svc)));
        TEST_ASSERT_EQUAL_INT(0, g_emits);                      // nothing was accepted
        TEST_ASSERT_NOT_EQUAL(SSH_PHASE_AUTH, ssh_sess[0].phase); // and it did not advance
    }
}

// In the service phase the request is answered and the session moves to authentication.
static void test_s10_service_request_after_the_key_exchange_is_accepted(void)
{
    ssh_sess[0].phase = SSH_PHASE_SERVICE;
    const uint8_t svc[] = {SSH_MSG_SERVICE_REQUEST, 0, 0, 0, 12, 's', 's', 'h', '-', 'u',
                           's',                     'e', 'r', 'a', 'u', 't', 'h'};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, svc, sizeof(svc)));
    TEST_ASSERT_EQUAL_INT(1, g_emits);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_SERVICE_ACCEPT, g_out[0]);
    TEST_ASSERT_EQUAL(SSH_PHASE_AUTH, ssh_sess[0].phase);
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 11 - Additional Messages
// ---------------------------------------------------------------------------

// sec 11.1: DISCONNECT "causes immediate termination of the connection", and "the recipient MUST NOT
// accept any data after receiving this message". The dispatcher reports the connection over.
static void test_s11_1_disconnect_terminates_the_connection(void)
{
    // reason code || description || language, the sec 11.1 layout.
    const uint8_t msg[] = {SSH_MSG_DISCONNECT, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0};
    ssh_sess[0].phase = SSH_PHASE_OPEN;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_DISCONNECT, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(0, g_emits); // a closing peer is not answered
}

// sec 11.2: IGNORE is understood and ignored "at any time". Understood means it is not an error;
// ignored means it draws no reply.
static void test_s11_2_ignore_is_understood_and_not_answered(void)
{
    const uint8_t msg[] = {SSH_MSG_IGNORE, 0, 0, 0, 3, 'a', 'b', 'c'};
    const uint8_t phases[] = {SSH_PHASE_KEXINIT, SSH_PHASE_SERVICE, SSH_PHASE_AUTH, SSH_PHASE_OPEN};
    for (size_t k = 0; k < sizeof(phases) / sizeof(phases[0]); k++)
    {
        ssh_sess[0].phase = (SshPhase)phases[k];
        g_emits = 0;
        TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_IGNORE, msg, sizeof(msg)));
        TEST_ASSERT_EQUAL_INT(0, g_emits);
    }
}

// sec 11.3: DEBUG "All implementations MUST understand this message, but they are allowed to ignore
// it." Understood and unanswered, in every phase.
static void test_s11_3_debug_is_understood_and_not_answered(void)
{
    // always_display || message || language tag.
    const uint8_t msg[] = {SSH_MSG_DEBUG, 1, 0, 0, 0, 2, 'h', 'i', 0, 0, 0, 0};
    const uint8_t phases[] = {SSH_PHASE_KEXINIT, SSH_PHASE_SERVICE, SSH_PHASE_AUTH, SSH_PHASE_OPEN};
    for (size_t k = 0; k < sizeof(phases) / sizeof(phases[0]); k++)
    {
        ssh_sess[0].phase = (SshPhase)phases[k];
        g_emits = 0;
        TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_DEBUG, msg, sizeof(msg)));
        TEST_ASSERT_EQUAL_INT(0, g_emits);
    }
}

// sec 11.4: an unrecognized message is answered with SSH_MSG_UNIMPLEMENTED carrying the "packet
// sequence number of rejected message". ssh_pkt_recv counts a packet before dispatch, so the
// rejected packet's number is one behind the receive counter.
static void test_s11_4_unrecognized_message_is_answered_with_unimplemented(void)
{
    ssh_sess[0].phase = SSH_PHASE_OPEN;
    ssh_pkt[0].seq_no_recv = 42; // this message was packet 41

    const uint8_t msg[] = {200, 0, 0, 0, 0}; // 200 is in no message-number table
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, 200, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(1, g_emits);
    TEST_ASSERT_EQUAL_size_t(5, g_out_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_UNIMPLEMENTED, g_out[0]);
    TEST_ASSERT_EQUAL_UINT32(41, be32(g_out + 1));
}

// "Such messages MUST be otherwise ignored": the unrecognized message does not move the session on.
static void test_s11_4_an_unrecognized_message_changes_nothing(void)
{
    ssh_sess[0].phase = SSH_PHASE_AUTH;
    ssh_sess[0].authed = PROTO_FALSE;
    const uint8_t msg[] = {201, 1, 2, 3};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, 201, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL(SSH_PHASE_AUTH, ssh_sess[0].phase);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// UNIMPLEMENTED is itself a recognized message. Answering it with another would put two peers in a
// loop, so it is understood and left unanswered.
static void test_s11_4_unimplemented_is_not_answered_with_unimplemented(void)
{
    ssh_sess[0].phase = SSH_PHASE_OPEN;
    const uint8_t msg[] = {SSH_MSG_UNIMPLEMENTED, 0, 0, 0, 7};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_UNIMPLEMENTED, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_INT(0, g_emits);
}

// ---------------------------------------------------------------------------
// RFC 4252 - what the dispatcher enforces on behalf of the authentication layer
// ---------------------------------------------------------------------------

// sec 5.1: "When SSH_MSG_USERAUTH_SUCCESS has been sent, any further authentication requests
// received after that SHOULD be silently ignored." Silently: accepted, unanswered, and not an error.
static void test_rfc4252_s5_1_userauth_after_success_is_silently_ignored(void)
{
    ssh_sess[0].phase = SSH_PHASE_OPEN; // where SUCCESS leaves an authenticated session
    const uint8_t req[] = {SSH_MSG_USERAUTH_REQUEST, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, req, sizeof(req)));
    TEST_ASSERT_EQUAL_INT(0, g_emits);
}

// sec 5.3: "After sending SSH_MSG_USERAUTH_SUCCESS, the server starts the requested service." Until
// then the RFC 4254 connection protocol is not running, so none of its messages may be acted on.
//
// The whole RFC 4254 sec 9 message range is listed rather than a sample: each has its own gate, and
// a gate dropped from any one of them is unauthenticated reach into that message's handler. A
// sample cannot see which one went missing.
static void test_rfc4252_s5_3_no_connection_message_is_served_before_auth(void)
{
    const uint8_t mts[] = {
        SSH_MSG_GLOBAL_REQUEST,        // 80
        SSH_MSG_CHANNEL_OPEN,          // 90
        SSH_MSG_CHANNEL_OPEN_CONFIRM,  // 91
        SSH_MSG_CHANNEL_OPEN_FAILURE,  // 92
        SSH_MSG_CHANNEL_WINDOW_ADJUST, // 93
        SSH_MSG_CHANNEL_DATA,          // 94
        SSH_MSG_CHANNEL_EXTENDED_DATA, // 95
        SSH_MSG_CHANNEL_EOF,           // 96
        SSH_MSG_CHANNEL_CLOSE,         // 97
        SSH_MSG_CHANNEL_REQUEST        // 98
    };
    for (size_t k = 0; k < sizeof(mts) / sizeof(mts[0]); k++)
    {
        ssh_sess[0].phase = SSH_PHASE_AUTH;
        ssh_sess[0].authed = PROTO_FALSE;
        g_emits = 0;
        uint8_t p[8] = {mts[k], 0, 0, 0, 0, 0, 0, 0};
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, mts[k], p, sizeof(p)));
        TEST_ASSERT_EQUAL_INT(0, g_emits);          // nothing is written back
        TEST_ASSERT_FALSE(ssh_sess[0].authed);      // and none of them authenticates
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_s10_service_request_before_the_key_exchange_is_refused);
    RUN_TEST(test_s10_service_request_after_the_key_exchange_is_accepted);
    RUN_TEST(test_s11_1_disconnect_terminates_the_connection);
    RUN_TEST(test_s11_2_ignore_is_understood_and_not_answered);
    RUN_TEST(test_s11_3_debug_is_understood_and_not_answered);
    RUN_TEST(test_s11_4_unrecognized_message_is_answered_with_unimplemented);
    RUN_TEST(test_s11_4_an_unrecognized_message_changes_nothing);
    RUN_TEST(test_s11_4_unimplemented_is_not_answered_with_unimplemented);
    RUN_TEST(test_rfc4252_s5_1_userauth_after_success_is_silently_ignored);
    RUN_TEST(test_rfc4252_s5_3_no_connection_message_is_served_before_auth);
    return UNITY_END();
}
