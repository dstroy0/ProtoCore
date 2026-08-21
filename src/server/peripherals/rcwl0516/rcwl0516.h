// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_RCWL0516_H
#define PROTOCORE_RCWL0516_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file rcwl0516.h
 * @brief RCWL-0516 microwave Doppler presence sensor, and the shared one-GPIO presence facade
(PROTOCORE_ENABLE_RCWL0516).
 *
 * The RCWL-0516 (RCWL-9196 controller + MMBR941M RF amp, ~3.18 GHz Doppler) has no data protocol at
 * all: a single 3.3 V **OUT** pin that latches HIGH when a moving reflector is detected and returns
 * LOW once its own retrigger window expires. Everything interesting is therefore in *time*, not in
 * bytes - which is what this module provides.
 *
 * Two problems a bare `digitalRead()` does not solve, and this does:
 *
 * 1. **Chatter.** The OUT pin is driven by an analog comparator, so around the detection threshold
 * it can flicker. A raw read turns one person walking past into a burst of presence events.
 * A level must therefore hold for @ref PresenceCore::debounce_ms before it is believed.
 *
 * 2. **Gaps.** The module drops OUT between retriggers, so a person who is present but briefly
 * still reads as absent for a moment. Presence is therefore held for
 * @ref PresenceCore::hold_ms past the last believed-HIGH sample, which turns a stream of
 * retriggers into one continuous "occupied" span instead of a flapping boolean.
 *
 * The core is pure and takes an explicit @p now, exactly like `services/hotswap`: it decides, the
 * binding acts. That makes the whole machine host-testable by injecting pin levels against a
 * synthetic clock, with no GPIO and no real time involved. All timing comparisons are unsigned
 * differences, so they are wrap-safe across a `millis()` rollover.
 *
 * @ref PresenceCore is deliberately sensor-agnostic: it is a debounced, hold-extended view of one
 * active-high presence pin. The RCWL-0516 is simply its first user, via the
 * @ref protocore_rcwl0516_core_init defaults - the HMMD's OUT pin, a PIR, or an HB100 can reuse the same
 * core by supplying their own two constants.
 *
 * Fail-safe start: a freshly initialized core reports *absent* and treats the pin as idle, so
 * presence is only ever reported after it has actually been observed and believed. Claiming
 * presence you have not yet measured is the failure mode worth avoiding.
 *
 * @c work is PROTOCORE_RCWL0516_BORROW bytes the CALLER took, at an address it knows. It is not held past the call, so
nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/**
 * @brief Default hold time (ms) for the RCWL-0516.
 *
 * The module's own retrigger window is ~2 s, so holding for at least that long bridges the gap
 * between retriggers while a target is still present.
 */
#ifndef PROTOCORE_RCWL0516_HOLD_MS
#define PROTOCORE_RCWL0516_HOLD_MS 2000
#endif

/** @brief Default debounce (ms) for the RCWL-0516 - long enough to swallow comparator chatter. */
#ifndef PROTOCORE_RCWL0516_DEBOUNCE_MS
#define PROTOCORE_RCWL0516_DEBOUNCE_MS 50
#endif

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
    uint8_t changed;       ///< set when @ref present flipped; cleared by @ref protocore_presence_take_event.
} PresenceCore;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*presence_init)(uint8_t *, PresenceCore *, uint32_t, uint32_t, uint32_t);
    proto_bool (*presence_update)(uint8_t *, PresenceCore *, proto_bool, uint32_t);
    proto_bool (*presence_get)(uint8_t *, const PresenceCore *);
    proto_bool (*presence_take_event)(uint8_t *, PresenceCore *);
    void (*core_init)(uint8_t *, PresenceCore *, uint32_t);
    proto_bool (*begin)(uint8_t *, int);
    proto_bool (*poll)(uint8_t *);
    void (*present)(uint8_t *);
} Rcwl0516Ns;
PROTOCORE_NS_LAYOUT(Rcwl0516Ns, presence_init, presence_update, presence_get, presence_take_event, core_init, begin,
                    poll, present);

/**
 * @brief Initialize to *absent* at now, with the pin treated as idle (LOW). .
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 * @param c C
 * @param debounce_ms 0 disables debouncing (every sample is believed immediately)
 * @param hold_ms 0 disables the hold (presence follows the debounced level exactly)
 * @param now Now
 */
void protocore_rcwl0516_presence_init(uint8_t *work, PresenceCore *c, uint32_t debounce_ms, uint32_t hold_ms,
                                      uint32_t now);
/**
 * @brief Feed one sample of the presence pin. Call it as often as .
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 * @param c C
 * @param pin_high Pin high
 * @param now Now
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rcwl0516_presence_update(uint8_t *work, PresenceCore *c, proto_bool pin_high, uint32_t now);
/**
 * @brief Current presence, without sampling.
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 * @param c C
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rcwl0516_presence_get(uint8_t *work, const PresenceCore *c);
/**
 * @brief Consume the presence-changed event.
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 * @param c C
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rcwl0516_presence_take_event(uint8_t *work, PresenceCore *c);
/**
 * @brief Initialize c with the RCWL-0516 defaults .
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 * @param c C
 * @param now Now
 */
void protocore_rcwl0516_core_init(uint8_t *work, PresenceCore *c, uint32_t now);
/**
 * @brief Configure out_pin as an input and start the core. true where the .
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 * @param out_pin Out pin
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rcwl0516_begin(uint8_t *work, int out_pin);
/**
 * @brief Sample the pin at the current time. true if presence changed on .
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rcwl0516_poll(uint8_t *work);
/**
 * @brief Latest debounced, hold-extended presence.
 * @param work PROTOCORE_RCWL0516_BORROW bytes the caller took. Not held past the call.
 */
void protocore_rcwl0516_present(uint8_t *work);

/**
 * @brief The PROTOCORE_RCWL0516_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_rcwl0516_span(void);

/** @brief Module namespace. */
PROTOCORE_NS Rcwl0516Ns Rcwl0516 PROTOCORE_UNUSED = {.presence_init = protocore_rcwl0516_presence_init,
                                                     .presence_update = protocore_rcwl0516_presence_update,
                                                     .presence_get = protocore_rcwl0516_presence_get,
                                                     .presence_take_event = protocore_rcwl0516_presence_take_event,
                                                     .core_init = protocore_rcwl0516_core_init,
                                                     .begin = protocore_rcwl0516_begin,
                                                     .poll = protocore_rcwl0516_poll,
                                                     .present = protocore_rcwl0516_present};

PROTOCORE_END_DECLS

#endif // PROTOCORE_RCWL0516_H
