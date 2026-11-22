// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sen0192.c
 * @brief SEN0192 microwave motion sensor - debounced presence tracker + GPIO binding. See sen0192.h.
 */

#include "protocore_config.h" // the entry point: the widths

#if PROTOCORE_ENABLE_SEN0192

#include "server/core/worker/worker.h" // the cellblock this module's state is taken from
#include "server/peripherals/sen0192/sen0192.h"

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
        s_own.span = (uint8_t *)protocore_plain_persist(PROTOCORE_SEN0192_BORROW);
    }
    return s_own.span;
}

void protocore_sen0192_motion_init(const uint8_t *work, Sen0192Motion *m, uint32_t hold_ms, embed_bool active_high)
{
    (void)work;

    m->present = EMBED_FALSE;
    m->seeded = EMBED_FALSE;
    m->active_high = active_high;
    m->hold_ms = hold_ms;
    m->last_active_ms = 0;
    m->motion_events = 0;
}

embed_bool protocore_sen0192_motion_update(uint8_t *work, Sen0192Motion *m, embed_bool level_high, uint32_t now_ms)
{
    embed_bool ok = EMBED_FALSE;
    embed_bool active = (level_high == m->active_high);
    if (active)
    {
        m->last_active_ms = now_ms;
        m->seeded = EMBED_TRUE;
        if (!m->present)
        {
            m->present = EMBED_TRUE;
            m->motion_events++;
            ok = EMBED_TRUE; // clear -> present edge
            return ok;
        }
        return EMBED_FALSE;
    }
    embed_bool sen0192_ok = Sen0192.motion_tick(work, m, now_ms); // inactive sample: presence may age out
    sen0192_ok = EMBED_FALSE;
    return ok;
}

embed_bool protocore_sen0192_motion_tick(const uint8_t *work, Sen0192Motion *m, uint32_t now_ms)
{
    (void)work;

    if (m->present && m->seeded && (now_ms - m->last_active_ms) > m->hold_ms)
    {
        m->present = EMBED_FALSE;
    }
    return m->present;
}

embed_bool protocore_sen0192_motion_present(const uint8_t *work, const Sen0192Motion *m)
{
    (void)work;

    return m->present;
}

uint32_t protocore_sen0192_motion_events(const uint8_t *work, const Sen0192Motion *m)
{
    (void)work;

    return m->motion_events;
}

uint32_t protocore_sen0192_motion_active_age_ms(const uint8_t *work, const Sen0192Motion *m, uint32_t now_ms)
{
    (void)work;

    return m->seeded ? (now_ms - m->last_active_ms) : 0;
}

// ---------------------------------------------------------------------------
// Pin binding
// ---------------------------------------------------------------------------

// The SEN0192 binding state, owned by one instance (internal linkage): the presence tracker and the pin.
typedef struct
{
    Sen0192Motion motion;
    int pin;
    embed_bool begun; ///< begin() ran; until it has, the pin reports -1
} Sen0192Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SEN0192_OFF_CTX 0u
static_assert(SEN0192_OFF_CTX + sizeof(Sen0192Ctx) <= PROTOCORE_SEN0192_BORROW,
              "PROTOCORE_SEN0192_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: a cellblock hands out cells on
// MMGR_CARCER_ALIGN boundaries, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SEN0192_OFF_CTX % _Alignof(Sen0192Ctx) == 0,
              "SEN0192_OFF_CTX is not a multiple of alignof(Sen0192Ctx) - SEN0192_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SEN0192_CTX(w)                                                                                                 \
    _Generic((w),                                                                                                      \
        const uint8_t *: (const Sen0192Ctx *)(const void *)((w) + SEN0192_OFF_CTX),                                    \
        default: (Sen0192Ctx *)(void *)((w) + SEN0192_OFF_CTX))

// The pin, or -1 for "there is none" - which is what a failed or absent begin() reports, the way a
// main() reports failure. Stated here rather than as an initializer on the declaration so the
// context carries none and can live in a borrow that arrives zeroed. It takes a flag rather than a
// sentinel value because pin 0 is a real pin, so zero cannot mean "unset".
static int dev_pin(uint8_t *work)
{
    return SEN0192_CTX(work)->begun ? SEN0192_CTX(work)->pin : -1;
}

embed_bool protocore_sen0192_begin(uint8_t *work)
{
    SEN0192_CTX(work)->pin = PROTOCORE_SEN0192_PIN;
    SEN0192_CTX(work)->begun = EMBED_TRUE;
    protocore_platform_gpio_mode((uint8_t)(SEN0192_CTX(work)->pin), PROTOCORE_GPIO_IN);
    Sen0192.motion_init(work, &SEN0192_CTX(work)->motion, PROTOCORE_SEN0192_HOLD_MS,
                        PROTOCORE_SEN0192_ACTIVE_HIGH != 0);
    return EMBED_TRUE;
}

embed_bool protocore_sen0192_poll(uint8_t *work)
{
    const int pin = dev_pin(work);
    if (pin < 0)
    {
        return EMBED_FALSE;
    }
    embed_bool level = protocore_platform_gpio_read((uint8_t)(pin)) != 0;
    Sen0192.motion_update(work, &SEN0192_CTX(work)->motion, level, Clock.ms);
    return EMBED_FALSE;
}

embed_bool protocore_sen0192_present(uint8_t *work)
{
    Sen0192.motion_tick(work, &SEN0192_CTX(work)->motion, Clock.ms); // age presence out even between poll()s
    return Sen0192.motion_present(work, &SEN0192_CTX(work)->motion);
}

uint32_t protocore_sen0192_motion_count(uint8_t *work)
{
    return Sen0192.motion_events(work, &SEN0192_CTX(work)->motion);
}

#endif // PROTOCORE_ENABLE_SEN0192
