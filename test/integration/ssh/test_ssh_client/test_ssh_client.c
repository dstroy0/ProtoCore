// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for ssh/ssh_client.c - the outbound SSH client and reverse tunnel.
//
// The device is the client here: it dials a relay, handshakes, authenticates with its own key, and
// asks the relay to forward a port back. The sections below are the ones this file is responsible
// for, taken from its declared surface; what each requires is taken from the RFC, quoted at the
// check. Whether the implementation satisfies them is what these cases determine.
//
// RFC 4253 sec 4.2, Protocol Version Exchange:
//   "When the connection has been established, both sides MUST send an identification string.
//    This identification string MUST be
//        SSH-protoversion-softwareversion SP comments CR LF
//    ... the 'protoversion' MUST be '2.0' ... The identification MUST be terminated by a single
//    Carriage Return (CR) and a single Line Feed (LF) character ... The null character MUST NOT be
//    sent.  The maximum length of the string is 255 characters, including the Carriage Return and
//    Line Feed."
//   "The server MAY send other lines of data before sending the version string.  Each line SHOULD
//    be terminated by a Carriage Return and Line Feed.  Such lines MUST NOT begin with 'SSH-' ...
//    Clients MUST be able to process such lines."
//   "Key exchange will begin immediately after sending this identifier."
//
// RFC 4253 sec 8, Diffie-Hellman Key Exchange, step 3:
//   "C verifies that K_S really is the host key for S (e.g., using certificates or a local
//    database)."
//
// The relay side is a mock: bytes the client writes are read out of the host transport capture, and
// the relay's answers are delivered into the client's socket. A complete authenticated session
// needs a real server to sign the exchange hash, which test/servers/cyclone_ssh drives with a real
// peer; what is reachable here is everything up to and including the host-key decision.

#include "network_drivers/presentation/ssh/ssh_client.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"    // the SSH_MSG_* numbers
#include "network_drivers/presentation/ssh/transport/ssh_transport.h" // the host-key availability the parse consults
#include "network_drivers/transport/tcp.h"
#include "shared_primitives/log.h" // the sink the client's own failure reason arrives on
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#define RELAY_PORT 2222

// A pin the relay will not match: the cases that exercise sec 8 supply a host key of their own.
static const uint8_t PIN_ZEROS[32] = {0};
static const uint8_t AUTH_SEED[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

static pc_ssh_tunnel_cfg base_cfg(void)
{
    pc_ssh_tunnel_cfg c;
    memset(&c, 0, sizeof(c));
    c.host = "127.0.0.1";
    c.port = RELAY_PORT;
    c.user = "device";
    c.auth_seed = AUTH_SEED;
    c.host_pin = PIN_ZEROS;
    c.bind_addr = "";
    c.bind_port = 8080;
    c.local_port = 80;
    return c;
}

// Every line the client logs, reported with the case that produced it. cli_fail() writes its reason
// here, so a handshake that stops says which check stopped it.
static char last_log[160];

static void log_sink(uint8_t level, const char *line)
{
    (void)level;
    size_t n = strlen(line);
    if (n >= sizeof(last_log))
    {
        n = sizeof(last_log) - 1;
    }
    memcpy(last_log, line, n);
    last_log[n] = '\0';
    TEST_MESSAGE(line);
}

void setUp()
{
    pc_ssh_tunnel_end(); // a prior case's tunnel must not decide this one
    pc_net_host_reset();
    tcp_capture_reset();
    last_log[0] = '\0';
    pc_log_set_sink(log_sink);
}

void tearDown()
{
    pc_ssh_tunnel_end();
}

// The socket the client dialed to the relay.
static pc_pcb *relay_socket(void)
{
    for (int i = 0; i < PC_NET_HOST_PCBS; i++)
    {
        if (pc_net_host_pcbs[i].in_use && pc_net_host_pcbs[i].remote_port == RELAY_PORT)
        {
            return &pc_net_host_pcbs[i];
        }
    }
    return NULL;
}

// Hand @p n bytes to the client as if the relay had sent them.
static void relay_sends(const void *bytes, size_t n)
{
    pc_pcb *p = relay_socket();
    TEST_ASSERT_NOT_NULL(p);
    static uint8_t buf[2048];
    memcpy(buf, bytes, n);
    pc_net_host_deliver(p, buf, (uint16_t)n);
}

static void relay_sends_str(const char *s)
{
    relay_sends(s, strlen(s));
}

static size_t put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return 4;
}

static size_t put_namelist(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    put_u32(p, n);
    memcpy(p + 4, s, n);
    return 4 + n;
}

// RFC 4253 sec 6, unencrypted: uint32 packet_length | byte padding_length | payload | padding,
// the whole run a multiple of 8 with at least four bytes of padding.
static void relay_sends_packet(const uint8_t *payload, size_t plen)
{
    uint8_t wire[2048];
    size_t total = 5 + plen;
    size_t pad = 8 - (total % 8);
    if (pad < 4)
    {
        pad += 8;
    }
    size_t pkt = 1 + plen + pad;
    put_u32(wire, (uint32_t)pkt);
    wire[4] = (uint8_t)pad;
    memcpy(wire + 5, payload, plen);
    memset(wire + 5 + plen, 0, pad);
    relay_sends(wire, 4 + pkt);
}

// RFC 4253 sec 7.1: byte SSH_MSG_KEXINIT | 16-byte cookie | ten name-lists | boolean
// first_kex_packet_follows | uint32 reserved.
static void relay_sends_kexinit(const char *kex, const char *hostkey, const char *cipher, const char *mac)
{
    uint8_t p[512];
    size_t o = 0;
    p[o++] = SSH_MSG_KEXINIT;
    for (int k = 0; k < 16; k++)
    {
        p[o++] = (uint8_t)(0xA0 + k);
    }
    o += put_namelist(p + o, kex);
    o += put_namelist(p + o, hostkey);
    o += put_namelist(p + o, cipher);
    o += put_namelist(p + o, cipher);
    o += put_namelist(p + o, mac);
    o += put_namelist(p + o, mac);
    o += put_namelist(p + o, "none");
    o += put_namelist(p + o, "none");
    o += put_namelist(p + o, "");
    o += put_namelist(p + o, "");
    p[o++] = 0; // first_kex_packet_follows
    o += put_u32(p + o, 0);
    relay_sends_packet(p, o);
}

// The client's own KEXINIT name-list number @p want, copied out of the capture. A relay negotiates
// against what the client offered, so the mock takes its algorithms from the wire rather than from
// an assumption about what this build advertises.
static void client_namelist(int want, char *out, size_t cap)
{
    size_t n = 0;
    const uint8_t *w = pc_net_host_sent(&n);
    out[0] = '\0';
    if (n < 23 || w[5] != SSH_MSG_KEXINIT)
    {
        return;
    }
    size_t at = 5 + 1 + 16;
    for (int list = 0; list <= want && at + 4 <= n; list++)
    {
        uint32_t len = ((uint32_t)w[at] << 24) | ((uint32_t)w[at + 1] << 16) | ((uint32_t)w[at + 2] << 8) | w[at + 3];
        at += 4;
        if (at + len > n)
        {
            return;
        }
        if (list == want)
        {
            size_t take = len < cap - 1 ? len : cap - 1;
            memcpy(out, w + at, take);
            out[take] = '\0';
            return;
        }
        at += len;
    }
}

// Keep only the first name of a comma-separated name-list: the client's own preference.
static void first_name(char *s)
{
    char *comma = strchr(s, ',');
    if (comma)
    {
        *comma = '\0';
    }
}

// Drive the client to the point where a KEXDH_REPLY is what it is waiting for, negotiating with the
// algorithms it actually offered.
static void drive_to_kexdh_reply(void)
{
    tcp_capture_reset();
    relay_sends_str("SSH-2.0-MockRelay\r\n");
    pc_ssh_tunnel_poll();

    char kex[256];
    char hostkey[256];
    char cipher[256];
    char mac[256];
    client_namelist(0, kex, sizeof(kex));
    client_namelist(1, hostkey, sizeof(hostkey));
    client_namelist(2, cipher, sizeof(cipher));
    client_namelist(4, mac, sizeof(mac));
    first_name(kex);
    first_name(hostkey);
    first_name(cipher);
    first_name(mac);

    relay_sends_kexinit(kex, hostkey, cipher, mac);
    // The poll is driven off the clock as well as the socket, so the virtual clock steps with it:
    // a frozen clock leaves any timed step waiting forever.
    for (int k = 0; k < 64; k++)
    {
        set_millis(millis() + 1);
        pc_ssh_tunnel_poll();
    }
}


// ---------------------------------------------------------------------------
// RFC 4253 sec 4.2 - Protocol Version Exchange
// ---------------------------------------------------------------------------

// "both sides MUST send an identification string", in the given form, "terminated by a single
// Carriage Return (CR) and a single Line Feed (LF)", at most 255 characters including them, and
// never carrying a null character.
static void test_s4_2_client_sends_a_conforming_identification_string(void)
{
    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));

    size_t n = 0;
    const uint8_t *sent = pc_net_host_sent(&n);
    TEST_ASSERT_TRUE(n >= 10);

    // Find the end of the identification line; everything up to CR LF is the string.
    size_t line = 0;
    while (line + 1 < n && !(sent[line] == '\r' && sent[line + 1] == '\n'))
    {
        line++;
    }
    TEST_ASSERT_TRUE(line + 1 < n);          // it is CR LF terminated
    TEST_ASSERT_TRUE(line + 2 <= 255);       // including the CR and LF
    TEST_ASSERT_EQUAL_MEMORY("SSH-2.0-", sent, 8); // protoversion MUST be 2.0

    for (size_t k = 0; k < line; k++)
    {
        TEST_ASSERT_NOT_EQUAL_UINT8(0, sent[k]); // "The null character MUST NOT be sent"
        TEST_ASSERT_NOT_EQUAL_UINT8('\r', sent[k]);
        TEST_ASSERT_NOT_EQUAL_UINT8('\n', sent[k]); // a single CR and a single LF, at the end
    }
}

// "The server MAY send other lines of data before sending the version string ... Such lines MUST
// NOT begin with 'SSH-' ... Clients MUST be able to process such lines." A relay fronted by a
// TCP-wrapper that prints a notice first must still reach the key exchange.
static void test_s4_2_client_processes_preamble_lines_before_the_version(void)
{
    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));
    tcp_capture_reset();

    relay_sends_str("Authorized use only.\r\n"
                    "Contact ops@example.com\r\n"
                    "SSH-2.0-MockRelay\r\n");
    pc_ssh_tunnel_poll();

    // "Key exchange will begin immediately after sending this identifier": having read the relay's
    // version past the preamble, the client's next packet is its KEXINIT.
    size_t n = 0;
    const uint8_t *sent = pc_net_host_sent(&n);
    TEST_ASSERT_TRUE(n > 5);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, sent[5]); // past packet_length + padding_length
    TEST_ASSERT_NOT_EQUAL(PC_TUN_FAILED, pc_ssh_tunnel_state_get());
}

// A preamble line that never terminates cannot be a version string either: the client keeps
// waiting rather than treating the fragment as the relay's identification.
static void test_s4_2_an_unterminated_preamble_line_is_not_taken_as_the_version(void)
{
    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));
    tcp_capture_reset();

    relay_sends_str("Authorized use only, no terminator yet");
    pc_ssh_tunnel_poll();

    size_t n = 0;
    (void)pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(0, n); // nothing sent: the exchange has not begun
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 7.1 - Algorithm Negotiation
// ---------------------------------------------------------------------------

// "Key exchange begins by each side sending" SSH_MSG_KEXINIT. The client sends its own once the
// identification strings have been exchanged.
static void test_s7_1_client_sends_kexinit_after_the_identification_exchange(void)
{
    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));
    tcp_capture_reset();

    relay_sends_str("SSH-2.0-MockRelay\r\n");
    pc_ssh_tunnel_poll();

    size_t n = 0;
    const uint8_t *sent = pc_net_host_sent(&n);
    TEST_ASSERT_TRUE(n > 5);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, sent[5]);

    // sec 7.1: the payload is the message byte, a 16-byte cookie, then the ten name-lists.
    uint32_t first_list = ((uint32_t)sent[22] << 24) | ((uint32_t)sent[23] << 16) | ((uint32_t)sent[24] << 8) |
                          sent[25];
    TEST_ASSERT_TRUE(first_list > 0); // the kex name-list is not empty
}

// Report what the client offered, so a negotiation that does not settle says why rather than
// leaving the next case to guess. Temporary: prints the sec 7.1 name-lists off the wire.
static void test_diag_report_client_kexinit_namelists(void)
{
    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));
    tcp_capture_reset();
    relay_sends_str("SSH-2.0-MockRelay\r\n");
    pc_ssh_tunnel_poll();

    size_t n = 0;
    const uint8_t *w = pc_net_host_sent(&n);
    TEST_ASSERT_TRUE(n > 22);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, w[5]);

    size_t at = 5 + 1 + 16; // past the message byte and the cookie
    char line[512];
    for (int list = 0; list < 4 && at + 4 <= n; list++)
    {
        uint32_t len = ((uint32_t)w[at] << 24) | ((uint32_t)w[at + 1] << 16) | ((uint32_t)w[at + 2] << 8) | w[at + 3];
        at += 4;
        if (at + len > n || len >= sizeof(line))
        {
            break;
        }
        memcpy(line, w + at, len);
        line[len] = '\0';
        TEST_MESSAGE(line);
        at += len;
    }

    // Now answer with a KEXINIT built from those exact lists and report every packet the client
    // sends afterwards, so a stalled negotiation says what it did rather than what it did not.
    char kex[256];
    char hostkey[256];
    char cipher[256];
    char mac[256];
    client_namelist(0, kex, sizeof(kex));
    client_namelist(1, hostkey, sizeof(hostkey));
    client_namelist(2, cipher, sizeof(cipher));
    client_namelist(4, mac, sizeof(mac));
    first_name(kex);
    first_name(hostkey);
    first_name(cipher);
    first_name(mac);
    TEST_MESSAGE("relay offers:");
    TEST_MESSAGE(kex);
    TEST_MESSAGE(hostkey);
    TEST_MESSAGE(cipher);
    TEST_MESSAGE(mac);

    char msg[64];
    tcp_capture_reset();
    snprintf(msg, sizeof(msg), "state before relay KEXINIT: %d", (int)pc_ssh_tunnel_state_get());
    TEST_MESSAGE(msg);
    relay_sends_kexinit(kex, hostkey, cipher, mac);

    // One millisecond and one poll at a time, reporting the state after each, so the step that
    // changes it is named rather than inferred from the end result.
    for (int k = 0; k < 6; k++)
    {
        set_millis(millis() + 1);
        pc_ssh_tunnel_poll();
        size_t so_far = 0;
        (void)pc_net_host_sent(&so_far);
        snprintf(msg, sizeof(msg), "  poll %d: state %d, %u bytes out", k, (int)pc_ssh_tunnel_state_get(),
                 (unsigned)so_far);
        TEST_MESSAGE(msg);
    }

    size_t after = 0;
    const uint8_t *a = pc_net_host_sent(&after);
    snprintf(msg, sizeof(msg), "bytes sent after relay KEXINIT: %u", (unsigned)after);
    TEST_MESSAGE(msg);
    size_t p = 0;
    while (p + 6 <= after)
    {
        uint32_t pkt = ((uint32_t)a[p] << 24) | ((uint32_t)a[p + 1] << 16) | ((uint32_t)a[p + 2] << 8) | a[p + 3];
        if (pkt < 2 || p + 4 + pkt > after)
        {
            break;
        }
        snprintf(msg, sizeof(msg), "  client sent message %u", (unsigned)a[p + 5]);
        TEST_MESSAGE(msg);
        p += 4 + pkt;
    }
    snprintf(msg, sizeof(msg), "tunnel state: %d", (int)pc_ssh_tunnel_state_get());
    TEST_MESSAGE(msg);
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 8 - the host key decision
// ---------------------------------------------------------------------------

// sec 8 step 3: "C verifies that K_S really is the host key for S (e.g., using certificates or a
// local database)." The tunnel is configured with a pin of zeros, which no real host-key blob
// hashes to, so a relay presenting any key at all must fail the exchange rather than proceed.
//
// The reply is otherwise well-formed: a KEXDH_REPLY carrying K_S, f and a signature. Only the host
// key is wrong, so a client that reached authentication or brought the tunnel up did not make the
// check sec 8 requires.
static void test_s8_a_host_key_that_does_not_match_the_pin_fails_the_exchange(void)
{
    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));

    tcp_capture_reset();
    drive_to_kexdh_reply();

    // The client is genuinely waiting for the reply: it has put its own KEXDH_INIT on the wire.
    // Without this the case would pass on a client that never looked at a host key at all.
    size_t sent_n = 0;
    const uint8_t *sent = pc_net_host_sent(&sent_n);
    TEST_ASSERT_TRUE(sent_n > 5);
    proto_bool saw_kexdh_init = PROTO_FALSE;
    for (size_t k = 0; k + 5 < sent_n; k++)
    {
        if (sent[k] == SSH_MSG_KEXDH_INIT)
        {
            saw_kexdh_init = PROTO_TRUE;
        }
    }
    TEST_ASSERT_TRUE(saw_kexdh_init);

    // A KEXDH_REPLY the client never asked to trust: string K_S | string f | string signature.
    uint8_t payload[256];
    size_t o = 0;
    payload[o++] = SSH_MSG_KEXDH_REPLY;
    static const char ks[] = "\x00\x00\x00\x0b"
                             "ssh-ed25519"
                             "\x00\x00\x00\x20";
    memcpy(payload + o, ks, sizeof(ks) - 1);
    o += sizeof(ks) - 1;
    memset(payload + o, 0xAB, 32); // a public key that is not the pinned one
    o += 32;
    payload[o++] = 0; // f: a 32-byte string
    payload[o++] = 0;
    payload[o++] = 0;
    payload[o++] = 32;
    memset(payload + o, 0xCD, 32);
    o += 32;
    payload[o++] = 0; // signature blob
    payload[o++] = 0;
    payload[o++] = 0;
    payload[o++] = 4;
    memset(payload + o, 0xEF, 4);
    o += 4;

    relay_sends_packet(payload, o);
    pc_ssh_tunnel_poll();

    // "the key exchange fails": the tunnel must report the failure, not merely be short of up. A
    // client that never reached the host-key check would also be not-up, and that is not the same
    // thing as having rejected the key.
    TEST_ASSERT_FALSE(pc_ssh_tunnel_up());
    TEST_ASSERT_EQUAL(PC_TUN_FAILED, pc_ssh_tunnel_state_get());
}

// ---------------------------------------------------------------------------
// This file's own contract - no RFC governs it, so each case says what it asserts
// ---------------------------------------------------------------------------

// Every field the handshake cannot proceed without is required up front. The pin is among them, so
// there is no path that dials a relay with nothing to check its host key against.
static void test_config_begin_requires_every_field_the_handshake_needs(void)
{
    pc_ssh_tunnel_cfg ok = base_cfg();

    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(NULL));

    pc_ssh_tunnel_cfg c = ok;
    c.host = NULL;
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&c));

    c = ok;
    c.user = NULL;
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&c));

    c = ok;
    c.auth_seed = NULL;
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&c));

    c = ok;
    c.host_pin = NULL; // no pin means no host-key check: refused rather than trusted
    TEST_ASSERT_FALSE(pc_ssh_tunnel_begin(&c));
}

// The lifecycle a caller observes: idle until begun, connecting while the handshake runs, and back
// to idle once torn down.
static void test_state_reports_the_tunnel_lifecycle(void)
{
    TEST_ASSERT_EQUAL(PC_TUN_IDLE, pc_ssh_tunnel_state_get());
    TEST_ASSERT_FALSE(pc_ssh_tunnel_up());

    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));
    TEST_ASSERT_EQUAL(PC_TUN_CONNECTING, pc_ssh_tunnel_state_get());
    TEST_ASSERT_FALSE(pc_ssh_tunnel_up()); // not up until authenticated and forwarded

    pc_ssh_tunnel_end();
    TEST_ASSERT_EQUAL(PC_TUN_IDLE, pc_ssh_tunnel_state_get());
    TEST_ASSERT_FALSE(pc_ssh_tunnel_up());
}

// Polling with no tunnel started does nothing rather than working on a slot that was never set up.
static void test_poll_without_a_tunnel_is_inert(void)
{
    TEST_ASSERT_EQUAL(PC_TUN_IDLE, pc_ssh_tunnel_state_get());
    pc_ssh_tunnel_poll();
    pc_ssh_tunnel_poll();
    size_t n = 0;
    (void)pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_EQUAL(PC_TUN_IDLE, pc_ssh_tunnel_state_get());
}

// The provisioning helper: the same seed always yields the same public key, and different seeds
// yield different ones. This is what an operator copies into the relay's authorized_keys, so it
// has to be reproducible from the seed alone.
static void test_pubkey_derivation_is_deterministic_in_the_seed(void)
{
    uint8_t a[32];
    uint8_t b[32];
    pc_ssh_tunnel_pubkey(AUTH_SEED, a);
    pc_ssh_tunnel_pubkey(AUTH_SEED, b);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, b, 32);

    uint8_t other_seed[32];
    memcpy(other_seed, AUTH_SEED, 32);
    other_seed[0] ^= 0xFF;
    uint8_t c[32];
    pc_ssh_tunnel_pubkey(other_seed, c);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, c, 32));
}

// Run the identification exchange and hand the relay's KEXINIT back, built from what the client
// offered. Reports the resulting state and the first message the client sends afterwards.
static int diag_negotiate_once(const char *tag)
{
    pc_ssh_tunnel_cfg cfg = base_cfg();
    TEST_ASSERT_TRUE(pc_ssh_tunnel_begin(&cfg));
    tcp_capture_reset();
    relay_sends_str("SSH-2.0-MockRelay\r\n");
    pc_ssh_tunnel_poll();

    char kex[256];
    char hostkey[256];
    char cipher[256];
    char mac[256];
    client_namelist(0, kex, sizeof(kex));
    client_namelist(1, hostkey, sizeof(hostkey));
    client_namelist(2, cipher, sizeof(cipher));
    client_namelist(4, mac, sizeof(mac));
    first_name(kex);
    first_name(hostkey);
    first_name(cipher);
    first_name(mac);

    tcp_capture_reset();
    relay_sends_kexinit(kex, hostkey, cipher, mac);
    set_millis(millis() + 1);
    pc_ssh_tunnel_poll();

    size_t n = 0;
    const uint8_t *w = pc_net_host_sent(&n);
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: state %d, %u bytes out, first msg %d", tag, (int)pc_ssh_tunnel_state_get(),
             (unsigned)n, n > 5 ? (int)w[5] : -1);
    TEST_MESSAGE(msg);
    pc_ssh_tunnel_end();
    return (int)pc_ssh_tunnel_state_get();
}

// Whether the parse consults host keys the DEVICE holds when the device is the client. The client
// owns no host key: it pins the relay's. Run last, because the setter is process-wide transport
// state with no unset.
static void test_diag_hostkey_availability_gates_client_negotiation(void)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "before: ed=%d ecdsa=%d", (int)pc_ssh_hostkey_ed25519_available(),
             (int)pc_ssh_hostkey_ecdsa_available());
    TEST_MESSAGE(msg);
    diag_negotiate_once("no host key held");

    static const uint8_t HOSTKEY_SEED[32] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                                             9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    pc_ssh_hostkey_ed25519_set(HOSTKEY_SEED);
    snprintf(msg, sizeof(msg), "after:  ed=%d ecdsa=%d", (int)pc_ssh_hostkey_ed25519_available(),
             (int)pc_ssh_hostkey_ecdsa_available());
    TEST_MESSAGE(msg);
    diag_negotiate_once("ed25519 host key held");
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_s4_2_client_sends_a_conforming_identification_string);
    RUN_TEST(test_s4_2_client_processes_preamble_lines_before_the_version);
    RUN_TEST(test_s4_2_an_unterminated_preamble_line_is_not_taken_as_the_version);
    RUN_TEST(test_s7_1_client_sends_kexinit_after_the_identification_exchange);
    RUN_TEST(test_diag_report_client_kexinit_namelists);
    RUN_TEST(test_s8_a_host_key_that_does_not_match_the_pin_fails_the_exchange);
    RUN_TEST(test_config_begin_requires_every_field_the_handshake_needs);
    RUN_TEST(test_state_reports_the_tunnel_lifecycle);
    RUN_TEST(test_poll_without_a_tunnel_is_inert);
    RUN_TEST(test_pubkey_derivation_is_deterministic_in_the_seed);
    RUN_TEST(test_diag_hostkey_availability_gates_client_negotiation);
    return UNITY_END();
}
