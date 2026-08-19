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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_RCWL0516

#include "mmgr/plaintext/plaintext.h"     // the persistent end this module's state is taken from
#include "server/clock/clock.h" // Clock.millis
#include "server/peripherals/rcwl0516/rcwl0516.h"

PROTOCORE_BEGIN_DECLS

#if !PROTOCORE_HAS_GPIO
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_RCWL0516 needs a GPIO seam. Provide one in test/core_setup/hal/<vendor>, or turn the driver\
 off - there is no software stand-in for a part on the other end of a wire."
#endif

// Elapsed-time test, wrap-safe across a Clock.ms rollover (unsigned arithmetic is modulo 2^32).
// A limit of 0 is always satisfied, which is what disables debounce / hold.
static inline proto_bool elapsed(uint32_t now, uint32_t since, uint32_t limit)
{
    return (now - since) >= limit;
}

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_RCWL0516_BORROW persistent bytes, or null while the pool was short
} Rcwl0516OwnCtx;
static Rcwl0516OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_rcwl0516_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_RCWL0516_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void rcwl0516_core_init(uint8_t *restrict work);
static void rcwl0516_presence_get(uint8_t *restrict work);
static void rcwl0516_presence_init(uint8_t *restrict work);
static void rcwl0516_presence_take_event(uint8_t *restrict work);
static void rcwl0516_presence_update(uint8_t *restrict work);

static void rcwl0516_presence_init(uint8_t *restrict work)
{
    (void)work;
    PresenceCore *c = Rcwl0516.presence_init_args.c;
    uint32_t debounce_ms = Rcwl0516.presence_init_args.debounce_ms;
    uint32_t hold_ms = Rcwl0516.presence_init_args.hold_ms;
    uint32_t now = Rcwl0516.presence_init_args.now;

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

static void rcwl0516_presence_update(uint8_t *restrict work)
{
    (void)work;
    PresenceCore *c = Rcwl0516.presence_update_args.c;
    proto_bool pin_high = Rcwl0516.presence_update_args.pin_high;
    uint32_t now = Rcwl0516.presence_update_args.now;

    if (!c)
    {
        Rcwl0516.ok = PROTO_FALSE;
        return;
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
    Rcwl0516.ok = c->present != 0;
}

static void rcwl0516_presence_get(uint8_t *restrict work)
{
    (void)work;
    const PresenceCore *c = Rcwl0516.presence_get_args.c;

    Rcwl0516.ok = c && c->present != 0;
}

static void rcwl0516_presence_take_event(uint8_t *restrict work)
{
    (void)work;
    PresenceCore *c = Rcwl0516.presence_take_event_args.c;

    if (!c || !c->changed)
    {
        Rcwl0516.ok = PROTO_FALSE;
        return;
    }
    c->changed = 0;
    Rcwl0516.ok = PROTO_TRUE;
}

static void rcwl0516_core_init(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    PresenceCore *c = Rcwl0516.core_init_args.c;
    uint32_t now = Rcwl0516.core_init_args.now;

    Rcwl0516.presence_init_args.c = c;
    Rcwl0516.presence_init_args.debounce_ms = PROTOCORE_RCWL0516_DEBOUNCE_MS;
    Rcwl0516.presence_init_args.hold_ms = PROTOCORE_RCWL0516_HOLD_MS;
    Rcwl0516.presence_init_args.now = now;
    rcwl0516_presence_init(work);
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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define RCWL0516_OFF_CTX 0u
static_assert(RCWL0516_OFF_CTX + sizeof(Rcwl0516Ctx) <= PROTOCORE_RCWL0516_BORROW,
              "PROTOCORE_RCWL0516_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define RCWL0516_CTX(w) ((Rcwl0516Ctx *)(void *)((w) + RCWL0516_OFF_CTX))

// The pin, or -1 for "there is none" - which is what a failed or absent begin() reports, the way a
// main() reports failure. Stated here rather than as an initializer on the declaration so the
// context carries none and can live in a borrow that arrives zeroed. It takes a flag rather than a
// sentinel value because pin 0 is a real pin, so zero cannot mean "unset". A caller that hands
// begin() a negative pin still lands on -1 here, and the poll below still refuses.
static int dev_pin(uint8_t *restrict work)
{
    return RCWL0516_CTX(work)->begun ? RCWL0516_CTX(work)->pin : -1;
}

static void rcwl0516_begin(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    int out_pin = Rcwl0516.begin_args.out_pin;

    RCWL0516_CTX(work)->pin = out_pin;
    RCWL0516_CTX(work)->begun = PROTO_TRUE;
    protocore_platform_gpio_mode((uint8_t)(out_pin),
                                 PROTOCORE_GPIO_IN); // the module drives OUT actively; no pull needed
    Rcwl0516.core_init_args.c = &RCWL0516_CTX(work)->core;
    Rcwl0516.core_init_args.now = Clock.ms;
    rcwl0516_core_init(work);
    Rcwl0516.ok = PROTO_TRUE;
}

static void rcwl0516_poll(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }

    const int pin = dev_pin(work);
    if (pin < 0)
    {
        Rcwl0516.ok = PROTO_FALSE;
        return;
    }
    Rcwl0516.presence_update_args.c = &RCWL0516_CTX(work)->core;
    Rcwl0516.presence_update_args.pin_high = protocore_platform_gpio_read((uint8_t)(pin)) == PROTOCORE_GPIO_HIGH;
    Rcwl0516.presence_update_args.now = Clock.ms;
    rcwl0516_presence_update(work);
    Rcwl0516.presence_take_event_args.c = &RCWL0516_CTX(work)->core;
    rcwl0516_presence_take_event(work);
}

static void rcwl0516_present(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }

    Rcwl0516.presence_get_args.c = &RCWL0516_CTX(work)->core;
    rcwl0516_presence_get(work);
}

Rcwl0516Ns Rcwl0516 = {
    .presence_init = rcwl0516_presence_init,
    .presence_update = rcwl0516_presence_update,
    .presence_get = rcwl0516_presence_get,
    .presence_take_event = rcwl0516_presence_take_event,
    .core_init = rcwl0516_core_init,
    .begin = rcwl0516_begin,
    .poll = rcwl0516_poll,
    .present = rcwl0516_present,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RCWL0516
