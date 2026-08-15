// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// connection/connection.c (RFC 4254 sec 5.2, sec 5.3, sec 6.2, sec 6.4, sec 6.5, sec 6.7, sec 8):
// the channel multiplexer - its window arithmetic, closing, the channel requests and the
// pseudo-terminal one of them allocates.

#include "mmgr/protostr.h" // str.len - the bounded length, since the library carries no strlen
#include "network_drivers/presentation/ssh/connection/connection.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

static proto_bool pty_req_parse(const uint8_t *p, size_t len, size_t off, SshPtyRequest *req)
{
    SshConnection.pty.p = p;
    SshConnection.chan.len = len;
    SshConnection.pty.off = off;
    SshConnection.pty.req = req;
    SshConnection.pty_req_parse(SshConnection.internal);
    return SshConnection.ok;
}

static proto_bool req_strings_present(const uint8_t *p, size_t len, size_t off, uint8_t n)
{
    SshConnection.pty.p = p;
    SshConnection.chan.len = len;
    SshConnection.pty.off = off;
    SshConnection.pty.n = n;
    SshConnection.req_strings_present(SshConnection.internal);
    return SshConnection.ok;
}

static proto_bool window_change_parse(const uint8_t *p, size_t len, size_t off, SshPtyRequest *req)
{
    SshConnection.pty.p = p;
    SshConnection.chan.len = len;
    SshConnection.pty.off = off;
    SshConnection.pty.req = req;
    SshConnection.window_change_parse(SshConnection.internal);
    return SshConnection.ok;
}

static int channel_handle_close(uint8_t slot, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                size_t cap)
{
    SshConnection.chan.slot = slot;
    SshConnection.chan.payload = payload;
    SshConnection.chan.len = len;
    SshConnection.chan.out = out;
    SshConnection.chan.cap = cap;
    SshConnection.channel_handle_close(SshConnection.internal);
    if (out_len)
    {
        *out_len = SshConnection.chan.out_len;
    }
    return SshConnection.i32;
}

static int channel_handle_open(uint8_t slot, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                               size_t cap)
{
    SshConnection.chan.slot = slot;
    SshConnection.chan.payload = payload;
    SshConnection.chan.len = len;
    SshConnection.chan.out = out;
    SshConnection.chan.cap = cap;
    SshConnection.channel_handle_open(SshConnection.internal);
    if (out_len)
    {
        *out_len = SshConnection.chan.out_len;
    }
    return SshConnection.i32;
}

static int channel_build_eof(uint8_t slot, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap)
{
    SshConnection.chan.slot = slot;
    SshConnection.chan.channel = channel;
    SshConnection.chan.out = out;
    SshConnection.chan.cap = cap;
    SshConnection.channel_build_eof(SshConnection.internal);
    if (out_len)
    {
        *out_len = SshConnection.chan.out_len;
    }
    return SshConnection.i32;
}

static int channel_build_close(uint8_t slot, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap)
{
    SshConnection.chan.slot = slot;
    SshConnection.chan.channel = channel;
    SshConnection.chan.out = out;
    SshConnection.chan.cap = cap;
    SshConnection.channel_build_close(SshConnection.internal);
    if (out_len)
    {
        *out_len = SshConnection.chan.out_len;
    }
    return SshConnection.i32;
}


// Per-channel flow control, reached through the connection namespace. The window struct stays the
// caller's; only the call moves onto the handle.
static proto_bool flow_recv_take(SshFlow *f, uint32_t n)
{
    SshConnection.flow.f = f;
    SshConnection.flow.n = n;
    SshConnection.flow_recv_take(SshConnection.internal);
    return SshConnection.ok;
}

static proto_bool flow_replenish_due(SshFlow *f, uint32_t *add)
{
    SshConnection.flow.f = f;
    SshConnection.flow_replenish_due(SshConnection.internal);
    if (add)
    {
        *add = SshConnection.flow.add;
    }
    return SshConnection.ok;
}

static void flow_local_credit(SshFlow *f, uint32_t add)
{
    SshConnection.flow.f = f;
    SshConnection.flow.add = add;
    SshConnection.flow_local_credit(SshConnection.internal);
}

static proto_bool flow_send_allows(SshFlow *f, size_t len)
{
    SshConnection.flow.f = f;
    SshConnection.chan.len = len;
    SshConnection.flow_send_allows(SshConnection.internal);
    return SshConnection.ok;
}

static uint32_t flow_send_cap(SshFlow *f, uint32_t want)
{
    SshConnection.flow.f = f;
    SshConnection.flow.want = want;
    SshConnection.flow_send_cap(SshConnection.internal);
    return SshConnection.u32;
}

static void flow_init(SshFlow *f, uint32_t local_window, uint32_t peer_window, uint32_t peer_max_pkt)
{
    SshConnection.flow.f = f;
    SshConnection.flow.local_window = local_window;
    SshConnection.flow.peer_window = peer_window;
    SshConnection.flow.peer_max_pkt = peer_max_pkt;
    SshConnection.flow_init(SshConnection.internal);
}

static void flow_send_take(SshFlow *f, uint32_t n)
{
    SshConnection.flow.f = f;
    SshConnection.flow.n = n;
    SshConnection.flow_send_take(SshConnection.internal);
}

static void flow_peer_add(SshFlow *f, uint32_t add)
{
    SshConnection.flow.f = f;
    SshConnection.flow.add = add;
    SshConnection.flow_peer_add(SshConnection.internal);
}


// The connection layer, reached through its namespace.
static int chan_alloc(uint8_t slot)
{
    SshConnection.chan.slot = slot;
    SshConnection.chan_alloc(SshConnection.internal);
    return SshConnection.i32;
}

static void channel_set_data_cb(SshChannelDataCb cb)
{
    SshConnection.data_cb = cb;
    SshConnection.set_data_cb(SshConnection.internal);
}

static int channel_handle_eof(uint8_t slot, const uint8_t *payload, size_t len)
{
    SshConnection.chan.slot = slot;
    SshConnection.chan.payload = payload;
    SshConnection.chan.len = len;
    SshConnection.channel_handle_eof(SshConnection.internal);
    return SshConnection.i32;
}

static proto_bool pty_modes_valid(const uint8_t *modes, uint32_t modes_len, uint32_t *consumed)
{
    SshConnection.pty.modes = modes;
    SshConnection.pty.modes_len = modes_len;
    SshConnection.pty_modes_valid(SshConnection.internal);
    if (consumed)
    {
        *consumed = SshConnection.pty.consumed;
    }
    return SshConnection.ok;
}


// A length-prefixed string, RFC 4251 sec 5: uint32 length, then that many bytes.
static size_t put_str(uint8_t *p, size_t off, const char *s, uint32_t n)
{
    p[off] = (uint8_t)(n >> 24);
    p[off + 1] = (uint8_t)(n >> 16);
    p[off + 2] = (uint8_t)(n >> 8);
    p[off + 3] = (uint8_t)n;
    for (uint32_t k = 0; k < n; k++)
    {
        p[off + 4 + k] = (uint8_t)s[k];
    }
    return off + 4 + n;
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t put_string(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    wr_u32(p, n);
    memcpy(p + 4, s, n);
    return 4 + n;
}

static uint32_t open_session(uint32_t peer_id, uint32_t peer_window)
{
    uint8_t pkt[64];
    size_t n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(pkt + n, "session");
    wr_u32(pkt + n, peer_id);
    wr_u32(pkt + n + 4, peer_window);
    wr_u32(pkt + n + 8, 32768);
    n += 12;

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_handle_open(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_OPEN_CONFIRMATION, out[0]);
    return rd_u32(out + 5);
}

static size_t put_u32(uint8_t *p, size_t off, uint32_t v)
{
    p[off] = (uint8_t)(v >> 24);
    p[off + 1] = (uint8_t)(v >> 16);
    p[off + 2] = (uint8_t)(v >> 8);
    p[off + 3] = (uint8_t)v;
    return off + 4;
}

static size_t build_pty(uint8_t *p, const char *term, uint32_t term_len, uint32_t w, uint32_t h, uint32_t wpx,
                        uint32_t hpx, const char *modes, uint32_t modes_len)
{
    size_t n = put_str(p, 0, term, term_len);
    n = put_u32(p, n, w);
    n = put_u32(p, n, h);
    n = put_u32(p, n, wpx);
    n = put_u32(p, n, hpx);
    return put_str(p, n, modes, modes_len);
}

void setUp(void)
{

    SshConnection.chan.slot = 0;
    SshConnection.channel_init(SshConnection.internal);
    channel_set_data_cb(NULL);
}
void tearDown(void)
{
}

// "The window size specifies how many bytes the other party can send before it must wait for the
// window to be adjusted."
static void test_sec5_2_send_is_bounded_by_the_peer_window(void)
{
    SshFlow f;
    flow_init(&f, 1024u, 10u, 32768u);

    TEST_ASSERT_TRUE(flow_send_allows(&f, 10u));
    TEST_ASSERT_FALSE(flow_send_allows(&f, 11u));
}

static void test_sec5_2_sending_decrements_the_window(void)
{
    SshFlow f;
    flow_init(&f, 1024u, 100u, 32768u);

    flow_send_take(&f, 40u);
    TEST_ASSERT_EQUAL_UINT32(60u, f.peer_window);

    flow_send_take(&f, 60u);
    TEST_ASSERT_EQUAL_UINT32(0u, f.peer_window);
    TEST_ASSERT_FALSE(flow_send_allows(&f, 1u));
}

static void test_sec5_2_send_cap_is_the_smaller_of_window_and_max_packet(void)
{
    SshFlow f;

    // Window is the binding limit.
    flow_init(&f, 1024u, 200u, 32768u);
    TEST_ASSERT_EQUAL_UINT32(200u, flow_send_cap(&f, 4096u));

    // Maximum packet size is the binding limit.
    flow_init(&f, 1024u, 100000u, 512u);
    TEST_ASSERT_EQUAL_UINT32(512u, flow_send_cap(&f, 4096u));

    // Neither binds: the caller gets what it asked for.
    flow_init(&f, 1024u, 100000u, 32768u);
    TEST_ASSERT_EQUAL_UINT32(4096u, flow_send_cap(&f, 4096u));
}

static void test_sec5_2_window_adjust_increments_the_peer_window(void)
{
    SshFlow f;
    flow_init(&f, 1024u, 0u, 32768u);
    TEST_ASSERT_FALSE(flow_send_allows(&f, 1u));

    flow_peer_add(&f, 4096u);
    TEST_ASSERT_EQUAL_UINT32(4096u, f.peer_window);
    TEST_ASSERT_TRUE(flow_send_allows(&f, 4096u));
}

static void test_sec5_2_window_of_2_pow_32_minus_1_is_handled(void)
{
    SshFlow f;
    flow_init(&f, 1024u, 0xFFFFFFFFu, 32768u);

    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, f.peer_window);
    TEST_ASSERT_TRUE(flow_send_allows(&f, 32768u));
    TEST_ASSERT_EQUAL_UINT32(32768u, flow_send_cap(&f, 0xFFFFFFFFu));

    // Spending against it decrements exactly, with no borrow off the top of the field.
    flow_send_take(&f, 32768u);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu - 32768u, f.peer_window);
}

static void test_sec5_2_max_packet_binds_even_with_a_wide_window(void)
{
    SshFlow f;
    flow_init(&f, 1024u, 0xFFFFFFFFu, 32768u);

    TEST_ASSERT_TRUE(flow_send_allows(&f, 32768u));
    TEST_ASSERT_FALSE(flow_send_allows(&f, 32769u));
}

static void test_sec5_2_window_must_not_be_increased_above_2_pow_32_minus_1(void)
{
    SshFlow f;
    flow_init(&f, 1024u, 0xFFFFFF00u, 32768u);

    flow_peer_add(&f, 0x1000u); // would wrap to 0x0FFF if it were a bare addition
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, f.peer_window);
}

static void test_sec5_2_recv_take_past_an_empty_window_does_not_wrap(void)
{
    SshFlow f;
    flow_init(&f, 64u, 1024u, 32768u);

    TEST_ASSERT_TRUE(flow_recv_take(&f, 64u));
    TEST_ASSERT_EQUAL_UINT32(0u, f.local_window);

    // One byte past the advertised window: refused, and the window stays at zero.
    TEST_ASSERT_FALSE(flow_recv_take(&f, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, f.local_window);
}

static void test_sec5_2_replenish_restores_the_advertised_window(void)
{
    SshFlow f;
    uint32_t add = 0;
    flow_init(&f, 2048u, 1024u, 32768u);

    TEST_ASSERT_TRUE(flow_recv_take(&f, 2048u));
    TEST_ASSERT_TRUE(flow_replenish_due(&f, &add));

    flow_local_credit(&f, add);
    TEST_ASSERT_EQUAL_UINT32(2048u, f.local_window);
}

static void test_sec5_2_replenish_not_due_on_a_full_window(void)
{
    SshFlow f;
    uint32_t add = 0xDEADBEEFu;
    flow_init(&f, 2048u, 1024u, 32768u);

    TEST_ASSERT_FALSE(flow_replenish_due(&f, &add));
}

static void test_sec6_5_one_string_present(void)
{
    uint8_t p[32];
    size_t n = put_str(p, 0, "whoami", 6);

    TEST_ASSERT_TRUE(req_strings_present(p, n, 0, 1));
}

static void test_sec6_4_two_strings_present(void)
{
    uint8_t p[48];
    size_t n = put_str(p, 0, "LANG", 4);
    n = put_str(p, n, "C.UTF-8", 7);

    TEST_ASSERT_TRUE(req_strings_present(p, n, 0, 2));
}

static void test_zero_strings_is_satisfied(void)
{
    uint8_t p[4] = {0, 0, 0, 0};

    TEST_ASSERT_TRUE(req_strings_present(p, sizeof(p), 0, 0));
}

static void test_second_string_missing_is_refused(void)
{
    uint8_t p[48];
    size_t n = put_str(p, 0, "LANG", 4);

    TEST_ASSERT_FALSE(req_strings_present(p, n, 0, 2));
}

static void test_string_body_truncated_is_refused(void)
{
    uint8_t p[16];
    (void)put_str(p, 0, "abcdefgh", 8);

    // Claim 8 bytes of body but hand over only 4 of them.
    TEST_ASSERT_FALSE(req_strings_present(p, 8, 0, 1));
}

static void test_string_header_truncated_is_refused(void)
{
    uint8_t p[4] = {0, 0, 0, 4};

    TEST_ASSERT_FALSE(req_strings_present(p, 3, 0, 1));
}

static void test_empty_string_is_whole(void)
{
    uint8_t p[8];
    size_t n = put_str(p, 0, "", 0);

    TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)n);
    TEST_ASSERT_TRUE(req_strings_present(p, n, 0, 1));
}

static void test_offset_is_honoured(void)
{
    uint8_t p[48];
    size_t first = put_str(p, 0, "skipme", 6);
    size_t n = put_str(p, first, "second", 6);

    TEST_ASSERT_TRUE(req_strings_present(p, n, first, 1));
    TEST_ASSERT_FALSE(req_strings_present(p, n, first, 2));
}

void test_s5_3_eof_is_byte_plus_recipient_channel()
{
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, protocore_ssh_sig_build_eof(7, out, sizeof(out), &olen));
    TEST_ASSERT_EQUAL_size_t(5, olen);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_EOF, out[0]);
    TEST_ASSERT_EQUAL_UINT32(7, rd_u32(out + 1));
}

void test_s5_3_close_is_byte_plus_recipient_channel()
{
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, protocore_ssh_sig_build_close(7, out, sizeof(out), &olen));
    TEST_ASSERT_EQUAL_size_t(5, olen);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_CLOSE, out[0]);
    TEST_ASSERT_EQUAL_UINT32(7, rd_u32(out + 1));
}

void test_s5_3_builders_refuse_a_short_buffer()
{
    uint8_t out[4];
    size_t olen = 0;
    TEST_ASSERT_LESS_THAN_INT(0, protocore_ssh_sig_build_eof(7, out, sizeof(out), &olen));
    TEST_ASSERT_LESS_THAN_INT(0, protocore_ssh_sig_build_close(7, out, sizeof(out), &olen));
}

void test_s5_3_build_eof_keeps_the_channel_open_and_latches_eof_sent()
{
    uint32_t id = open_session(5, 1000);
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_build_eof(0, id, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(5, olen);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_EOF, out[0]);
    TEST_ASSERT_EQUAL_UINT32(5, rd_u32(out + 1));
    TEST_ASSERT_TRUE(ssh_chan[0][id].open);
    TEST_ASSERT_TRUE(ssh_chan[0][id].eof_sent);
}

// "The channel is considered closed for a party when it has both sent and received
// SSH_MSG_CHANNEL_CLOSE, and the party may then reuse the channel number." Sending is one half, so
// the number stays taken until the peer's CLOSE answers ours.
void test_s5_3_sending_close_alone_does_not_free_the_channel_number()
{
    uint32_t id = open_session(5, 1000);
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_build_close(0, id, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(5, olen);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_CLOSE, out[0]);
    TEST_ASSERT_EQUAL_UINT32(5, rd_u32(out + 1));
    TEST_ASSERT_TRUE(ssh_chan[0][id].close_sent);
    TEST_ASSERT_FALSE(ssh_chan[0][id].close_received);
    TEST_ASSERT_TRUE(ssh_chan[0][id].open);
}

// Both halves in: closed for this party, and the number may be reused.
void test_s5_3_both_closes_free_the_channel_number()
{
    uint32_t id = open_session(5, 1000);
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_build_close(0, id, out, &olen, sizeof(out)));

    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(pkt + 1, id);
    olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_handle_close(0, pkt, sizeof(pkt), out, &olen, sizeof(out)));
    // "a party MUST send back an SSH_MSG_CHANNEL_CLOSE unless it has already sent this message for
    // the channel" - it already sent one, so nothing goes back.
    TEST_ASSERT_EQUAL_size_t(0, olen);
    TEST_ASSERT_FALSE(ssh_chan[0][id].open);
    TEST_ASSERT_EQUAL_INT((int)id, chan_alloc(0));
}

// The mirrored order: the peer closes first, so this end MUST send one back, which completes it.
void test_s5_3_inbound_close_is_answered_and_completes_the_close()
{
    uint32_t id = open_session(5, 1000);
    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(pkt + 1, id);
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_handle_close(0, pkt, sizeof(pkt), out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(5, olen);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_CLOSE, out[0]);
    TEST_ASSERT_EQUAL_UINT32(5, rd_u32(out + 1)); // addressed to the peer's number
    TEST_ASSERT_TRUE(ssh_chan[0][id].close_sent);
    TEST_ASSERT_TRUE(ssh_chan[0][id].close_received);
    TEST_ASSERT_FALSE(ssh_chan[0][id].open);
}

void test_s5_3_inbound_eof_marks_the_peer_done_and_keeps_the_channel_open()
{
    uint32_t id = open_session(5, 1000);
    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_EOF;
    wr_u32(pkt + 1, id);
    TEST_ASSERT_EQUAL_INT(0, channel_handle_eof(0, pkt, sizeof(pkt)));
    TEST_ASSERT_TRUE(ssh_chan[0][id].relay_eof);
    TEST_ASSERT_TRUE(ssh_chan[0][id].open);
    TEST_ASSERT_FALSE(ssh_chan[0][id].eof_sent);
}

void test_s5_3_inbound_eof_does_not_consume_window_space()
{
    uint32_t id = open_session(5, 1000);
    uint32_t before = ssh_chan[0][id].flow.local_window;
    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_EOF;
    wr_u32(pkt + 1, id);
    TEST_ASSERT_EQUAL_INT(0, channel_handle_eof(0, pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_UINT32(before, ssh_chan[0][id].flow.local_window);
}

void test_s5_3_inbound_eof_rejects_a_bad_slot_type_or_length()
{
    uint32_t id = open_session(5, 1000);
    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_EOF;
    wr_u32(pkt + 1, id);

    TEST_ASSERT_LESS_THAN_INT(0, channel_handle_eof(MAX_SSH_CONNS, pkt, sizeof(pkt)));
    TEST_ASSERT_LESS_THAN_INT(0, channel_handle_eof(0, pkt, 4));
    pkt[0] = SSH_MSG_CHANNEL_CLOSE;
    TEST_ASSERT_LESS_THAN_INT(0, channel_handle_eof(0, pkt, sizeof(pkt)));
}

void test_s5_3_inbound_eof_on_an_unknown_channel_is_rejected()
{
    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_EOF;
    wr_u32(pkt + 1, PROTOCORE_SSH_MAX_CHANNELS + 1);
    TEST_ASSERT_LESS_THAN_INT(0, channel_handle_eof(0, pkt, sizeof(pkt)));
}

void test_s5_3_inbound_close_is_answered_with_close()
{
    uint32_t id = open_session(5, 1000);
    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(pkt + 1, id);
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_handle_close(0, pkt, sizeof(pkt), out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(5, olen);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_CLOSE, out[0]);
    TEST_ASSERT_EQUAL_UINT32(5, rd_u32(out + 1));
    TEST_ASSERT_FALSE(ssh_chan[0][id].open);
}

// A CLOSE may arrive after we already half-closed; it is still answered with CLOSE.
void test_s5_3_close_after_our_eof_is_still_answered_with_close()
{
    uint32_t id = open_session(5, 1000);
    uint8_t eof_out[16];
    size_t eof_len = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_build_eof(0, id, eof_out, &eof_len, sizeof(eof_out)));
    TEST_ASSERT_TRUE(ssh_chan[0][id].eof_sent);

    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(pkt + 1, id);
    uint8_t out[16];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, channel_handle_close(0, pkt, sizeof(pkt), out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(5, olen);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_CLOSE, out[0]);
    TEST_ASSERT_FALSE(ssh_chan[0][id].open);
}

static void test_sec8_empty_stream_is_valid(void)
{
    uint32_t used = 0xFFFFFFFFu;
    TEST_ASSERT_TRUE(pty_modes_valid(NULL, 0, &used));
    TEST_ASSERT_EQUAL_UINT32(0u, used);
}

static void test_sec8_tty_op_end_terminates(void)
{
    const uint8_t s[] = {0x00, 0xDE, 0xAD};
    uint32_t used = 0;
    TEST_ASSERT_TRUE(pty_modes_valid(s, sizeof(s), &used));
    TEST_ASSERT_EQUAL_UINT32(1u, used);
}

static void test_sec8_one_pair_then_terminator(void)
{
    const uint8_t s[] = {1, 0, 0, 0, 3, 0};
    uint32_t used = 0;
    TEST_ASSERT_TRUE(pty_modes_valid(s, sizeof(s), &used));
    TEST_ASSERT_EQUAL_UINT32(6u, used);
}

static void test_sec8_opcode_159_takes_an_argument(void)
{
    const uint8_t s[] = {159, 0, 0, 0, 1, 0};
    TEST_ASSERT_TRUE(pty_modes_valid(s, sizeof(s), NULL));
}

static void test_sec8_opcode_160_stops_parsing(void)
{
    const uint8_t s[] = {160, 0xFF, 0xFF};
    uint32_t used = 0;
    TEST_ASSERT_TRUE(pty_modes_valid(s, sizeof(s), &used));
    TEST_ASSERT_EQUAL_UINT32(1u, used); // the undefined opcode, and nothing past it
}

static void test_sec8_truncated_argument_is_refused(void)
{
    const uint8_t s[] = {1, 0, 0, 0}; // one byte short of the argument
    TEST_ASSERT_FALSE(pty_modes_valid(s, sizeof(s), NULL));
}

static void test_sec8_opcode_with_no_argument_is_refused(void)
{
    const uint8_t s[] = {5};
    TEST_ASSERT_FALSE(pty_modes_valid(s, sizeof(s), NULL));
}

static void test_sec8_consecutive_pairs(void)
{
    const uint8_t s[] = {1, 0, 0, 0, 3, 2, 0, 0, 0, 28, 3, 0, 0, 0, 127, 0};
    uint32_t used = 0;
    TEST_ASSERT_TRUE(pty_modes_valid(s, sizeof(s), &used));
    TEST_ASSERT_EQUAL_UINT32(16u, used);
}

static void test_sec8_missing_terminator_keeps_the_whole_pairs(void)
{
    const uint8_t s[] = {1, 0, 0, 0, 3};
    uint32_t used = 0;
    TEST_ASSERT_TRUE(pty_modes_valid(s, sizeof(s), &used));
    TEST_ASSERT_EQUAL_UINT32(5u, used);
}

static void test_sec6_2_every_field_is_read(void)
{
    uint8_t p[64];
    const size_t n = build_pty(p, "xterm", 5, 80, 24, 640, 480, "", 0);

    SshPtyRequest pty;
    TEST_ASSERT_TRUE(pty_req_parse(p, n, 0, &pty));
    TEST_ASSERT_EQUAL_STRING("xterm", pty.term);
    TEST_ASSERT_EQUAL_UINT32(80u, pty.width_chars);
    TEST_ASSERT_EQUAL_UINT32(24u, pty.height_rows);
    TEST_ASSERT_EQUAL_UINT32(640u, pty.width_px);
    TEST_ASSERT_EQUAL_UINT32(480u, pty.height_px);
    TEST_ASSERT_EQUAL_UINT32(0u, pty.modes_len);
}

static void test_sec6_2_zero_dimensions_are_still_well_formed(void)
{
    uint8_t p[64];
    const size_t n = build_pty(p, "vt100", 5, 0, 0, 0, 0, "", 0);

    SshPtyRequest pty;
    TEST_ASSERT_TRUE(pty_req_parse(p, n, 0, &pty));
    TEST_ASSERT_EQUAL_UINT32(0u, pty.width_chars);
    TEST_ASSERT_EQUAL_UINT32(0u, pty.height_rows);
}

static void test_sec6_2_both_dimension_pairs_are_carried(void)
{
    uint8_t p[64];
    const size_t n = build_pty(p, "vt100", 5, 132, 43, 0, 0, "", 0);

    SshPtyRequest pty;
    TEST_ASSERT_TRUE(pty_req_parse(p, n, 0, &pty));
    TEST_ASSERT_EQUAL_UINT32(132u, pty.width_chars);
    TEST_ASSERT_EQUAL_UINT32(43u, pty.height_rows);
    TEST_ASSERT_EQUAL_UINT32(0u, pty.width_px);
    TEST_ASSERT_EQUAL_UINT32(0u, pty.height_px);
}

static void test_sec6_2_long_term_is_truncated_not_refused(void)
{
    uint8_t p[128];
    const char *term = "xterm-256color-with-a-very-long-name";
    const size_t n = build_pty(p, term, 36, 80, 24, 0, 0, "", 0);

    SshPtyRequest pty;
    TEST_ASSERT_TRUE(pty_req_parse(p, n, 0, &pty));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(PROTOCORE_SSH_PTY_TERM_MAX - 1u),
                             (uint32_t)str.len(pty.term, sizeof(pty.term)));
    TEST_ASSERT_EQUAL_UINT32(80u, pty.width_chars); // the fields after it still line up
}

static void test_sec6_2_missing_term_is_refused(void)
{
    uint8_t p[8] = {0, 0, 0, 0};
    SshPtyRequest pty;
    TEST_ASSERT_FALSE(pty_req_parse(p, 2, 0, &pty));
}

static void test_sec6_2_truncated_dimensions_are_refused(void)
{
    uint8_t p[64];
    size_t n = put_str(p, 0, "vt100", 5);
    n = put_u32(p, n, 80);
    n = put_u32(p, n, 24);
    n = put_u32(p, n, 640); // three of the four

    SshPtyRequest pty;
    TEST_ASSERT_FALSE(pty_req_parse(p, n, 0, &pty));
}

static void test_sec6_2_missing_modes_string_is_refused(void)
{
    uint8_t p[64];
    size_t n = put_str(p, 0, "vt100", 5);
    n = put_u32(p, n, 80);
    n = put_u32(p, n, 24);
    n = put_u32(p, n, 640);
    n = put_u32(p, n, 480);

    SshPtyRequest pty;
    TEST_ASSERT_FALSE(pty_req_parse(p, n, 0, &pty));
}

static void test_sec6_2_modes_stream_is_validated(void)
{
    uint8_t p[64];
    SshPtyRequest pty;

    const size_t ok = build_pty(p, "vt100", 5, 80, 24, 0, 0, "\x01\x00\x00\x00\x03\x00", 6);
    TEST_ASSERT_TRUE(pty_req_parse(p, ok, 0, &pty));
    TEST_ASSERT_EQUAL_UINT32(6u, pty.modes_len);

    const size_t bad = build_pty(p, "vt100", 5, 80, 24, 0, 0, "\x01\x00\x00", 3); // argument cut short
    TEST_ASSERT_FALSE(pty_req_parse(p, bad, 0, &pty));
}

static void test_sec6_2_offset_is_honoured(void)
{
    uint8_t p[96];
    const size_t skip = put_str(p, 0, "skipme", 6);
    size_t n = put_str(p, skip, "vt100", 5);
    n = put_u32(p, n, 80);
    n = put_u32(p, n, 24);
    n = put_u32(p, n, 0);
    n = put_u32(p, n, 0);
    n = put_str(p, n, "", 0);

    SshPtyRequest pty;
    TEST_ASSERT_TRUE(pty_req_parse(p, n, skip, &pty));
    TEST_ASSERT_EQUAL_STRING("vt100", pty.term);
}

static void test_sec6_7_four_dimensions_are_read(void)
{
    uint8_t p[32];
    size_t n = put_u32(p, 0, 100);
    n = put_u32(p, n, 40);
    n = put_u32(p, n, 800);
    n = put_u32(p, n, 600);

    SshPtyRequest dim;
    TEST_ASSERT_TRUE(window_change_parse(p, n, 0, &dim));
    TEST_ASSERT_EQUAL_UINT32(100u, dim.width_chars);
    TEST_ASSERT_EQUAL_UINT32(40u, dim.height_rows);
    TEST_ASSERT_EQUAL_UINT32(800u, dim.width_px);
    TEST_ASSERT_EQUAL_UINT32(600u, dim.height_px);
}

static void test_sec6_7_truncated_is_refused(void)
{
    uint8_t p[32];
    size_t n = put_u32(p, 0, 100);
    n = put_u32(p, n, 40);
    n = put_u32(p, n, 800); // three of the four

    SshPtyRequest dim;
    TEST_ASSERT_FALSE(window_change_parse(p, n, 0, &dim));
}

static void test_sec6_7_carries_no_term_or_modes(void)
{
    uint8_t p[32];
    size_t n = put_u32(p, 0, 1);
    n = put_u32(p, n, 2);
    n = put_u32(p, n, 3);
    n = put_u32(p, n, 4);

    SshPtyRequest dim;
    TEST_ASSERT_TRUE(window_change_parse(p, n, 0, &dim));
    TEST_ASSERT_EQUAL_UINT32(0u, dim.modes_len);
    TEST_ASSERT_EQUAL_CHAR('\0', dim.term[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_s5_3_eof_is_byte_plus_recipient_channel);
    RUN_TEST(test_s5_3_close_is_byte_plus_recipient_channel);
    RUN_TEST(test_s5_3_builders_refuse_a_short_buffer);
    RUN_TEST(test_s5_3_build_eof_keeps_the_channel_open_and_latches_eof_sent);
    RUN_TEST(test_s5_3_sending_close_alone_does_not_free_the_channel_number);
    RUN_TEST(test_s5_3_both_closes_free_the_channel_number);
    RUN_TEST(test_s5_3_inbound_close_is_answered_and_completes_the_close);
    RUN_TEST(test_s5_3_inbound_eof_marks_the_peer_done_and_keeps_the_channel_open);
    RUN_TEST(test_s5_3_inbound_eof_does_not_consume_window_space);
    RUN_TEST(test_s5_3_inbound_eof_rejects_a_bad_slot_type_or_length);
    RUN_TEST(test_s5_3_inbound_eof_on_an_unknown_channel_is_rejected);
    RUN_TEST(test_s5_3_inbound_close_is_answered_with_close);
    RUN_TEST(test_s5_3_close_after_our_eof_is_still_answered_with_close);
    RUN_TEST(test_sec5_2_send_is_bounded_by_the_peer_window);
    RUN_TEST(test_sec5_2_sending_decrements_the_window);
    RUN_TEST(test_sec5_2_send_cap_is_the_smaller_of_window_and_max_packet);
    RUN_TEST(test_sec5_2_window_adjust_increments_the_peer_window);
    RUN_TEST(test_sec5_2_window_of_2_pow_32_minus_1_is_handled);
    RUN_TEST(test_sec5_2_max_packet_binds_even_with_a_wide_window);
    RUN_TEST(test_sec5_2_window_must_not_be_increased_above_2_pow_32_minus_1);
    RUN_TEST(test_sec5_2_recv_take_past_an_empty_window_does_not_wrap);
    RUN_TEST(test_sec5_2_replenish_restores_the_advertised_window);
    RUN_TEST(test_sec5_2_replenish_not_due_on_a_full_window);
    RUN_TEST(test_sec6_5_one_string_present);
    RUN_TEST(test_sec6_4_two_strings_present);
    RUN_TEST(test_zero_strings_is_satisfied);
    RUN_TEST(test_second_string_missing_is_refused);
    RUN_TEST(test_string_body_truncated_is_refused);
    RUN_TEST(test_string_header_truncated_is_refused);
    RUN_TEST(test_empty_string_is_whole);
    RUN_TEST(test_offset_is_honoured);
    RUN_TEST(test_sec8_empty_stream_is_valid);
    RUN_TEST(test_sec8_tty_op_end_terminates);
    RUN_TEST(test_sec8_one_pair_then_terminator);
    RUN_TEST(test_sec8_opcode_159_takes_an_argument);
    RUN_TEST(test_sec8_opcode_160_stops_parsing);
    RUN_TEST(test_sec8_truncated_argument_is_refused);
    RUN_TEST(test_sec8_opcode_with_no_argument_is_refused);
    RUN_TEST(test_sec8_consecutive_pairs);
    RUN_TEST(test_sec8_missing_terminator_keeps_the_whole_pairs);
    RUN_TEST(test_sec6_2_every_field_is_read);
    RUN_TEST(test_sec6_2_zero_dimensions_are_still_well_formed);
    RUN_TEST(test_sec6_2_both_dimension_pairs_are_carried);
    RUN_TEST(test_sec6_2_long_term_is_truncated_not_refused);
    RUN_TEST(test_sec6_2_missing_term_is_refused);
    RUN_TEST(test_sec6_2_truncated_dimensions_are_refused);
    RUN_TEST(test_sec6_2_missing_modes_string_is_refused);
    RUN_TEST(test_sec6_2_modes_stream_is_validated);
    RUN_TEST(test_sec6_2_offset_is_honoured);
    RUN_TEST(test_sec6_7_four_dimensions_are_read);
    RUN_TEST(test_sec6_7_truncated_is_refused);
    RUN_TEST(test_sec6_7_carries_no_term_or_modes);
    return UNITY_END();
}
