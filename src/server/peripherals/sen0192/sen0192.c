// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sen0192.c
 * @brief SEN0192 microwave motion sensor - debounced presence tracker + GPIO binding. See sen0192.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SEN0192

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "server/peripherals/sen0192/sen0192.h"

PROTOCORE_BEGIN_DECLS

#if !PROTOCORE_HAS_GPIO
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SEN0192 needs a GPIO seam. Provide one in test/core_setup/hal/<vendor>, or turn the driver\
 off - there is no software stand-in for a part on the other end of a wire."
#endif

// ---------------------------------------------------------------------------
// Pure presence state machine (host-tested).
// ---------------------------------------------------------------------------

#include "server/clock/clock.h" // Clock.millis

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SEN0192_BORROW persistent bytes
} Sen0192OwnCtx;
static Sen0192OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_sen0192_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SEN0192_BORROW).buf;
    }
    return s_own.span;
}

static void sen0192_motion_events(uint8_t *restrict work);
static void sen0192_motion_init(uint8_t *restrict work);
static void sen0192_motion_present(uint8_t *restrict work);
static void sen0192_motion_tick(uint8_t *restrict work);
static void sen0192_motion_update(uint8_t *restrict work);

static void sen0192_motion_init(uint8_t *restrict work)
{
    (void)work;
    Sen0192Motion *m = Sen0192.motion_init_args.m;
    uint32_t hold_ms = Sen0192.motion_init_args.hold_ms;
    proto_bool active_high = Sen0192.motion_init_args.active_high;

    m->present = PROTO_FALSE;
    m->seeded = PROTO_FALSE;
    m->active_high = active_high;
    m->hold_ms = hold_ms;
    m->last_active_ms = 0;
    m->motion_events = 0;
}

static void sen0192_motion_update(uint8_t *restrict work)
{
    Sen0192Motion *m = Sen0192.motion_update_args.m;
    proto_bool level_high = Sen0192.motion_update_args.level_high;
    uint32_t now_ms = Sen0192.motion_update_args.now_ms;

    proto_bool active = (level_high == m->active_high);
    if (active)
    {
        m->last_active_ms = now_ms;
        m->seeded = PROTO_TRUE;
        if (!m->present)
        {
            m->present = PROTO_TRUE;
            m->motion_events++;
            Sen0192.ok = PROTO_TRUE; // clear -> present edge
            return;
        }
        Sen0192.ok = PROTO_FALSE;
        return;
    }
    Sen0192.motion_tick_args.m = m;
    Sen0192.motion_tick_args.now_ms = now_ms;
    sen0192_motion_tick(work); // inactive sample: presence may age out
    Sen0192.ok = PROTO_FALSE;
}

static void sen0192_motion_tick(uint8_t *restrict work)
{
    (void)work;
    Sen0192Motion *m = Sen0192.motion_tick_args.m;
    uint32_t now_ms = Sen0192.motion_tick_args.now_ms;

    if (m->present && m->seeded && (uint32_t)(now_ms - m->last_active_ms) > m->hold_ms)
    {
        m->present = PROTO_FALSE;
    }
    Sen0192.ok = m->present;
}

static void sen0192_motion_present(uint8_t *restrict work)
{
    (void)work;
    const Sen0192Motion *m = Sen0192.motion_present_args.m;

    Sen0192.ok = m->present;
}

static void sen0192_motion_events(uint8_t *restrict work)
{
    (void)work;
    const Sen0192Motion *m = Sen0192.motion_events_args.m;

    Sen0192.n = m->motion_events;
}

static void sen0192_motion_active_age_ms(uint8_t *restrict work)
{
    (void)work;
    const Sen0192Motion *m = Sen0192.motion_active_age_ms_args.m;
    uint32_t now_ms = Sen0192.motion_active_age_ms_args.now_ms;

    Sen0192.ms = m->seeded ? (uint32_t)(now_ms - m->last_active_ms) : 0;
}

// ---------------------------------------------------------------------------
// Pin binding
// ---------------------------------------------------------------------------

// The SEN0192 binding state, owned by one instance (internal linkage): the presence tracker and the pin.
typedef struct
{
    Sen0192Motion motion;
    int pin;
    proto_bool begun; ///< begin() ran; until it has, the pin reports -1
} Sen0192Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SEN0192_OFF_CTX 0u
static_assert(SEN0192_OFF_CTX + sizeof(Sen0192Ctx) <= PROTOCORE_SEN0192_BORROW,
              "PROTOCORE_SEN0192_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SEN0192_CTX(w) ((Sen0192Ctx *)(void *)((w) + SEN0192_OFF_CTX))

// The pin, or -1 for "there is none" - which is what a failed or absent begin() reports, the way a
// main() reports failure. Stated here rather than as an initializer on the declaration so the
// context carries none and can live in a borrow that arrives zeroed. It takes a flag rather than a
// sentinel value because pin 0 is a real pin, so zero cannot mean "unset".
static int dev_pin(uint8_t *restrict work)
{
    return SEN0192_CTX(work)->begun ? SEN0192_CTX(work)->pin : -1;
}

static void sen0192_begin(uint8_t *restrict work)
{
    SEN0192_CTX(work)->pin = PROTOCORE_SEN0192_PIN;
    SEN0192_CTX(work)->begun = PROTO_TRUE;
    protocore_platform_gpio_mode((uint8_t)(SEN0192_CTX(work)->pin), PROTOCORE_GPIO_IN);
    Sen0192.motion_init_args.m = &SEN0192_CTX(work)->motion;
    Sen0192.motion_init_args.hold_ms = PROTOCORE_SEN0192_HOLD_MS;
    Sen0192.motion_init_args.active_high = PROTOCORE_SEN0192_ACTIVE_HIGH != 0;
    sen0192_motion_init(work);
    Sen0192.ok = PROTO_TRUE;
}

static void sen0192_poll(uint8_t *restrict work)
{
    const int pin = dev_pin(work);
    if (pin < 0)
    {
        Sen0192.ok = PROTO_FALSE;
        return;
    }
    proto_bool level = protocore_platform_gpio_read((uint8_t)(pin)) != 0;
    Sen0192.motion_update_args.m = &SEN0192_CTX(work)->motion;
    Sen0192.motion_update_args.level_high = level;
    Sen0192.motion_update_args.now_ms = Clock.ms;
    sen0192_motion_update(work);
}

static void sen0192_present(uint8_t *restrict work)
{
    Sen0192.motion_tick_args.m = &SEN0192_CTX(work)->motion;
    Sen0192.motion_tick_args.now_ms = Clock.ms;
    sen0192_motion_tick(work); // age presence out even between poll()s
    Sen0192.motion_present_args.m = &SEN0192_CTX(work)->motion;
    sen0192_motion_present(work);
}

static void sen0192_motion_count(uint8_t *restrict work)
{
    Sen0192.motion_events_args.m = &SEN0192_CTX(work)->motion;
    sen0192_motion_events(work);
}

Sen0192Ns Sen0192 = {
    .motion_init = sen0192_motion_init,
    .motion_update = sen0192_motion_update,
    .motion_tick = sen0192_motion_tick,
    .motion_present = sen0192_motion_present,
    .motion_events = sen0192_motion_events,
    .motion_active_age_ms = sen0192_motion_active_age_ms,
    .begin = sen0192_begin,
    .poll = sen0192_poll,
    .present = sen0192_present,
    .motion_count = sen0192_motion_count,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SEN0192
