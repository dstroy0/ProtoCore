// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_health.cpp
 * @brief Hardware-health diagnostics (see hw_health.h).
 */

#include "services/system/hw_health/hw_health.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_HW_HEALTH

#include <string.h>

void pc_hwhealth_rail_init(HwRailMonitor *m, uint32_t nominal_mv, uint32_t warn_mv, uint32_t crit_mv)
{
    if (!m)
    {
        return;
    }
    m->nominal_mv = nominal_mv;
    m->warn_mv = warn_mv;
    m->crit_mv = crit_mv;
    m->min_mv = nominal_mv;
    m->sag_events = 0;
    m->brownout_events = 0;
}

HwRailVerdict pc_hwhealth_rail_sample(HwRailMonitor *m, uint32_t mv)
{
    if (!m)
    {
        return HwRailVerdict::HW_RAIL_OK;
    }
    if (mv < m->min_mv)
    {
        m->min_mv = mv;
    }
    if (mv < m->crit_mv)
    {
        m->brownout_events++;
        return HwRailVerdict::HW_RAIL_BROWNOUT;
    }
    if (mv < m->warn_mv)
    {
        m->sag_events++;
        return HwRailVerdict::HW_RAIL_SAG;
    }
    return HwRailVerdict::HW_RAIL_OK;
}

size_t pc_hwhealth_rail_json(const HwRailMonitor *m, char *out, size_t cap)
{
    if (!m || !out || cap == 0)
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, true};
    pc_sb_put(&b, "{\"nominal_mv\":");
    pc_sb_u32(&b, m->nominal_mv);
    pc_sb_put(&b, ",\"min_mv\":");
    pc_sb_u32(&b, m->min_mv);
    pc_sb_put(&b, ",\"sag\":");
    pc_sb_u32(&b, m->sag_events);
    pc_sb_put(&b, ",\"brownout\":");
    pc_sb_u32(&b, m->brownout_events);
    pc_sb_put(&b, "}");
    if (!b.ok)
    {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}

void pc_hwhealth_spi_init(HwSpiBackoff *s, uint32_t start_hz, uint32_t min_hz, uint32_t max_hz, uint16_t fail_trip,
                          uint16_t ok_trip)
{
    if (!s)
    {
        return;
    }
    s->min_hz = min_hz;
    s->max_hz = max_hz;
    if (start_hz < min_hz)
    {
        s->hz = min_hz;
    }
    else if (start_hz > max_hz)
    {
        s->hz = max_hz;
    }
    else
    {
        s->hz = start_hz;
    }
    s->fail_streak = 0;
    s->ok_streak = 0;
    s->fail_trip = fail_trip ? fail_trip : 1;
    s->ok_trip = ok_trip ? ok_trip : 1;
}

uint32_t pc_hwhealth_spi_result(HwSpiBackoff *s, bool crc_ok)
{
    if (!s)
    {
        return 0;
    }
    if (crc_ok)
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
    return s->hz;
}

HwGpioVerdict pc_hwhealth_gpio_short(bool driven_high, bool read_high)
{
    if (driven_high && !read_high)
    {
        return HwGpioVerdict::HW_GPIO_SHORT_GND;
    }
    if (!driven_high && read_high)
    {
        return HwGpioVerdict::HW_GPIO_SHORT_VCC;
    }
    return HwGpioVerdict::HW_GPIO_OK;
}

HwCapVerdict pc_hwhealth_cap_leak(uint32_t measured_ms, uint32_t expected_ms, uint8_t tol_pct)
{
    if (expected_ms == 0)
    {
        return HwCapVerdict::HW_CAP_OK;
    }
    // Tolerance band around expected, computed in 64-bit to avoid overflow.
    uint64_t band = (uint64_t)expected_ms * tol_pct / 100;
    uint64_t lo = (uint64_t)expected_ms > band ? (uint64_t)expected_ms - band : 0;
    uint64_t hi = (uint64_t)expected_ms + band;
    if (measured_ms < lo)
    {
        return HwCapVerdict::HW_CAP_LEAK; // discharges too fast
    }
    if (measured_ms > hi)
    {
        return HwCapVerdict::HW_CAP_HIGH_ESR; // discharges too slow
    }
    return HwCapVerdict::HW_CAP_OK;
}

#endif // PC_ENABLE_HW_HEALTH
