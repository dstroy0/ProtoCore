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
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a close names
#include "network_drivers/transport/tcp/tcp.h"

/**
 * @brief The bucket's compile-time storage: the one snapshot every deposit lands in.
 */
struct SignalingStorage
{
    protocore_signal_snapshot state;
};

/**
 * @brief The bucket and the calls that reach it - what SignalingNs points at.
 *
 * @var SignalingInternal::store  the snapshot every deposit lands in
 * @var SignalingInternal::ns     the handle a caller sets a call's members on
 */
struct SignalingInternal
{
    struct SignalingStorage *store;
    SignalingNs *ns;
};

static struct SignalingStorage s_store;

static struct SignalingInternal s_sig = {.store = &s_store, .ns = &Signal};

static void signal_put_response(struct SignalingInternal *restrict ctx)
{
    const int code = ctx->ns->put.code;

    ctx->store->state.requests_total++;
    if (code >= 200 && code < 300)
    {
        ctx->store->state.responses_2xx++;
    }
    else if (code >= 400 && code < 500)
    {
        ctx->store->state.responses_4xx++;
    }
    else if (code >= 500 && code < 600)
    {
        ctx->store->state.responses_5xx++;
    }
}

static void signal_put_tick(struct SignalingInternal *restrict ctx)
{
    ctx->store->state.uptime_ms = ctx->ns->put.uptime_ms;
    ctx->store->state.conns_active = ctx->ns->put.conns_active;
    ctx->store->state.listeners_up = ctx->ns->put.listeners_up;
}

static void signal_know(struct SignalingInternal *restrict ctx)
{
    if (ctx->ns->out == NULL)
    {
        return;
    }
    // A copy, not a pointer into the bucket. A reader formats several fields and the loop deposits
    // between its reads, so handing out the storage would let one report mix two server states.
    *ctx->ns->out = ctx->store->state;
}

static void signal_reset(struct SignalingInternal *restrict ctx)
{
    // The tallies are per-run: a server that has started over has answered no requests. Zero is the
    // bucket's initial state, so the reset is the same store the static initialization performs.
    static const struct SignalingStorage blank = {0};
    *ctx->store = blank;
}

static void signal_kill(struct SignalingInternal *restrict ctx)
{
    // A plain forward: no liveness test, no result. Transport owns the slot's lifetime and its idle
    // sweep reaps a stale one regardless, so a check here would answer a question transport has
    // already answered, and the answer could be stale before the caller read it.
    ConnPool.slot = ctx->ns->slot;
    ConnPool.close(ConnPool.internal);
}

// Designated, so a member's position in the struct does not decide what it binds to.
SignalingNs Signal = {.know = signal_know,
                      .reset = signal_reset,
                      .put_response = signal_put_response,
                      .put_tick = signal_put_tick,
                      .kill = signal_kill,
                      .internal = &s_sig};
