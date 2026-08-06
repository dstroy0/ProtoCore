// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file signaling.c
 * @brief The bucket: deposits in from the loop, one copy out to a reader, and the kill forward.
 *
 * Every function is a store, a copy, or a forward. Signaling originates nothing - the loop
 * establishes each fact and deposits it. See signaling.h.
 */

#include "server/signaling/signaling.h"
#include "network_drivers/transport/tcp.h"

// The deposited bucket, one owner with internal linkage: storage for facts the loop established,
// never a source of them. Static storage zero-initializes, so a read taken before the first tick
// answers an idle server rather than garbage.
typedef struct
{
    pc_signal_snapshot state;
} SignalingCtx;
static SignalingCtx s_sig;

void pc_signal_put_response(int code)
{
    s_sig.state.requests_total++;

    // The class is the leading digit, so one divide yields it. A ladder of range compares would
    // re-derive what the division already answered, and would have to agree with itself about the
    // boundaries in three places.
    int cls = code / 100;
    if (cls == 2)
    {
        s_sig.state.responses_2xx++;
    }
    else if (cls == 4)
    {
        s_sig.state.responses_4xx++;
    }
    else if (cls == 5)
    {
        s_sig.state.responses_5xx++;
    }
    // 1xx and 3xx count toward the total and carry no class tally of their own: nothing reads one,
    // and a counter nobody reads is state that can only be wrong.
}

void pc_signal_put_tick(uint32_t uptime_ms, uint32_t conns_active, uint32_t listeners_up)
{
    s_sig.state.uptime_ms = uptime_ms;
    s_sig.state.conns_active = conns_active;
    s_sig.state.listeners_up = listeners_up;
}

void pc_signal_know(pc_signal_snapshot *out)
{
    if (out == NULL)
    {
        return;
    }
    // A copy, not a pointer into the bucket. A reader formats several fields and the loop deposits
    // between its reads, so handing out the storage would let one report mix two server states.
    *out = s_sig.state;
}

void pc_signal_reset(void)
{
    // The tallies are per-run: a server that has started over has answered no requests. Zero is the
    // bucket's initial state, so the reset is the same store the static initialization performs.
    static const SignalingCtx blank = {0};
    s_sig = blank;
}

void pc_signal_kill(uint8_t slot)
{
    // A plain forward: no liveness test, no result. Transport owns the slot's lifetime and its idle
    // sweep reaps a stale one regardless, so a check here would answer a question transport has
    // already answered, and the answer could be stale before the caller read it.
    Tcp.conn->close(slot);
}
