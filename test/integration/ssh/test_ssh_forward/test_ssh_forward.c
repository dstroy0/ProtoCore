// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for ssh/connection/ssh_forward.c - the remote-forward owner (ssh -R).
//
// ssh_channel.c parses RFC 4254 sec 7.1 off the wire and hands the request to a callback; this
// file is the owner behind that callback - it allocates the real listener, keeps the binding
// table, and tears both down. test_ssh_channel drives the codec with fake callbacks, so nothing
// until now exercised the owner. Here pc_ssh_forward_begin() installs the real ones and each case
// drives a hand-built sec 7.1 message through the codec into it.
//
// The oracle is RFC 4254 (rfc-editor.org/rfc/rfc4254.txt), quoted verbatim.
//
// sec 7.1, the request:
//
//   byte      SSH_MSG_GLOBAL_REQUEST
//   string    "tcpip-forward"
//   boolean   want reply
//   string    address to bind (e.g., "0.0.0.0")
//   uint32    port number to bind
//
//   "If a client passes 0 as port number to bind and has 'want reply' as TRUE, then the server
//    allocates the next available unprivileged port number and replies with the following
//    message; otherwise, there is no response-specific data."
//
//   byte     SSH_MSG_REQUEST_SUCCESS
//   uint32   port that was bound on the server
//
// sec 7.1, the cancel:
//
//   "A port forwarding can be canceled with the following message.  Note that channel open
//    requests may be received until a reply to this message is received."
//
//   byte      SSH_MSG_GLOBAL_REQUEST
//   string    "cancel-tcpip-forward"
//   boolean   want reply
//   string    address_to_bind (e.g., "127.0.0.1")
//   uint32    port number to bind
//
// sec 4 governs every reply above:
//
//   "The recipient will respond to this message with SSH_MSG_REQUEST_SUCCESS or
//    SSH_MSG_REQUEST_FAILURE if 'want reply' is TRUE."
//   "Usually, the 'response specific data' is non-existent."
//   "If the recipient does not recognize or support the request, it simply responds with
//    SSH_MSG_REQUEST_FAILURE."
//   "it is REQUIRED that replies to SSH_MSG_GLOBAL_REQUESTS MUST be sent in the same order as
//    the corresponding request messages."

#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/connection/ssh_forward.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/session/session.h"
#include "network_drivers/transport/tcp.h"
#include "network_drivers/transport/tcp/tcp_conn.h"
#include "network_drivers/transport/tcp/tcp_listener.h"
#include "network_drivers/transport/tcp_evt.h"
#include "server/clock/clock.h"
#include <stdint.h>
#include <string.h>

#include "rx_feed.h"
#include <unity.h>

#define SSH_SLOT 0

// conn_pool slots: the SSH control connection, and the socket accepted on the forwarded port.
#define SSH_CONN_SLOT 0
#define FWD_CONN_SLOT 1

// Unprivileged, so sec 7.1's "Implementations should only allow forwarding privileged ports if
// the user has been authenticated as a privileged user" never enters into these cases.
#define BIND_PORT 8022

static int policy_calls;
static proto_bool policy_answer;
static char policy_host[PC_SSH_FWD_HOST_MAX];
static uint16_t policy_port;

static proto_bool policy_cb(const char *host, uint16_t port)
{
    policy_calls++;
    policy_port = port;
    size_t n = strlen(host);
    if (n >= sizeof(policy_host))
    {
        n = sizeof(policy_host) - 1;
    }
    memcpy(policy_host, host, n);
    policy_host[n] = 0;
    return policy_answer;
}

void setUp()
{
    memset(listener_pool, 0, sizeof(Listener) * MAX_LISTENERS);
    pc_net_host_reset();

    // Session.tick sweeps idle slots before it dispatches, off the virtual clock. Fixing the clock
    // and stamping each slot keeps the elapsed time at 0, so the sweep never reaps mid-test.
    set_millis(1000);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = pc_net_host_pcb();
        conn_pool[i].proto_slot = PC_PROTO_SLOT_NONE;
        conn_pool[i].last_activity_ms = pc_millis();
    }
    conn_pool[SSH_CONN_SLOT].proto = PROTO_SSH;

    pc_ssh_conn_setup();
    pc_ssh_channel_init(SSH_SLOT);

    // The codec holds the owner's callbacks in file-scope state that outlives a test, so a prior
    // pc_ssh_forward_begin() would leave forwarding on. Clearing them restores "not yet begun".
    pc_ssh_channel_set_forward_open_cb(NULL);
    pc_ssh_channel_set_forward_data_cb(NULL);
    pc_ssh_channel_set_rforward_open_cb(NULL);
    pc_ssh_channel_set_rforward_cancel_cb(NULL);
    pc_ssh_channel_set_forward_confirm_cb(NULL);

    pc_ssh_forward_set_policy_cb(NULL);
    policy_calls = 0;
    policy_answer = PROTO_TRUE;
    policy_host[0] = 0;
    policy_port = 0;
    tcp_capture_reset();
}

void tearDown()
{
    pc_ssh_forward_reset(SSH_SLOT);
    pc_ssh_conn_close(SSH_CONN_SLOT);
}

// ---------------------------------------------------------------------------
// sec 7.1 / sec 4 message builders - field for field, in the order the RFC prints them
// ---------------------------------------------------------------------------

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static size_t put_string(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    put_u32(p, n);
    memcpy(p + 4, s, n);
    return 4 + n;
}

static size_t build_global_request(uint8_t *out, const char *name, proto_bool want_reply, const char *addr,
                                   uint16_t port)
{
    size_t o = 0;
    out[o++] = SSH_MSG_GLOBAL_REQUEST;
    o += put_string(out + o, name);
    out[o++] = want_reply ? 1 : 0;
    o += put_string(out + o, addr);
    put_u32(out + o, port);
    o += 4;
    return o;
}

static int request(const char *name, proto_bool want_reply, const char *addr, uint16_t port, uint8_t *reply,
                   size_t *reply_len)
{
    uint8_t msg[128];
    size_t msg_len = build_global_request(msg, name, want_reply, addr, port);
    *reply_len = 0;
    return ssh_global_request_handle(SSH_SLOT, msg, msg_len, reply, reply_len, 64);
}

// The listener slot bound to @p port, or -1: what "the owner opened a real listener" means.
static int listener_on(uint16_t port)
{
    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        if (listener_pool[i].active && listener_pool[i].port == port)
        {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Opt-in: no listener exists until the application turns forwarding on
// ---------------------------------------------------------------------------

// With no owner installed the request is one the recipient does not support, which sec 4 answers
// SSH_MSG_REQUEST_FAILURE. Nothing binds, so an unconfigured server is not an open relay.
static void test_s4_remote_forward_is_refused_until_begin(void)
{
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);
    TEST_ASSERT_EQUAL_INT(-1, listener_on(BIND_PORT));
}

// ---------------------------------------------------------------------------
// sec 7.1 "tcpip-forward"
// ---------------------------------------------------------------------------

// A named port binds, and the reply is the bare success of sec 4's "Usually, the 'response
// specific data' is non-existent" - the uint32 form is reserved for the port-0 case below.
static void test_s7_1_tcpip_forward_binds_the_requested_port(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);

    int li = listener_on(BIND_PORT);
    TEST_ASSERT_NOT_EQUAL_INT(-1, li);
    TEST_ASSERT_EQUAL_INT(PROTO_SSH_RFWD, listener_pool[li].proto);
}

// sec 4: a reply is sent only "if 'want reply' is TRUE". The forward is established either way.
static void test_s4_no_reply_when_want_reply_is_clear(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_FALSE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(0, reply_len);
    TEST_ASSERT_NOT_EQUAL_INT(-1, listener_on(BIND_PORT));
}

// sec 7.1 lets a server answer port 0 by allocating one and replying with the bound port. This
// owner does not allocate ephemeral ports, so the request is one it does not support, and sec 4
// answers that with SSH_MSG_REQUEST_FAILURE rather than binding a port the client never named.
static void test_s7_1_port_zero_is_refused_rather_than_allocated(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", 0, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);
    TEST_ASSERT_EQUAL_INT(-1, listener_on(0));
}

// The same port twice on one connection: the second is refused and the first listener stands.
static void test_s7_1_duplicate_bind_is_refused(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
    int first = listener_on(BIND_PORT);

    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);
    TEST_ASSERT_EQUAL_INT(first, listener_on(BIND_PORT));
}

// A bind the stack refuses is a refusal, not a half-open forward: no binding survives a listener
// that never came up, and the table row is free for the next attempt.
static void test_s7_1_a_bind_the_stack_refuses_leaves_no_binding(void)
{
    pc_ssh_forward_begin();
    mock_bind_fail_once();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);
    TEST_ASSERT_EQUAL_INT(-1, listener_on(BIND_PORT));

    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
    TEST_ASSERT_NOT_EQUAL_INT(-1, listener_on(BIND_PORT));
}

// sec 7.1 lists "", "0.0.0.0", "::", "localhost", "127.0.0.1" and "::1" as addresses with
// special-case semantics. Each is accepted; the owner keeps whichever arrived, because sec 7.2
// makes it the "address that was connected" echoed in every forwarded-tcpip channel open.
static void test_s7_1_each_special_case_bind_address_is_accepted(void)
{
    static const char *const addrs[] = {"", "0.0.0.0", "::", "localhost", "127.0.0.1", "::1"};
    for (int a = 0; a < 6; a++)
    {
        pc_ssh_forward_begin();
        uint8_t reply[64];
        size_t reply_len = 0;
        TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, addrs[a], BIND_PORT, reply, &reply_len));
        TEST_ASSERT_EQUAL_size_t(1, reply_len);
        TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
        TEST_ASSERT_NOT_EQUAL_INT(-1, listener_on(BIND_PORT));
        pc_ssh_forward_reset(SSH_SLOT);
    }
}

// ---------------------------------------------------------------------------
// sec 7.1 "cancel-tcpip-forward"
// ---------------------------------------------------------------------------

// The cancel names the same port; the listener stops and the reply is a bare success.
static void test_s7_1_cancel_tcpip_forward_stops_the_listener(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_NOT_EQUAL_INT(-1, listener_on(BIND_PORT));

    TEST_ASSERT_EQUAL_INT(0, request("cancel-tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
    TEST_ASSERT_EQUAL_INT(-1, listener_on(BIND_PORT));
}

// A cancel for a port this connection never bound is a request the recipient cannot satisfy.
static void test_s7_1_cancel_of_an_unbound_port_fails(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("cancel-tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);
}

// A cancel frees the table row, so the same port can be forwarded again afterwards.
static void test_s7_1_port_can_be_forwarded_again_after_a_cancel(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_INT(0, request("cancel-tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));

    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
    TEST_ASSERT_NOT_EQUAL_INT(-1, listener_on(BIND_PORT));
}

// ---------------------------------------------------------------------------
// sec 4 ordering, and requests the owner never sees
// ---------------------------------------------------------------------------

// "replies to SSH_MSG_GLOBAL_REQUESTS MUST be sent in the same order as the corresponding request
// messages": a bind, an unsupported port-0 bind, and a cancel produce success, failure, success.
static void test_s4_replies_are_in_request_order(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;

    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);

    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", 0, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);

    TEST_ASSERT_EQUAL_INT(0, request("cancel-tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
}

// sec 4: a request name the recipient does not recognize is answered SSH_MSG_REQUEST_FAILURE, and
// it reaches no owner and binds nothing.
static void test_s4_unknown_request_name_never_binds(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(
        0, request("no-more-sessions@openssh.com", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_size_t(1, reply_len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);
    TEST_ASSERT_EQUAL_INT(-1, listener_on(BIND_PORT));
}

// ---------------------------------------------------------------------------
// The forward does not outlive the connection that asked for it
// ---------------------------------------------------------------------------

// The SSH connection going away takes its listeners and its bindings with it.
static void test_s7_1_reset_stops_the_listener_the_connection_owned(void)
{
    pc_ssh_forward_begin();
    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_NOT_EQUAL_INT(-1, listener_on(BIND_PORT));

    pc_ssh_forward_reset(SSH_SLOT);
    TEST_ASSERT_EQUAL_INT(-1, listener_on(BIND_PORT));

    TEST_ASSERT_EQUAL_INT(0, request("cancel-tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_FAILURE, reply[0]);
}

// The direct-tcpip target policy is the -L half. sec 7.1 carries no target host for a policy to
// judge, only an address and port to bind, so a -R request does not consult it.
static void test_s7_1_policy_does_not_gate_a_remote_forward(void)
{
    pc_ssh_forward_begin();
    pc_ssh_forward_set_policy_cb(policy_cb);
    policy_answer = PROTO_FALSE;

    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, "0.0.0.0", BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
    TEST_ASSERT_EQUAL_INT(0, policy_calls);
}

// ---------------------------------------------------------------------------
// sec 7.2 "forwarded-tcpip" - the channel opened when a connection arrives
//
//   "When a connection comes to a port for which remote forwarding has been requested, a channel
//    is opened to forward the port to the other side."
//
//   byte      SSH_MSG_CHANNEL_OPEN
//   string    "forwarded-tcpip"
//   uint32    sender channel
//   uint32    initial window size
//   uint32    maximum packet size
//   string    address that was connected
//   uint32    port that was connected
//   string    originator IP address
//   uint32    originator port
// ---------------------------------------------------------------------------

// A cursor over the payload of the one SSH packet the server framed. RFC 4253 sec 6 with the
// "none" cipher: uint32 packet_length || byte padding_length || payload || padding.
typedef struct
{
    const uint8_t *p;
    size_t len;
    size_t off;
} Cursor;

// Payload of packet @p want in the capture, counting from 0.
static Cursor payload_at(int want)
{
    size_t n = 0;
    const uint8_t *w = pc_net_host_sent(&n);
    Cursor c;
    c.p = NULL;
    c.len = 0;
    c.off = 0;

    size_t at = 0;
    for (int i = 0; at + 6 <= n; i++)
    {
        uint32_t pkt_len =
            ((uint32_t)w[at] << 24) | ((uint32_t)w[at + 1] << 16) | ((uint32_t)w[at + 2] << 8) | w[at + 3];
        uint8_t pad = w[at + 4];
        if (pkt_len < (uint32_t)pad + 1u || at + 4u + pkt_len > n)
        {
            return c;
        }
        if (i == want)
        {
            c.p = w + at + 5;
            c.len = pkt_len - pad - 1u;
            return c;
        }
        at += 4u + pkt_len;
    }
    return c;
}

static Cursor captured_payload(void)
{
    return payload_at(0);
}

static uint32_t take_u32(Cursor *c)
{
    TEST_ASSERT_TRUE(c->off + 4 <= c->len);
    uint32_t v = ((uint32_t)c->p[c->off] << 24) | ((uint32_t)c->p[c->off + 1] << 16) |
                 ((uint32_t)c->p[c->off + 2] << 8) | c->p[c->off + 3];
    c->off += 4;
    return v;
}

static void expect_string(Cursor *c, const char *want)
{
    uint32_t n = take_u32(c);
    TEST_ASSERT_EQUAL_size_t(strlen(want), n);
    TEST_ASSERT_TRUE(c->off + n <= c->len);
    TEST_ASSERT_EQUAL_MEMORY(want, c->p + c->off, n);
    c->off += n;
}

// Bind the forward, stage the accepted socket on the forwarded port, and let one Session.tick
// dispatch EVT_CONNECT into the owner's accept handler. Returns the listener slot.
static int accept_on_forwarded_port(const char *bind_addr, const char *originator)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    uint8_t reply[64];
    size_t reply_len = 0;
    TEST_ASSERT_EQUAL_INT(0, request("tcpip-forward", PROTO_TRUE, bind_addr, BIND_PORT, reply, &reply_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_REQUEST_SUCCESS, reply[0]);
    int li = listener_on(BIND_PORT);
    TEST_ASSERT_NOT_EQUAL_INT(-1, li);

    conn_pool[FWD_CONN_SLOT].proto = PROTO_SSH_RFWD;
    conn_pool[FWD_CONN_SLOT].listener_id = (uint8_t)li;
    pc_net_ip_parse(originator, &conn_pool[FWD_CONN_SLOT].pcb->remote_ip);

    tcp_capture_reset(); // drop the banner the SSH accept queued; keep only the channel open

    TcpEvt evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EVT_CONNECT;
    evt.slot_id = FWD_CONN_SLOT;
    TEST_ASSERT_TRUE(Tcp.listener->enqueue((uint8_t)li, &evt));
    Session.tick(0);
    return li;
}

// Put bytes in the accepted socket's rx ring and dispatch the EVT_DATA that drains it. The -R
// bridge pumps from the data handler; pc_ssh_forward_pump walks the direct-tcpip table instead.
static void deliver_socket_data(int li, const char *s)
{
    push_str(FWD_CONN_SLOT, s);
    TcpEvt evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EVT_DATA;
    evt.slot_id = FWD_CONN_SLOT;
    TEST_ASSERT_TRUE(Tcp.listener->enqueue((uint8_t)li, &evt));
    Session.tick(0);
}

// Every sec 7.2 field, in the order the RFC prints them.
static void test_s7_2_accept_opens_a_forwarded_tcpip_channel(void)
{
    accept_on_forwarded_port("127.0.0.1", "192.168.1.50");

    Cursor c = captured_payload();
    TEST_ASSERT_NOT_NULL(c.p);
    TEST_ASSERT_TRUE(c.len > 0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN, c.p[0]);
    c.off = 1;

    expect_string(&c, "forwarded-tcpip");
    uint32_t sender = take_u32(&c);
    TEST_ASSERT_TRUE(sender < PC_SSH_MAX_CHANNELS);
    TEST_ASSERT_TRUE(take_u32(&c) > 0); // initial window size
    TEST_ASSERT_TRUE(take_u32(&c) > 0); // maximum packet size

    // "address that was connected" / "port that was connected": the forward's own bind address
    // and port, which is what the client matches the channel against.
    expect_string(&c, "127.0.0.1");
    TEST_ASSERT_EQUAL_UINT32(BIND_PORT, take_u32(&c));

    // "originator IP address" is the peer that connected to the forwarded port.
    expect_string(&c, "192.168.1.50");

    // "originator port": the transport does not surface the peer port, so it is reported as 0.
    TEST_ASSERT_EQUAL_UINT32(0, take_u32(&c));
    TEST_ASSERT_EQUAL_size_t(c.len, c.off); // no trailing bytes
}

// sec 7.1 lets a client bind "" for all protocol families. The channel still needs an "address
// that was connected", so the owner reports the IPv4 wildcard.
static void test_s7_2_empty_bind_address_is_reported_as_wildcard(void)
{
    accept_on_forwarded_port("", "10.0.0.9");

    Cursor c = captured_payload();
    TEST_ASSERT_NOT_NULL(c.p);
    c.off = 1;
    expect_string(&c, "forwarded-tcpip");
    (void)take_u32(&c);
    (void)take_u32(&c);
    (void)take_u32(&c);
    expect_string(&c, "0.0.0.0");
    TEST_ASSERT_EQUAL_UINT32(BIND_PORT, take_u32(&c));
}

// A connection on a listener no binding owns opens no channel and the socket is dropped.
static void test_s7_2_accept_with_no_binding_opens_no_channel(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    // An active listener the forward owner never bound.
    listener_pool[0].active = PROTO_TRUE;
    listener_pool[0].port = BIND_PORT;
    listener_pool[0].proto = PROTO_SSH_RFWD;
    listener_pool[0].queue = pc_platform_queue_create(8, sizeof(TcpEvt), &listener_pool[0], NULL);
    conn_pool[FWD_CONN_SLOT].proto = PROTO_SSH_RFWD;
    conn_pool[FWD_CONN_SLOT].listener_id = 0;
    tcp_capture_reset();

    TcpEvt evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EVT_CONNECT;
    evt.slot_id = FWD_CONN_SLOT;
    Tcp.listener->enqueue(0, &evt);
    Session.tick(0);

    size_t n = 0;
    (void)pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

// sec 5.1: the confirmation carries our recipient id and the peer's sender id. Once it lands the
// bridge is live, and bytes arriving on the accepted socket reach the client as channel data.
static void test_s5_1_open_confirmation_starts_the_byte_bridge(void)
{
    int li = accept_on_forwarded_port("127.0.0.1", "192.168.1.50");
    Cursor c = captured_payload();
    TEST_ASSERT_NOT_NULL(c.p);
    c.off = 1;
    expect_string(&c, "forwarded-tcpip");
    uint32_t ours = take_u32(&c);

    //   byte SSH_MSG_CHANNEL_OPEN_CONFIRMATION || recipient || sender || window || max packet
    uint8_t confirm[17];
    confirm[0] = SSH_MSG_CHANNEL_OPEN_CONFIRM;
    put_u32(confirm + 1, ours);
    put_u32(confirm + 5, 77);      // the peer's channel id
    put_u32(confirm + 9, 32768);   // its initial window
    put_u32(confirm + 13, 16384);  // its maximum packet size
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open_confirm(SSH_SLOT, confirm, sizeof(confirm)));

    SshChannel *ch = pc_ssh_chan_by_id(SSH_SLOT, ours);
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_TRUE(ch->open);
    TEST_ASSERT_EQUAL_UINT32(77, ch->peer_id);

    // Bytes off the forwarded socket now reach the client on that channel.
    tcp_capture_reset();
    deliver_socket_data(li, "hello");

    Cursor d = captured_payload();
    TEST_ASSERT_NOT_NULL(d.p);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_DATA, d.p[0]);
    d.off = 1;
    TEST_ASSERT_EQUAL_UINT32(77, take_u32(&d)); // addressed to the peer's channel id
    uint32_t dlen = take_u32(&d);
    TEST_ASSERT_EQUAL_size_t(5, dlen);
    TEST_ASSERT_EQUAL_MEMORY("hello", d.p + d.off, 5);
}

// sec 5.1: a client that refuses the channel gets no bridge, and the accepted socket is dropped
// rather than left holding bytes nothing will carry.
static void test_s5_1_open_failure_drops_the_accepted_socket(void)
{
    int li = accept_on_forwarded_port("127.0.0.1", "192.168.1.50");
    Cursor c = captured_payload();
    TEST_ASSERT_NOT_NULL(c.p);
    c.off = 1;
    expect_string(&c, "forwarded-tcpip");
    uint32_t ours = take_u32(&c);

    //   byte SSH_MSG_CHANNEL_OPEN_FAILURE || recipient || reason || description || language tag
    uint8_t fail[64];
    size_t o = 0;
    fail[o++] = SSH_MSG_CHANNEL_OPEN_FAILURE;
    put_u32(fail + o, ours);
    o += 4;
    put_u32(fail + o, 1u); // SSH_OPEN_ADMINISTRATIVELY_PROHIBITED
    o += 4;
    o += put_string(fail + o, "refused");
    o += put_string(fail + o, "");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open_failure(SSH_SLOT, fail, o));

    TEST_ASSERT_NULL(pc_ssh_chan_by_id(SSH_SLOT, ours)); // the pending channel is freed

    // Nothing bridges any more: bytes on the socket produce no channel data.
    tcp_capture_reset();
    deliver_socket_data(li, "hello");
    size_t n = 0;
    (void)pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

// sec 7.2: "Implementations MUST reject these messages unless they have previously requested a
// remote TCP/IP port forwarding with the given port number." This end grants remote forwards, it
// never requests one, so every inbound forwarded-tcpip open is rejected - and sec 5.1 fixes the
// code for a type the recipient does not support at SSH_OPEN_UNKNOWN_CHANNEL_TYPE (3).
static void test_s7_2_inbound_forwarded_tcpip_open_is_rejected(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    uint8_t msg[128];
    size_t o = 0;
    msg[o++] = SSH_MSG_CHANNEL_OPEN;
    o += put_string(msg + o, "forwarded-tcpip");
    put_u32(msg + o, 9); // sender channel
    o += 4;
    put_u32(msg + o, 32768); // initial window size
    o += 4;
    put_u32(msg + o, 16384); // maximum packet size
    o += 4;
    o += put_string(msg + o, "127.0.0.1"); // address that was connected
    put_u32(msg + o, BIND_PORT);
    o += 4;
    o += put_string(msg + o, "10.0.0.1"); // originator IP address
    put_u32(msg + o, 1234);
    o += 4;

    uint8_t out[128];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open(SSH_SLOT, msg, o, out, &out_len, sizeof(out)));
    TEST_ASSERT_TRUE(out_len >= 9);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_FAILURE, out[0]);

    Cursor c;
    c.p = out;
    c.len = out_len;
    c.off = 1;
    TEST_ASSERT_EQUAL_UINT32(9, take_u32(&c)); // recipient channel: the sender id it named
    TEST_ASSERT_EQUAL_UINT32(3, take_u32(&c)); // SSH_OPEN_UNKNOWN_CHANNEL_TYPE
}

// sec 7.2: "Forwarded TCP/IP channels are independent of any sessions, and closing a session
// channel does not in any way imply that forwarded connections should be closed."
static void test_s7_2_forwards_are_independent_of_sessions(void)
{
    int li = accept_on_forwarded_port("127.0.0.1", "192.168.1.50");
    Cursor c = captured_payload();
    TEST_ASSERT_NOT_NULL(c.p);
    c.off = 1;
    expect_string(&c, "forwarded-tcpip");
    uint32_t fwd_ch = take_u32(&c);

    uint8_t confirm[17];
    confirm[0] = SSH_MSG_CHANNEL_OPEN_CONFIRM;
    put_u32(confirm + 1, fwd_ch);
    put_u32(confirm + 5, 77);
    put_u32(confirm + 9, 32768);
    put_u32(confirm + 13, 16384);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open_confirm(SSH_SLOT, confirm, sizeof(confirm)));

    // A separate "session" channel, opened and then closed by the client.
    uint8_t open[64];
    size_t o = 0;
    open[o++] = SSH_MSG_CHANNEL_OPEN;
    o += put_string(open + o, "session");
    put_u32(open + o, 5);
    o += 4;
    put_u32(open + o, 32768);
    o += 4;
    put_u32(open + o, 16384);
    o += 4;
    uint8_t out[128];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open(SSH_SLOT, open, o, out, &out_len, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]);
    uint32_t sess_ch = ((uint32_t)out[5] << 24) | ((uint32_t)out[6] << 16) | ((uint32_t)out[7] << 8) | out[8];
    TEST_ASSERT_NOT_EQUAL_UINT32(fwd_ch, sess_ch);

    uint8_t close[5];
    close[0] = SSH_MSG_CHANNEL_CLOSE;
    put_u32(close + 1, sess_ch);
    out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_close(SSH_SLOT, close, sizeof(close), out, &out_len, sizeof(out)));

    // The forward survives it: the listener still stands and the channel is still open.
    TEST_ASSERT_NOT_EQUAL_INT(-1, listener_on(BIND_PORT));
    SshChannel *fwd = pc_ssh_chan_by_id(SSH_SLOT, fwd_ch);
    TEST_ASSERT_NOT_NULL(fwd);
    TEST_ASSERT_TRUE(fwd->open);

    // And it still carries bytes.
    tcp_capture_reset();
    deliver_socket_data(li, "still here");
    Cursor d = captured_payload();
    TEST_ASSERT_NOT_NULL(d.p);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_DATA, d.p[0]);
}

// sec 5.3: EOF is what a party sends "when a party will no longer send more data to a channel",
// and CLOSE is what it sends "when either party wishes to terminate the channel". The forwarded
// socket closing produces both, EOF first, each naming the peer's channel id.
static void test_s5_3_socket_close_sends_eof_then_close(void)
{
    int li = accept_on_forwarded_port("127.0.0.1", "192.168.1.50");
    Cursor c = captured_payload();
    TEST_ASSERT_NOT_NULL(c.p);
    c.off = 1;
    expect_string(&c, "forwarded-tcpip");
    uint32_t ours = take_u32(&c);

    uint8_t confirm[17];
    confirm[0] = SSH_MSG_CHANNEL_OPEN_CONFIRM;
    put_u32(confirm + 1, ours);
    put_u32(confirm + 5, 77);
    put_u32(confirm + 9, 32768);
    put_u32(confirm + 13, 16384);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open_confirm(SSH_SLOT, confirm, sizeof(confirm)));

    tcp_capture_reset();
    TcpEvt evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EVT_DISCONNECT;
    evt.slot_id = FWD_CONN_SLOT;
    TEST_ASSERT_TRUE(Tcp.listener->enqueue((uint8_t)li, &evt));
    Session.tick(0);

    Cursor eof = payload_at(0);
    TEST_ASSERT_NOT_NULL(eof.p);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_EOF, eof.p[0]);
    eof.off = 1;
    TEST_ASSERT_EQUAL_UINT32(77, take_u32(&eof));

    Cursor cls = payload_at(1);
    TEST_ASSERT_NOT_NULL(cls.p);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_CLOSE, cls.p[0]);
    cls.off = 1;
    TEST_ASSERT_EQUAL_UINT32(77, take_u32(&cls));
}

// ---------------------------------------------------------------------------
// sec 7.2 "direct-tcpip" - the local-forward half (ssh -L)
//
//   byte      SSH_MSG_CHANNEL_OPEN
//   string    "direct-tcpip"
//   uint32    sender channel
//   uint32    initial window size
//   uint32    maximum packet size
//   string    host to connect
//   uint32    port to connect
//   string    originator IP address
//   uint32    originator port
//
//   "The 'host to connect' and 'port to connect' specify the TCP/IP host and port where the
//    recipient should connect the channel.  The 'host to connect' may be either a domain name or
//    a numeric IP address."
//
// sec 5.1 fixes the refusal codes:
//
//    SSH_OPEN_ADMINISTRATIVELY_PROHIBITED          1
//    SSH_OPEN_CONNECT_FAILED                       2
//    SSH_OPEN_UNKNOWN_CHANNEL_TYPE                 3
//    SSH_OPEN_RESOURCE_SHORTAGE                    4
// ---------------------------------------------------------------------------

#define DIRECT_PORT 2222

static size_t build_direct_tcpip_open(uint8_t *out, uint32_t sender, const char *host, uint16_t port)
{
    size_t o = 0;
    out[o++] = SSH_MSG_CHANNEL_OPEN;
    o += put_string(out + o, "direct-tcpip");
    put_u32(out + o, sender);
    o += 4;
    put_u32(out + o, 32768); // initial window size
    o += 4;
    put_u32(out + o, 16384); // maximum packet size
    o += 4;
    o += put_string(out + o, host); // host to connect
    put_u32(out + o, port);
    o += 4;
    o += put_string(out + o, "10.0.0.7"); // originator IP address
    put_u32(out + o, 4321);               // originator port
    o += 4;
    return o;
}

static int direct_open(uint32_t sender, const char *host, uint16_t port, uint8_t *out, size_t *out_len)
{
    uint8_t msg[256];
    size_t n = build_direct_tcpip_open(msg, sender, host, port);
    *out_len = 0;
    return pc_ssh_channel_handle_open(SSH_SLOT, msg, n, out, out_len, 256);
}

// The reason code in a CHANNEL_OPEN_FAILURE (sec 5.1 layout: recipient then reason).
static uint32_t failure_reason(const uint8_t *out, size_t out_len)
{
    TEST_ASSERT_TRUE(out_len >= 9);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_FAILURE, out[0]);
    Cursor c;
    c.p = out;
    c.len = out_len;
    c.off = 5; // past the message byte and the recipient channel
    return take_u32(&c);
}

// The host pcb the client transport dialed to @p port, or NULL.
static pc_pcb *dialed_pcb(uint16_t port)
{
    for (int i = 0; i < PC_NET_HOST_PCBS; i++)
    {
        if (pc_net_host_pcbs[i].in_use && pc_net_host_pcbs[i].remote_port == port)
        {
            return &pc_net_host_pcbs[i];
        }
    }
    return NULL;
}

// Forwarding is off until the application asks for it, so the codec has no owner to consult and
// refuses. sec 5.1 makes that SSH_OPEN_ADMINISTRATIVELY_PROHIBITED.
static void test_s5_1_direct_tcpip_is_prohibited_until_begin(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, "127.0.0.1", DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT32(1, failure_reason(out, out_len));
    TEST_ASSERT_NULL(dialed_pcb(DIRECT_PORT)); // nothing was dialed
}

// "the TCP/IP host and port where the recipient should connect the channel": the owner dials
// exactly what the request named, and the channel is confirmed.
static void test_s7_2_direct_tcpip_connects_to_the_named_host_and_port(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, "127.0.0.1", DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]);
    TEST_ASSERT_NOT_NULL(dialed_pcb(DIRECT_PORT));
}

// The policy hook is handed the host and port the sec 7.2 string and uint32 carried, with the
// host NUL-terminated.
static void test_s7_2_the_policy_sees_the_host_and_port_from_the_wire(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();
    pc_ssh_forward_set_policy_cb(policy_cb);
    policy_answer = PROTO_TRUE;

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, "127.0.0.1", DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]);
    TEST_ASSERT_EQUAL_INT(1, policy_calls);
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", policy_host);
    TEST_ASSERT_EQUAL_UINT16(DIRECT_PORT, policy_port);
}

// A denied target opens nothing and dials nothing.
static void test_s7_2_a_denied_target_is_not_dialed(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();
    pc_ssh_forward_set_policy_cb(policy_cb);
    policy_answer = PROTO_FALSE;

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, "127.0.0.1", DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_FAILURE, out[0]);
    TEST_ASSERT_EQUAL_INT(1, policy_calls);
    TEST_ASSERT_NULL(dialed_pcb(DIRECT_PORT));

    // The codec maps every owner refusal to one code, so an administrative denial is reported as
    // SSH_OPEN_CONNECT_FAILED (2) rather than SSH_OPEN_ADMINISTRATIVELY_PROHIBITED (1).
    TEST_ASSERT_EQUAL_UINT32(2, failure_reason(out, out_len));
}

// A target that refuses the connection. sec 5.1 has SSH_OPEN_CONNECT_FAILED (2) for exactly this,
// but the client transport is non-blocking: pc_client_open hands back a connection id before the
// connect settles, so the owner has nothing to refuse on and the channel is confirmed. The failure
// reaches the client afterwards as EOF + CLOSE on a channel it was just told was open.
static void test_s7_2_a_refused_connect_is_reported_after_the_confirmation(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();
    mock_connect_fail_once();

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, "127.0.0.1", DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]); // not the sec 5.1 refusal

    tcp_capture_reset();
    pc_ssh_forward_pump(SSH_SLOT);

    Cursor eof = payload_at(0);
    TEST_ASSERT_NOT_NULL(eof.p);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_EOF, eof.p[0]);
    Cursor cls = payload_at(1);
    TEST_ASSERT_NOT_NULL(cls.p);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_CLOSE, cls.p[0]);
}

// "The 'host to connect' may be either a domain name or a numeric IP address", so the field is
// variable length. The owner copies it into a fixed buffer and refuses anything that will not fit
// rather than truncating to a different host.
static void test_s7_2_an_oversized_host_is_refused(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    char host[PC_SSH_FWD_HOST_MAX + 8];
    memset(host, 'a', sizeof(host) - 1);
    host[sizeof(host) - 1] = 0;

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, host, DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_FAILURE, out[0]);
    TEST_ASSERT_NULL(dialed_pcb(DIRECT_PORT));
}

// An empty 'host to connect' names no target.
static void test_s7_2_an_empty_host_is_refused(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, "", DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_FAILURE, out[0]);
}

// Channel data on a direct-tcpip channel is written to the forwarded socket, not surfaced as
// session data.
static void test_s7_2_channel_data_reaches_the_forwarded_socket(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, direct_open(4, "127.0.0.1", DIRECT_PORT, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]);
    uint32_t ours = ((uint32_t)out[5] << 24) | ((uint32_t)out[6] << 16) | ((uint32_t)out[7] << 8) | out[8];

    //   byte SSH_MSG_CHANNEL_DATA || uint32 recipient channel || string data   (sec 5.2)
    uint8_t data[64];
    size_t o = 0;
    data[o++] = SSH_MSG_CHANNEL_DATA;
    put_u32(data + o, ours);
    o += 4;
    o += put_string(data + o, "forwarded");

    tcp_capture_reset();
    size_t rlen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_data(SSH_SLOT, data, o, out, &rlen, sizeof(out)));

    size_t n = 0;
    const uint8_t *sent = pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(9, n); // straight onto the socket, no SSH framing
    TEST_ASSERT_EQUAL_MEMORY("forwarded", sent, 9);
}

// PC_SSH_FWD_MAX forwards fit; the next one has nowhere to go. The owner refuses it, which sec 5.1
// would call SSH_OPEN_RESOURCE_SHORTAGE - the codec reports the owner's refusal as 2 instead.
static void test_s5_1_a_full_forward_table_is_refused(void)
{
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_forward_begin();

    uint8_t out[256];
    size_t out_len = 0;
    for (int k = 0; k < PC_SSH_FWD_MAX; k++)
    {
        TEST_ASSERT_EQUAL_INT(0, direct_open((uint32_t)(10 + k), "127.0.0.1", (uint16_t)(DIRECT_PORT + k), out,
                                             &out_len));
        TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]);
    }
    TEST_ASSERT_EQUAL_INT(0, direct_open(99, "127.0.0.1", 3333, out, &out_len));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_FAILURE, out[0]);
    TEST_ASSERT_NULL(dialed_pcb(3333));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_s4_remote_forward_is_refused_until_begin);
    RUN_TEST(test_s7_1_tcpip_forward_binds_the_requested_port);
    RUN_TEST(test_s4_no_reply_when_want_reply_is_clear);
    RUN_TEST(test_s7_1_port_zero_is_refused_rather_than_allocated);
    RUN_TEST(test_s7_1_duplicate_bind_is_refused);
    RUN_TEST(test_s7_1_a_bind_the_stack_refuses_leaves_no_binding);
    RUN_TEST(test_s7_1_each_special_case_bind_address_is_accepted);
    RUN_TEST(test_s7_1_cancel_tcpip_forward_stops_the_listener);
    RUN_TEST(test_s7_1_cancel_of_an_unbound_port_fails);
    RUN_TEST(test_s7_1_port_can_be_forwarded_again_after_a_cancel);
    RUN_TEST(test_s4_replies_are_in_request_order);
    RUN_TEST(test_s4_unknown_request_name_never_binds);
    RUN_TEST(test_s7_1_reset_stops_the_listener_the_connection_owned);
    RUN_TEST(test_s7_1_policy_does_not_gate_a_remote_forward);
    RUN_TEST(test_s7_2_accept_opens_a_forwarded_tcpip_channel);
    RUN_TEST(test_s7_2_empty_bind_address_is_reported_as_wildcard);
    RUN_TEST(test_s7_2_accept_with_no_binding_opens_no_channel);
    RUN_TEST(test_s5_1_open_confirmation_starts_the_byte_bridge);
    RUN_TEST(test_s5_1_open_failure_drops_the_accepted_socket);
    RUN_TEST(test_s7_2_inbound_forwarded_tcpip_open_is_rejected);
    RUN_TEST(test_s7_2_forwards_are_independent_of_sessions);
    RUN_TEST(test_s5_3_socket_close_sends_eof_then_close);
    RUN_TEST(test_s5_1_direct_tcpip_is_prohibited_until_begin);
    RUN_TEST(test_s7_2_direct_tcpip_connects_to_the_named_host_and_port);
    RUN_TEST(test_s7_2_the_policy_sees_the_host_and_port_from_the_wire);
    RUN_TEST(test_s7_2_a_denied_target_is_not_dialed);
    RUN_TEST(test_s7_2_a_refused_connect_is_reported_after_the_confirmation);
    RUN_TEST(test_s7_2_an_oversized_host_is_refused);
    RUN_TEST(test_s7_2_an_empty_host_is_refused);
    RUN_TEST(test_s7_2_channel_data_reaches_the_forwarded_socket);
    RUN_TEST(test_s5_1_a_full_forward_table_is_refused);
    return UNITY_END();
}
