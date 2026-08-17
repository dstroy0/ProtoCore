// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rcwl0516.c
 * @brief One-GPIO presence facade - implementation. See rcwl0516.h.
 *
 * Three steps per sample: track how long the raw level has been steady, promote it to the believed
 * level once it outlasts the debounce, then extend presence for the hold past the last believed
 * HIGH. Every elapsed-time test is an unsigned difference (`now - stamp >= limit`), which is exactly
 * what makes it wrap-safe: at a Clock.ms rollover the subtraction wraps with it and still yields the
 * true elapsed interval.
 */

#include "server/peripherals/rcwl0516/rcwl0516.h"
#include "server/clock/clock.h" // Clock.millis

#if PROTOCORE_ENABLE_RCWL0516

#if !PROTOCORE_HAS_GPIO
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_RCWL0516 needs a GPIO seam. Provide one in core_setup/hal/<vendor>, or turn the driver\
 off - there is no software stand-in for a part on the other end of a wire."
#endif

// Elapsed-time test, wrap-safe across a Clock.ms rollover (unsigned arithmetic is modulo 2^32).
// A limit of 0 is always satisfied, which is what disables debounce / hold.
static inline proto_bool elapsed(uint32_t now, uint32_t since, uint32_t limit)
{
    return (now - since) >= limit;
}

void protocore_presence_core_init(PresenceCore *c, uint32_t debounce_ms, uint32_t hold_ms, uint32_t now)
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

proto_bool protocore_presence_core_update(PresenceCore *c, proto_bool pin_high, uint32_t now)
{
    if (!c)
    {
        return PROTO_FALSE;
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

proto_bool protocore_presence_core_get(const PresenceCore *c)
{
    return c && c->present != 0;
}

proto_bool protocore_presence_take_event(PresenceCore *c)
{
    if (!c || !c->changed)
    {
        return PROTO_FALSE;
    }
    c->changed = 0;
    return PROTO_TRUE;
}

void protocore_rcwl0516_core_init(PresenceCore *c, uint32_t now)
{
    protocore_presence_core_init(c, PROTOCORE_RCWL0516_DEBOUNCE_MS, PROTOCORE_RCWL0516_HOLD_MS, now);
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

// All RCWL-0516 binding state, owned by one instance (internal linkage): the presence core and the
// pin it samples, grouped so it is one named owner unreachable from any other translation unit.
typedef struct
{
    PresenceCore core;
    int pin;
    proto_bool begun; ///< begin() ran; until it has, the pin reports -1
} Rcwl0516Ctx;
static Rcwl0516Ctx s_rcwl;

// The pin, or -1 for "there is none" - which is what a failed or absent begin() reports, the way a
// main() reports failure. Stated here rather than as an initializer on the declaration so the
// context carries none and can live in a borrow that arrives zeroed. It takes a flag rather than a
// sentinel value because pin 0 is a real pin, so zero cannot mean "unset". A caller that hands
// begin() a negative pin still lands on -1 here, and the poll below still refuses.
static int dev_pin(void)
{
    return s_rcwl.begun ? s_rcwl.pin : -1;
}

proto_bool protocore_rcwl0516_begin(int out_pin)
{
    s_rcwl.pin = out_pin;
    s_rcwl.begun = PROTO_TRUE;
    protocore_platform_gpio_mode((uint8_t)(out_pin),
                                 PROTOCORE_GPIO_IN); // the module drives OUT actively; no pull needed
    protocore_rcwl0516_core_init(&s_rcwl.core, Clock.ms);
    return PROTO_TRUE;
}

proto_bool protocore_rcwl0516_poll()
{
    const int pin = dev_pin();
    if (pin < 0)
    {
        return PROTO_FALSE;
    }
    protocore_presence_core_update(&s_rcwl.core, protocore_platform_gpio_read((uint8_t)(pin)) == PROTOCORE_GPIO_HIGH,
                                   Clock.ms);
    return protocore_presence_take_event(&s_rcwl.core);
}

proto_bool protocore_rcwl0516_present()
{
    return protocore_presence_core_get(&s_rcwl.core);
}

#endif // PROTOCORE_ENABLE_RCWL0516
