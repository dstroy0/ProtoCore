// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_flow_control.c
 * @brief SSH channel flow control - RFC 4254 sec 5.2 window arithmetic.
 */

#include "network_drivers/presentation/ssh/connection/ssh_flow_control.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h" // SSH_MSG_CHANNEL_*
#include "protocore_config.h"                                      // SSH_CHAN_MAX_PACKET
#include "mmgr/endian.h" // pc_wr32be - the one source of truth for wire integers

void pc_ssh_flow_init(SshFlow *f, uint32_t local_window, uint32_t peer_window, uint32_t peer_max_pkt)
{
    f->local_window = local_window;
    f->local_max = local_window;
    f->peer_window = peer_window;
    f->peer_max_pkt = peer_max_pkt;
}

proto_bool pc_ssh_flow_recv_take(SshFlow *f, uint32_t n)
{
    if (n > f->local_window)
    {
        return PROTO_FALSE; // peer overran the advertised window (RFC 4254 sec 5.2)
    }
    f->local_window -= n;
    return PROTO_TRUE;
}

proto_bool pc_ssh_flow_replenish_due(const SshFlow *f, uint32_t *add)
{
    if (f->local_window >= f->local_max / 2)
    {
        return PROTO_FALSE;
    }
    *add = f->local_max - f->local_window;
    return PROTO_TRUE;
}

void pc_ssh_flow_local_credit(SshFlow *f, uint32_t add)
{
    f->local_window += add;
}

proto_bool pc_ssh_flow_send_allows(const SshFlow *f, size_t len)
{
    return len <= f->peer_window && len <= f->peer_max_pkt;
}

uint32_t pc_ssh_flow_send_cap(const SshFlow *f, uint32_t want)
{
    uint32_t cap = want;
    if (cap > f->peer_window)
    {
        cap = f->peer_window;
    }
    if (cap > f->peer_max_pkt)
    {
        cap = f->peer_max_pkt;
    }
    return cap;
}

void pc_ssh_flow_send_take(SshFlow *f, uint32_t n)
{
    f->peer_window -= n;
}

void pc_ssh_flow_peer_add(SshFlow *f, uint32_t add)
{
    uint32_t w = f->peer_window;
    f->peer_window = (w + add < w) ? 0xFFFFFFFFu : (w + add);
}

uint32_t pc_ssh_flow_peer_window(const SshFlow *f)
{
    return f->peer_window;
}

// ---------------------------------------------------------------------------
// Channel signaling (RFC 4254 sec 5)
// ---------------------------------------------------------------------------

int32_t pc_ssh_sig_build_open_failure(uint8_t *out, size_t cap, uint32_t peer_id, uint32_t reason, size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_FAILURE;
    pc_wr32be(out + 1, peer_id);
    pc_wr32be(out + 5, reason);
    pc_wr32be(out + 9, 0);  // empty description
    pc_wr32be(out + 13, 0); // empty language
    *out_len = 17;
    return 0;
}

int32_t pc_ssh_sig_build_open_confirm(const SshFlow *f, uint32_t peer_id, uint32_t local_id, uint8_t *out, size_t cap,
                                      size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_CONFIRM;
    pc_wr32be(out + 1, peer_id);
    pc_wr32be(out + 5, local_id);
    pc_wr32be(out + 9, f->local_window);
    pc_wr32be(out + 13, SSH_CHAN_MAX_PACKET);
    *out_len = 17;
    return 0;
}

int32_t pc_ssh_sig_build_data(SshFlow *f, uint32_t peer_id, const uint8_t *data, size_t len, uint8_t *out, size_t cap,
                              size_t *out_len)
{
    if (!pc_ssh_flow_send_allows(f, len))
    {
        return -1; // would exceed the peer's window or its maximum packet size
    }
    if (cap < 9 + len)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_DATA;
    pc_wr32be(out + 1, peer_id);
    pc_wr32be(out + 5, (uint32_t)len);
    memcpy(out + 9, data, len);
    *out_len = 9 + len;
    pc_ssh_flow_send_take(f, (uint32_t)len);
    return 0;
}

int32_t pc_ssh_sig_build_window_adjust(uint32_t peer_id, uint32_t add, uint8_t *out, size_t cap, size_t *out_len)
{
    if (cap < 9)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    pc_wr32be(out + 1, peer_id);
    pc_wr32be(out + 5, add);
    *out_len = 9;
    return 0;
}

int32_t pc_ssh_sig_build_close(uint32_t peer_id, uint8_t *out, size_t cap, size_t *out_len)
{
    if (cap < 10)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_EOF;
    pc_wr32be(out + 1, peer_id);
    out[5] = SSH_MSG_CHANNEL_CLOSE;
    pc_wr32be(out + 6, peer_id);
    *out_len = 10;
    return 0;
}
