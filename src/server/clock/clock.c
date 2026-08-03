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

// The installed clocks and their divisors, owned by one instance (internal linkage): the
// millisecond source, the microsecond source, and the factor each is divided by to reach the
// library's own rate. One named owner, unreachable from any other translation unit.
typedef struct
{
    pc_clock_fn ms_fn;
    uint32_t ms_div;
    pc_clock_fn us_fn;
    uint32_t us_div;
} ClockCtx;
static ClockCtx s_clock = {NULL, 1u, NULL, 1u};

void pc_set_clock(pc_clock_fn fn, uint32_t ticks_per_second)
{
    s_clock.ms_fn = fn;
    s_clock.ms_div = (ticks_per_second >= 1000u) ? (ticks_per_second / 1000u) : 1u;
}

uint32_t pc_millis(void)
{
    if (s_clock.ms_fn)
    {
        return s_clock.ms_fn() / s_clock.ms_div;
    }
    return pc_platform_millis();
}

void pc_set_micros_clock(pc_clock_fn fn, uint32_t ticks_per_second)
{
    s_clock.us_fn = fn;
    s_clock.us_div = (ticks_per_second >= 1000000u) ? (ticks_per_second / 1000000u) : 1u;
}

uint32_t pc_micros(void)
{
    if (s_clock.us_fn)
    {
        return s_clock.us_fn() / s_clock.us_div;
    }
    return pc_platform_micros();
}

uint32_t pc_cycles(void)
{
    return pc_platform_cycles();
}
