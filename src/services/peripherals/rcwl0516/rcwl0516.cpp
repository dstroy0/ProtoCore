// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rcwl0516.cpp
 * @brief One-GPIO presence facade - implementation. See rcwl0516.h.
 *
 * Three steps per sample: track how long the raw level has been steady, promote it to the believed
 * level once it outlasts the debounce, then extend presence for the hold past the last believed
 * HIGH. Every elapsed-time test is an unsigned difference (`now - stamp >= limit`), which is exactly
 * what makes it wrap-safe: at a millis() rollover the subtraction wraps with it and still yields the
 * true elapsed interval.
 */

#include "services/peripherals/rcwl0516/rcwl0516.h"

#if PC_ENABLE_RCWL0516

#if defined(ARDUINO)
#include <Arduino.h>
#endif
namespace
{
// Elapsed-time test, wrap-safe across a millis() rollover (unsigned arithmetic is modulo 2^32).
// A limit of 0 is always satisfied, which is what disables debounce / hold.
inline bool elapsed(uint32_t now, uint32_t since, uint32_t limit)
{
    return (now - since) >= limit;
}
} // namespace

void pc_presence_core_init(PresenceCore *c, uint32_t debounce_ms, uint32_t hold_ms, uint32_t now)
{
    if (!c)
    {
        return;
    }
    c->debounce_ms = debounce_ms;
    c->hold_ms = hold_ms;
    c->raw_since_ms = now;
    c->last_high_ms = now;
    c->raw = 0;
    c->stable = 0;
    c->present = 0; // fail-safe: absent until observed
    c->changed = 0;
}

bool pc_presence_core_update(PresenceCore *c, bool pin_high, uint32_t now)
{
    if (!c)
    {
        return false;
    }
    const uint8_t lvl = pin_high ? 1u : 0u;

    // 1) restart the debounce whenever the raw level moves
    if (lvl != c->raw)
    {
        c->raw = lvl;
        c->raw_since_ms = now;
    }

    // 2) believe the raw level once it has outlasted the debounce
    if (elapsed(now, c->raw_since_ms, c->debounce_ms))
    {
        c->stable = c->raw;
    }

    // 3) presence follows the believed level, but decays only after the hold
    const uint8_t was = c->present;
    if (c->stable)
    {
        c->last_high_ms = now;
        c->present = 1;
    }
    else if (c->present && elapsed(now, c->last_high_ms, c->hold_ms))
    {
        c->present = 0;
    }

    if (c->present != was)
    {
        c->changed = 1;
    }
    return c->present != 0;
}

bool pc_presence_core_get(const PresenceCore *c)
{
    return c && c->present != 0;
}

bool pc_presence_take_event(PresenceCore *c)
{
    if (!c || !c->changed)
    {
        return false;
    }
    c->changed = 0;
    return true;
}

void pc_rcwl0516_core_init(PresenceCore *c, uint32_t now)
{
    pc_presence_core_init(c, PC_RCWL0516_DEBOUNCE_MS, PC_RCWL0516_HOLD_MS, now);
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

#if defined(ARDUINO)

namespace
{
// All RCWL-0516 binding state, owned by one instance (internal linkage): the presence core and the
// pin it samples, grouped so it is one named owner unreachable from any other translation unit.
struct Rcwl0516Ctx
{
    PresenceCore core;
    int pin = -1;
};
Rcwl0516Ctx s_rcwl;
} // namespace

bool pc_rcwl0516_begin(int out_pin)
{
    s_rcwl.pin = out_pin;
    pinMode(out_pin, INPUT); // the module drives OUT actively; no pull needed
    pc_rcwl0516_core_init(&s_rcwl.core, (uint32_t)millis());
    return true;
}

bool pc_rcwl0516_poll()
{
    if (s_rcwl.pin < 0)
    {
        return false;
    }
    pc_presence_core_update(&s_rcwl.core, digitalRead(s_rcwl.pin) == HIGH, (uint32_t)millis());
    return pc_presence_take_event(&s_rcwl.core);
}

bool pc_rcwl0516_present()
{
    return pc_presence_core_get(&s_rcwl.core);
}

#else // host build: no GPIO

bool pc_rcwl0516_begin(int)
{
    return false;
}

bool pc_rcwl0516_poll()
{
    return false;
}

bool pc_rcwl0516_present()
{
    return false;
}

#endif // ARDUINO

#endif // PC_ENABLE_RCWL0516
