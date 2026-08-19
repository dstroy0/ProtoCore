// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file signaling.c
 * @brief The bucket: deposits in from the loop, one copy out to a reader, and the kill forward.
 *
 * Every function is a store, a copy, or a forward. Signaling originates nothing - the loop
 * establishes each fact and deposits it. See signaling.h.
 */

#include "server/signaling/signaling/signaling.h"
#include "mmgr/plaintext/plaintext.h"                                  // the persistent end this module's state is taken from
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a close names
#include "network_drivers/transport/tcp/tcp.h"

/**
 * @brief The bucket's compile-time storage: the one snapshot every deposit lands in.
 */
struct SignalingStorage
{
    protocore_signal_snapshot state;
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SIGNALING_OFF_CTX 0u
static_assert(SIGNALING_OFF_CTX + sizeof(struct SignalingStorage) <= PROTOCORE_SIGNALING_BORROW,
              "PROTOCORE_SIGNALING_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SIGNALING_CTX(w) ((struct SignalingStorage *)(void *)((w) + SIGNALING_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SIGNALING_BORROW persistent bytes, or null while the pool was short
} SignalOwnCtx;
static SignalOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_signaling_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_SIGNALING_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void signal_put_response(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const int code = Signal.put.code;

    SIGNALING_CTX(work)->state.requests_total++;
    if (code >= 200 && code < 300)
    {
        SIGNALING_CTX(work)->state.responses_2xx++;
    }
    else if (code >= 400 && code < 500)
    {
        SIGNALING_CTX(work)->state.responses_4xx++;
    }
    else if (code >= 500 && code < 600)
    {
        SIGNALING_CTX(work)->state.responses_5xx++;
    }
}

static void signal_put_tick(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    SIGNALING_CTX(work)->state.uptime_ms = Signal.put.uptime_ms;
    SIGNALING_CTX(work)->state.conns_active = Signal.put.conns_active;
    SIGNALING_CTX(work)->state.listeners_up = Signal.put.listeners_up;
}

static void signal_know(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (Signal.out == NULL)
    {
        return;
    }
    // A copy, not a pointer into the bucket. A reader formats several fields and the loop deposits
    // between its reads, so handing out the storage would let one report mix two server states.
    *Signal.out = SIGNALING_CTX(work)->state;
}

static void signal_reset(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    // The tallies are per-run: a server that has started over has answered no requests. Zero is the
    // bucket's initial state, so the reset is the same store the static initialization performs.
    static const struct SignalingStorage blank = {0};
    *SIGNALING_CTX(work) = blank;
}

static void signal_kill(uint8_t *restrict work)
{
    (void)work;
    // A plain forward: no liveness test, no result. Transport owns the slot's lifetime and its idle
    // sweep reaps a stale one regardless, so a check here would answer a question transport has
    // already answered, and the answer could be stale before the caller read it.
    ConnPool.slot = Signal.slot;
    ConnPool.close(protocore_conn_pool_span());
}

// Designated, so a member's position in the struct does not decide what it binds to.
SignalingNs Signal = {.know = signal_know,
                      .reset = signal_reset,
                      .put_response = signal_put_response,
                      .put_tick = signal_put_tick,
                      .kill = signal_kill};
