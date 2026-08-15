// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_health.c
 * @brief Hardware-health diagnostics (see hw_health.h).
 */

#include "server/signaling/hw_health.h"
#include "mmgr/membuild.h" // protocore_sb frame builder

#if PROTOCORE_ENABLE_HW_HEALTH

/**
 * @brief The checks' calls - what HwHealthNs points at.
 *
 * @var HwHealthInternal::ns  the handle a caller sets a call's members on
 */
struct HwHealthInternal
{
    HwHealthNs *ns;
};

static struct HwHealthInternal s_hw = {.ns = &HwHealth};

static void rail_init(struct HwHealthInternal *restrict ctx)
{
    HwRailMonitor *m = ctx->ns->rail.m;
    if (!m)
    {
        return;
    }
    m->nominal_mv = ctx->ns->rail.nominal_mv;
    m->warn_mv = ctx->ns->rail.warn_mv;
    m->crit_mv = ctx->ns->rail.crit_mv;
    m->min_mv = ctx->ns->rail.nominal_mv;
    m->sag_events = 0;
    m->brownout_events = 0;
}

static void rail_sample(struct HwHealthInternal *restrict ctx)
{
    HwRailMonitor *m = ctx->ns->rail.m;
    const uint32_t mv = ctx->ns->rail.mv;

    ctx->ns->rail_verdict = HW_RAIL_OK;
    if (!m)
    {
        return;
    }
    if (mv < m->min_mv)
    {
        m->min_mv = mv;
    }
    if (mv < m->crit_mv)
    {
        m->brownout_events++;
        ctx->ns->rail_verdict = HW_RAIL_BROWNOUT;
        return;
    }
    if (mv < m->warn_mv)
    {
        m->sag_events++;
        ctx->ns->rail_verdict = HW_RAIL_SAG;
    }
}

static void rail_json(struct HwHealthInternal *restrict ctx)
{
    const HwRailMonitor *m = ctx->ns->rail.m_ro;
    char *out = ctx->ns->out_args.out;
    const size_t cap = ctx->ns->out_args.cap;

    ctx->ns->n = 0;
    if (!m || !out || cap == 0)
    {
        return;
    }
    protocore_sb b = {out, cap, 0, PROTO_TRUE};
    Sb.put(&b, "{\"nominal_mv\":");
    Sb.u32(&b, m->nominal_mv);
    Sb.put(&b, ",\"min_mv\":");
    Sb.u32(&b, m->min_mv);
    Sb.put(&b, ",\"sag\":");
    Sb.u32(&b, m->sag_events);
    Sb.put(&b, ",\"brownout\":");
    Sb.u32(&b, m->brownout_events);
    Sb.put(&b, "}");
    if (!b.ok)
    {
        return;
    }
    out[b.len] = '\0';
    ctx->ns->n = b.len;
}

static void spi_init(struct HwHealthInternal *restrict ctx)
{
    HwSpiBackoff *s = ctx->ns->spi.s;
    if (!s)
    {
        return;
    }
    s->min_hz = ctx->ns->spi.min_hz;
    s->max_hz = ctx->ns->spi.max_hz;
    if (ctx->ns->spi.start_hz < ctx->ns->spi.min_hz)
    {
        s->hz = ctx->ns->spi.min_hz;
    }
    else if (ctx->ns->spi.start_hz > ctx->ns->spi.max_hz)
    {
        s->hz = ctx->ns->spi.max_hz;
    }
    else
    {
        s->hz = ctx->ns->spi.start_hz;
    }
    s->fail_streak = 0;
    s->ok_streak = 0;
    s->fail_trip = ctx->ns->spi.fail_trip ? ctx->ns->spi.fail_trip : 1;
    s->ok_trip = ctx->ns->spi.ok_trip ? ctx->ns->spi.ok_trip : 1;
}

static void spi_result(struct HwHealthInternal *restrict ctx)
{
    HwSpiBackoff *s = ctx->ns->spi.s;

    ctx->ns->hz = 0;
    if (!s)
    {
        return;
    }
    if (ctx->ns->spi.crc_ok)
    {
        s->fail_streak = 0;
        if (++s->ok_streak >= s->ok_trip)
        {
            s->ok_streak = 0;
            uint32_t up = s->hz << 1;
            if (up < s->hz || up > s->max_hz) // overflow or past ceiling
            {
                up = s->max_hz;
            }
            s->hz = up;
        }
    }
    else
    {
        s->ok_streak = 0;
        if (++s->fail_streak >= s->fail_trip)
        {
            s->fail_streak = 0;
            uint32_t down = s->hz >> 1;
            if (down < s->min_hz)
            {
                down = s->min_hz;
            }
            s->hz = down;
        }
    }
    ctx->ns->hz = s->hz;
}

static void gpio_short(struct HwHealthInternal *restrict ctx)
{
    if (ctx->ns->probe.driven_high && !ctx->ns->probe.read_high)
    {
        ctx->ns->gpio_verdict = HW_GPIO_SHORT_GND;
        return;
    }
    if (!ctx->ns->probe.driven_high && ctx->ns->probe.read_high)
    {
        ctx->ns->gpio_verdict = HW_GPIO_SHORT_VCC;
        return;
    }
    ctx->ns->gpio_verdict = HW_GPIO_OK;
}

static void cap_leak(struct HwHealthInternal *restrict ctx)
{
    const uint32_t measured_ms = ctx->ns->probe.measured_ms;
    const uint32_t expected_ms = ctx->ns->probe.expected_ms;

    ctx->ns->cap_verdict = HW_CAP_OK;
    if (expected_ms == 0)
    {
        return;
    }
    // Tolerance band around expected, computed in 64-bit to avoid overflow.
    uint64_t band = (uint64_t)expected_ms * ctx->ns->probe.tol_pct / 100;
    uint64_t lo = (uint64_t)expected_ms > band ? (uint64_t)expected_ms - band : 0;
    uint64_t hi = (uint64_t)expected_ms + band;
    if (measured_ms < lo)
    {
        ctx->ns->cap_verdict = HW_CAP_LEAK; // discharges too fast
        return;
    }
    if (measured_ms > hi)
    {
        ctx->ns->cap_verdict = HW_CAP_HIGH_ESR; // discharges too slow
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
HwHealthNs HwHealth = {.rail_init = rail_init,
                       .rail_sample = rail_sample,
                       .rail_json = rail_json,
                       .spi_init = spi_init,
                       .spi_result = spi_result,
                       .gpio_short = gpio_short,
                       .cap_leak = cap_leak,
                       .internal = &s_hw};

#endif // PROTOCORE_ENABLE_HW_HEALTH
