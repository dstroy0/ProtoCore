// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sen0192.c
 * @brief SEN0192 microwave motion sensor - debounced presence tracker + GPIO binding. See sen0192.h.
 */

#include "server/peripherals/sen0192/sen0192.h"

#if PROTOCORE_ENABLE_SEN0192

#if !PROTOCORE_HAS_GPIO
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SEN0192 needs a GPIO seam. Provide one in core_setup/hal/<vendor>, or turn the driver\
 off - there is no software stand-in for a part on the other end of a wire."
#endif

// ---------------------------------------------------------------------------
// Pure presence state machine (host-tested).
// ---------------------------------------------------------------------------

#include "server/clock/clock.h" // Clock.millis

void protocore_sen0192_motion_init(Sen0192Motion *m, uint32_t hold_ms, proto_bool active_high)
{
    m->present = PROTO_FALSE;
    m->seeded = PROTO_FALSE;
    m->active_high = active_high;
    m->hold_ms = hold_ms;
    m->last_active_ms = 0;
    m->motion_events = 0;
}

proto_bool protocore_sen0192_motion_update(Sen0192Motion *m, proto_bool level_high, uint32_t now_ms)
{
    proto_bool active = (level_high == m->active_high);
    if (active)
    {
        m->last_active_ms = now_ms;
        m->seeded = PROTO_TRUE;
        if (!m->present)
        {
            m->present = PROTO_TRUE;
            m->motion_events++;
            return PROTO_TRUE; // clear -> present edge
        }
        return PROTO_FALSE;
    }
    protocore_sen0192_motion_tick(m, now_ms); // inactive sample: presence may age out
    return PROTO_FALSE;
}

proto_bool protocore_sen0192_motion_tick(Sen0192Motion *m, uint32_t now_ms)
{
    if (m->present && m->seeded && (uint32_t)(now_ms - m->last_active_ms) > m->hold_ms)
    {
        m->present = PROTO_FALSE;
    }
    return m->present;
}

proto_bool protocore_sen0192_motion_present(const Sen0192Motion *m)
{
    return m->present;
}

uint32_t protocore_sen0192_motion_events(const Sen0192Motion *m)
{
    return m->motion_events;
}

uint32_t protocore_sen0192_motion_active_age_ms(const Sen0192Motion *m, uint32_t now_ms)
{
    return m->seeded ? (uint32_t)(now_ms - m->last_active_ms) : 0;
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
static Sen0192Ctx s_sen;

// The pin, or -1 for "there is none" - which is what a failed or absent begin() reports, the way a
// main() reports failure. Stated here rather than as an initializer on the declaration so the
// context carries none and can live in a borrow that arrives zeroed. It takes a flag rather than a
// sentinel value because pin 0 is a real pin, so zero cannot mean "unset".
static int dev_pin(void)
{
    return s_sen.begun ? s_sen.pin : -1;
}

proto_bool protocore_sen0192_begin(void)
{
    s_sen.pin = PROTOCORE_SEN0192_PIN;
    s_sen.begun = PROTO_TRUE;
    protocore_platform_gpio_mode((uint8_t)(s_sen.pin), PROTOCORE_GPIO_IN);
    protocore_sen0192_motion_init(&s_sen.motion, PROTOCORE_SEN0192_HOLD_MS, PROTOCORE_SEN0192_ACTIVE_HIGH != 0);
    return PROTO_TRUE;
}

proto_bool protocore_sen0192_poll(void)
{
    const int pin = dev_pin();
    if (pin < 0)
    {
        return PROTO_FALSE;
    }
    proto_bool level = protocore_platform_gpio_read((uint8_t)(pin)) != 0;
    return protocore_sen0192_motion_update(&s_sen.motion, level, Clock.ms);
}

proto_bool protocore_sen0192_present(void)
{
    protocore_sen0192_motion_tick(&s_sen.motion, Clock.ms); // age presence out even between poll()s
    return protocore_sen0192_motion_present(&s_sen.motion);
}

uint32_t protocore_sen0192_motion_count(void)
{
    return protocore_sen0192_motion_events(&s_sen.motion);
}

#endif // PROTOCORE_ENABLE_SEN0192
