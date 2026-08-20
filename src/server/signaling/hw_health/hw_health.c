// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_health.c
 * @brief Hardware-health diagnostics (see hw_health.h).
 */

#include "server/signaling/hw_health/hw_health.h"
#include "mmgr/membuild/membuild.h" // protocore_sb frame builder

#if PROTOCORE_ENABLE_HW_HEALTH

void protocore_hw_health_rail_init(uint8_t *restrict work)
{
    (void)work;
    HwRailMonitor *m = HwHealthV.rail.m;
    if (!m)
    {
        return;
    }
    m->nominal_mv = HwHealthV.rail.nominal_mv;
    m->warn_mv = HwHealthV.rail.warn_mv;
    m->crit_mv = HwHealthV.rail.crit_mv;
    m->min_mv = HwHealthV.rail.nominal_mv;
    m->sag_events = 0;
    m->brownout_events = 0;
}

void protocore_hw_health_rail_sample(uint8_t *restrict work)
{
    (void)work;
    HwRailMonitor *m = HwHealthV.rail.m;
    const uint32_t mv = HwHealthV.rail.mv;

    HwHealthV.rail_verdict = HW_RAIL_OK;
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
        HwHealthV.rail_verdict = HW_RAIL_BROWNOUT;
        return;
    }
    if (mv < m->warn_mv)
    {
        m->sag_events++;
        HwHealthV.rail_verdict = HW_RAIL_SAG;
    }
}

void protocore_hw_health_rail_json(uint8_t *restrict work)
{
    (void)work;
    const HwRailMonitor *m = HwHealthV.rail.m_ro;
    char *out = HwHealthV.out_args.out;
    const size_t cap = HwHealthV.out_args.cap;

    HwHealthV.n = 0;
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
    HwHealthV.n = b.len;
}

void protocore_hw_health_spi_init(uint8_t *restrict work)
{
    (void)work;
    HwSpiBackoff *s = HwHealthV.spi.s;
    if (!s)
    {
        return;
    }
    s->min_hz = HwHealthV.spi.min_hz;
    s->max_hz = HwHealthV.spi.max_hz;
    if (HwHealthV.spi.start_hz < HwHealthV.spi.min_hz)
    {
        s->hz = HwHealthV.spi.min_hz;
    }
    else if (HwHealthV.spi.start_hz > HwHealthV.spi.max_hz)
    {
        s->hz = HwHealthV.spi.max_hz;
    }
    else
    {
        s->hz = HwHealthV.spi.start_hz;
    }
    s->fail_streak = 0;
    s->ok_streak = 0;
    s->fail_trip = HwHealthV.spi.fail_trip ? HwHealthV.spi.fail_trip : 1;
    s->ok_trip = HwHealthV.spi.ok_trip ? HwHealthV.spi.ok_trip : 1;
}

void protocore_hw_health_spi_result(uint8_t *restrict work)
{
    (void)work;
    HwSpiBackoff *s = HwHealthV.spi.s;

    HwHealthV.hz = 0;
    if (!s)
    {
        return;
    }
    if (HwHealthV.spi.crc_ok)
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
    HwHealthV.hz = s->hz;
}

void protocore_hw_health_gpio_short(uint8_t *restrict work)
{
    (void)work;
    if (HwHealthV.probe.driven_high && !HwHealthV.probe.read_high)
    {
        HwHealthV.gpio_verdict = HW_GPIO_SHORT_GND;
        return;
    }
    if (!HwHealthV.probe.driven_high && HwHealthV.probe.read_high)
    {
        HwHealthV.gpio_verdict = HW_GPIO_SHORT_VCC;
        return;
    }
    HwHealthV.gpio_verdict = HW_GPIO_OK;
}

void protocore_hw_health_cap_leak(uint8_t *restrict work)
{
    (void)work;
    const uint32_t measured_ms = HwHealthV.probe.measured_ms;
    const uint32_t expected_ms = HwHealthV.probe.expected_ms;

    HwHealthV.cap_verdict = HW_CAP_OK;
    if (expected_ms == 0)
    {
        return;
    }
    // Tolerance band around expected, computed in 64-bit to avoid overflow.
    uint64_t band = (uint64_t)expected_ms * HwHealthV.probe.tol_pct / 100;
    uint64_t lo = (uint64_t)expected_ms > band ? (uint64_t)expected_ms - band : 0;
    uint64_t hi = (uint64_t)expected_ms + band;
    if (measured_ms < lo)
    {
        HwHealthV.cap_verdict = HW_CAP_LEAK; // discharges too fast
        return;
    }
    if (measured_ms > hi)
    {
        HwHealthV.cap_verdict = HW_CAP_HIGH_ESR; // discharges too slow
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
HwHealthVars HwHealthV;

#endif // PROTOCORE_ENABLE_HW_HEALTH
