// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rcwl0516.h
 * @brief RCWL-0516 microwave Doppler presence sensor, and the shared one-GPIO presence facade
 *        (PC_ENABLE_RCWL0516).
 *
 * The RCWL-0516 (RCWL-9196 controller + MMBR941M RF amp, ~3.18 GHz Doppler) has no data protocol at
 * all: a single 3.3 V **OUT** pin that latches HIGH when a moving reflector is detected and returns
 * LOW once its own retrigger window expires. Everything interesting is therefore in *time*, not in
 * bytes - which is what this module provides.
 *
 * Two problems a bare `digitalRead()` does not solve, and this does:
 *
 *   1. **Chatter.** The OUT pin is driven by an analog comparator, so around the detection threshold
 *      it can flicker. A raw read turns one person walking past into a burst of presence events.
 *      A level must therefore hold for @ref PresenceCore::debounce_ms before it is believed.
 *
 *   2. **Gaps.** The module drops OUT between retriggers, so a person who is present but briefly
 *      still reads as absent for a moment. Presence is therefore held for
 *      @ref PresenceCore::hold_ms past the last believed-HIGH sample, which turns a stream of
 *      retriggers into one continuous "occupied" span instead of a flapping boolean.
 *
 * The core is pure and takes an explicit @p now, exactly like `services/hotswap`: it decides, the
 * binding acts. That makes the whole machine host-testable by injecting pin levels against a
 * synthetic clock, with no GPIO and no real time involved. All timing comparisons are unsigned
 * differences, so they are wrap-safe across a `millis()` rollover.
 *
 * @ref PresenceCore is deliberately sensor-agnostic: it is a debounced, hold-extended view of one
 * active-high presence pin. The RCWL-0516 is simply its first user, via the
 * @ref pc_rcwl0516_core_init defaults - the HMMD's OUT pin, a PIR, or an HB100 can reuse the same
 * core by supplying their own two constants.
 *
 * Fail-safe start: a freshly initialized core reports *absent* and treats the pin as idle, so
 * presence is only ever reported after it has actually been observed and believed. Claiming
 * presence you have not yet measured is the failure mode worth avoiding.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RCWL0516_H
#define PROTOCORE_RCWL0516_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_RCWL0516

/**
 * @brief Default hold time (ms) for the RCWL-0516.
 *
 * The module's own retrigger window is ~2 s, so holding for at least that long bridges the gap
 * between retriggers while a target is still present.
 */
#ifndef PC_RCWL0516_HOLD_MS
#define PC_RCWL0516_HOLD_MS 2000
#endif

/** @brief Default debounce (ms) for the RCWL-0516 - long enough to swallow comparator chatter. */
#ifndef PC_RCWL0516_DEBOUNCE_MS
#define PC_RCWL0516_DEBOUNCE_MS 50
#endif

// ---------------------------------------------------------------------------
// Host-testable core (sensor-agnostic: any active-high presence pin)
// ---------------------------------------------------------------------------

/** @brief Debounced, hold-extended state of one active-high presence pin. Pure: it decides. */
typedef struct
{
    uint32_t debounce_ms;  ///< a level must hold this long before it is believed.
    uint32_t hold_ms;      ///< presence persists this long past the last believed-HIGH sample.
    uint32_t raw_since_ms; ///< when the raw pin level last changed.
    uint32_t last_high_ms; ///< when the believed level was last HIGH.
    uint8_t raw;           ///< last raw pin level as sampled (0/1).
    uint8_t stable;        ///< believed level, after debouncing (0/1).
    uint8_t present;       ///< presence output (0/1) - @ref stable, extended by @ref hold_ms.
    uint8_t changed;       ///< set when @ref present flipped; cleared by @ref pc_presence_take_event.
} PresenceCore;

/**
 * @brief Initialize to *absent* at @p now, with the pin treated as idle (LOW).
 *
 * If the pin is in fact already HIGH, the first update starts its debounce and presence asserts once
 * that elapses - the sensor is never assumed to be reporting something that has not been sampled.
 *
 * @param debounce_ms 0 disables debouncing (every sample is believed immediately).
 * @param hold_ms     0 disables the hold (presence follows the debounced level exactly).
 */
void pc_presence_core_init(PresenceCore *c, uint32_t debounce_ms, uint32_t hold_ms, uint32_t now);

/**
 * @brief Feed one sample of the presence pin.
 *
 * Call it as often as convenient; it is level-driven, not edge-driven, so a missed poll only delays
 * a transition rather than losing it. Sampling with a non-monotonic or repeated @p now is harmless.
 *
 * @return the presence state after this sample (also in @ref PresenceCore::present).
 */
proto_bool pc_presence_core_update(PresenceCore *c, proto_bool pin_high, uint32_t now);

/** @brief Current presence, without sampling. */
proto_bool pc_presence_core_get(const PresenceCore *c);

/**
 * @brief Consume the presence-changed event.
 * @return true exactly once per transition, so a caller can publish an event per edge rather than
 *         re-publishing a level every poll. Clears the flag.
 */
proto_bool pc_presence_take_event(PresenceCore *c);

// ---------------------------------------------------------------------------
// RCWL-0516 convenience
// ---------------------------------------------------------------------------

/** @brief Initialize @p c with the RCWL-0516 defaults (@ref PC_RCWL0516_DEBOUNCE_MS / _HOLD_MS). */
void pc_rcwl0516_core_init(PresenceCore *c, uint32_t now);

// ---------------------------------------------------------------------------
// Binding (no-ops on a host build)
// ---------------------------------------------------------------------------

/** @brief Configure @p out_pin as an input and start the core. @return true on ESP32. */
proto_bool pc_rcwl0516_begin(int out_pin);

/** @brief Sample the pin at the current time. @return true if presence changed on this poll. */
proto_bool pc_rcwl0516_poll();

/** @brief Latest debounced, hold-extended presence. */
proto_bool pc_rcwl0516_present();

#endif // PC_ENABLE_RCWL0516

PROTO_END_DECLS

#endif // PROTOCORE_RCWL0516_H
