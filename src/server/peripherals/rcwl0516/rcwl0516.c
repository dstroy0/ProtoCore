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

#include "protocore_config.h" // the entry point: the widths

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "server/clock/clock.h"       // Clock.millis
#include "server/peripherals/rcwl0516/rcwl0516.h"

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
    uint8_t *span; ///< PROTOCORE_RCWL0516_BORROW persistent bytes
} Rcwl0516OwnCtx;
static Rcwl0516OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_rcwl0516_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_RCWL0516_BORROW).buf;
    }
    return s_own.span;
}

void protocore_rcwl0516_presence_init(uint8_t *work, PresenceCore *c, uint32_t debounce_ms, uint32_t hold_ms,
                                      uint32_t now)
{
    (void)work;

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

proto_bool protocore_rcwl0516_presence_update(uint8_t *work, PresenceCore *c, proto_bool pin_high, uint32_t now)
{
    (void)work;

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

proto_bool protocore_rcwl0516_presence_get(uint8_t *work, const PresenceCore *c)
{
    (void)work;

    return c && c->present != 0;
}

proto_bool protocore_rcwl0516_presence_take_event(uint8_t *work, PresenceCore *c)
{
    (void)work;

    if (!c || !c->changed)
    {
        return PROTO_FALSE;
    }
    c->changed = 0;
    return PROTO_TRUE;
}

void protocore_rcwl0516_core_init(uint8_t *work, PresenceCore *c, uint32_t now)
{

    Rcwl0516.presence_init(work, c, PROTOCORE_RCWL0516_DEBOUNCE_MS, PROTOCORE_RCWL0516_HOLD_MS, now);
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

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(RCWL0516_OFF_CTX % _Alignof(Rcwl0516Ctx) == 0,
              "RCWL0516_OFF_CTX is not a multiple of alignof(Rcwl0516Ctx) - RCWL0516_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define RCWL0516_CTX(w) ((Rcwl0516Ctx *)(void *)((w) + RCWL0516_OFF_CTX))

// The pin, or -1 for "there is none" - which is what a failed or absent begin() reports, the way a
// main() reports failure. Stated here rather than as an initializer on the declaration so the
// context carries none and can live in a borrow that arrives zeroed. It takes a flag rather than a
// sentinel value because pin 0 is a real pin, so zero cannot mean "unset". A caller that hands
// begin() a negative pin still lands on -1 here, and the poll below still refuses.
static int dev_pin(uint8_t *work)
{
    return RCWL0516_CTX(work)->begun ? RCWL0516_CTX(work)->pin : -1;
}

proto_bool protocore_rcwl0516_begin(uint8_t *work, int out_pin)
{

    RCWL0516_CTX(work)->pin = out_pin;
    RCWL0516_CTX(work)->begun = PROTO_TRUE;
    protocore_platform_gpio_mode((uint8_t)(out_pin),
                                 PROTOCORE_GPIO_IN); // the module drives OUT actively; no pull needed
    Rcwl0516.core_init(work, &RCWL0516_CTX(work)->core, Clock.ms);
    return PROTO_TRUE;
}

proto_bool protocore_rcwl0516_poll(uint8_t *work)
{
    const int pin = dev_pin(work);
    if (pin < 0)
    {
        return PROTO_FALSE;
    }
    Rcwl0516.presence_update(work, &RCWL0516_CTX(work)->core,
                             protocore_platform_gpio_read((uint8_t)(pin)) == PROTOCORE_GPIO_HIGH, Clock.ms);
    Rcwl0516.presence_take_event(work, &RCWL0516_CTX(work)->core);
    return PROTO_FALSE;
}

void protocore_rcwl0516_present(uint8_t *work)
{
    Rcwl0516.presence_get(work, &RCWL0516_CTX(work)->core);
}
