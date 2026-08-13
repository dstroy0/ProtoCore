// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file clock.c
 * @brief The pluggable time source - state and readers. See clock.h.
 *
 * The override lives here rather than in the header because a caller installing a clock and the
 * library reading it are different translation units, and they have to see the same value. A
 * header-only `static` would give each one its own copy, so a test that installed a clock would
 * not affect the code under test.
 */

#include "server/clock/clock.h"

/**
 * @brief The clocks' compile-time storage: each installed source and the factor it divides by.
 */
struct ClockStorage
{
    protocore_clock_fn ms_fn;
    uint32_t ms_div;
    protocore_clock_fn us_fn;
    uint32_t us_div;
};

/**
 * @brief The installed clocks and the calls that read them - what ClockNs points at.
 *
 * @var ClockInternal::store  each installed source and the factor it divides by
 * @var ClockInternal::ns     the handle a caller sets a call's members on
 */
struct ClockInternal
{
    struct ClockStorage *store;
    ClockNs *ns;
};

static struct ClockStorage s_store = {NULL, 1u, NULL, 1u};

static struct ClockInternal s_clock = {.store = &s_store, .ns = &Clock};

static void clock_set_ms(struct ClockInternal *restrict ctx)
{
    const uint32_t rate = ctx->ns->src.ticks_per_second;
    ctx->store->ms_fn = ctx->ns->src.fn;
    ctx->store->ms_div = (rate >= 1000u) ? (rate / 1000u) : 1u;
}

static void clock_millis(struct ClockInternal *restrict ctx)
{
    if (ctx->store->ms_fn)
    {
        ctx->ns->ms = ctx->store->ms_fn() / ctx->store->ms_div;
        return;
    }
    ctx->ns->ms = protocore_platform_millis();
}

static void clock_set_us(struct ClockInternal *restrict ctx)
{
    const uint32_t rate = ctx->ns->src.ticks_per_second;
    ctx->store->us_fn = ctx->ns->src.fn;
    ctx->store->us_div = (rate >= 1000000u) ? (rate / 1000000u) : 1u;
}

static void clock_micros(struct ClockInternal *restrict ctx)
{
    if (ctx->store->us_fn)
    {
        ctx->ns->us = ctx->store->us_fn() / ctx->store->us_div;
        return;
    }
    ctx->ns->us = protocore_platform_micros();
}

static void clock_cycles(struct ClockInternal *restrict ctx)
{
    ctx->ns->cyc = protocore_platform_cycles();
}

// Designated, so a member's position in the struct does not decide what it binds to.
ClockNs Clock = {.set_ms = clock_set_ms,
                 .millis = clock_millis,
                 .set_us = clock_set_us,
                 .micros = clock_micros,
                 .cycles = clock_cycles,
                 .internal = &s_clock};
