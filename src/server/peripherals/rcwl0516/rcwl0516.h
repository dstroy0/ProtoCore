// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rcwl0516.h
 * @brief RCWL-0516 microwave Doppler presence sensor, and the shared one-GPIO presence facade
 *        (PROTOCORE_ENABLE_RCWL0516).
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
 * @ref protocore_rcwl0516_core_init defaults - the HMMD's OUT pin, a PIR, or an HB100 can reuse the same
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RCWL0516

PROTOCORE_BEGIN_DECLS

// PROTOCORE_RCWL0516_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

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
/** @brief What presence_init takes: c, debounce_ms, hold_ms, now. */
typedef struct
{
    PresenceCore *c;
    uint32_t debounce_ms; ///< 0 disables debouncing (every sample is believed immediately)
    uint32_t hold_ms;     ///< 0 disables the hold (presence follows the debounced level exactly)
    uint32_t now;
} Rcwl0516PresenceInitArgs;
/** @brief What presence_update takes: c, pin_high, now. */
typedef struct
{
    PresenceCore *c;
    proto_bool pin_high;
    uint32_t now;
} Rcwl0516PresenceUpdateArgs;
/** @brief What presence_get takes: c. */
typedef struct
{
    const PresenceCore *c;
} Rcwl0516PresenceGetArgs;
/** @brief What presence_take_event takes: c. */
typedef struct
{
    PresenceCore *c;
} Rcwl0516PresenceTakeEventArgs;
/** @brief What core_init takes: c, now. */
typedef struct
{
    PresenceCore *c;
    uint32_t now;
} Rcwl0516CoreInitArgs;
/** @brief What begin takes: out_pin. */
typedef struct
{
    int out_pin;
} Rcwl0516BeginArgs;
/**
 * @brief RCWL-0516 microwave Doppler presence sensor, and the shared one-GPIO presence facade
 * (PROTOCORE_ENABLE_RCWL0516).
 *
 * A caller sets the members a call takes, invokes it through ::Rcwl0516 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Rcwl0516.presence_init_args.c = ...;
 *   Rcwl0516.presence_init_args.debounce_ms = ...;
 *   Rcwl0516.presence_init_args.hold_ms = ...;
 *   Rcwl0516.presence_init_args.now = ...;
 *   Rcwl0516.presence_init(work);
 *
 * @var Rcwl0516Ns::presence_init_args  what presence_init takes: c, debounce_ms, hold_ms, now
 * @var Rcwl0516Ns::presence_update_args  what presence_update takes: c, pin_high, now
 * @var Rcwl0516Ns::presence_get_args  what presence_get takes: c
 * @var Rcwl0516Ns::presence_take_event_args  what presence_take_event takes: c
 * @var Rcwl0516Ns::core_init_args  what core_init takes: c, now
 * @var Rcwl0516Ns::begin_args  what begin takes: out_pin
 * @var Rcwl0516Ns::ok  the presence state after this sample (also in PresenceCore::present)
 * @var Rcwl0516Ns::presence_init  initialize to *absent* at now, with the pin treated as idle (LOW). ...
 * @var Rcwl0516Ns::presence_update  feed one sample of the presence pin. Call it as often as ...
 * @var Rcwl0516Ns::presence_get  current presence, without sampling
 * @var Rcwl0516Ns::presence_take_event  consume the presence-changed event
 * @var Rcwl0516Ns::core_init  initialize c with the RCWL-0516 defaults ...
 * @var Rcwl0516Ns::begin  configure out_pin as an input and start the core. true where the ...
 * @var Rcwl0516Ns::poll  sample the pin at the current time. true if presence changed on ...
 * @var Rcwl0516Ns::present  latest debounced, hold-extended presence
 *
 * @c work is PROTOCORE_RCWL0516_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Rcwl0516PresenceInitArgs presence_init_args;
    Rcwl0516PresenceUpdateArgs presence_update_args;
    Rcwl0516PresenceGetArgs presence_get_args;
    Rcwl0516PresenceTakeEventArgs presence_take_event_args;
    Rcwl0516CoreInitArgs core_init_args;
    Rcwl0516BeginArgs begin_args;
    proto_bool ok;
} Rcwl0516Vars;

/** @brief The operands and the outcome. */
extern Rcwl0516Vars Rcwl0516V;

/** @brief The entries. */
typedef struct
{
    void (*const presence_init)(uint8_t *restrict work);
    void (*const presence_update)(uint8_t *restrict work);
    void (*const presence_get)(uint8_t *restrict work);
    void (*const presence_take_event)(uint8_t *restrict work);
    void (*const core_init)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const present)(uint8_t *restrict work);
} Rcwl0516Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Rcwl0516V or a region of the borrow at a fixed offset.
void protocore_rcwl0516_presence_init(uint8_t *restrict work);
void protocore_rcwl0516_presence_update(uint8_t *restrict work);
void protocore_rcwl0516_presence_get(uint8_t *restrict work);
void protocore_rcwl0516_presence_take_event(uint8_t *restrict work);
void protocore_rcwl0516_core_init(uint8_t *restrict work);
void protocore_rcwl0516_begin(uint8_t *restrict work);
void protocore_rcwl0516_poll(uint8_t *restrict work);
void protocore_rcwl0516_present(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Rcwl0516.presence_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Rcwl0516Ns Rcwl0516 __attribute__((unused)) = {
    .presence_init = protocore_rcwl0516_presence_init,
    .presence_update = protocore_rcwl0516_presence_update,
    .presence_get = protocore_rcwl0516_presence_get,
    .presence_take_event = protocore_rcwl0516_presence_take_event,
    .core_init = protocore_rcwl0516_core_init,
    .begin = protocore_rcwl0516_begin,
    .poll = protocore_rcwl0516_poll,
    .present = protocore_rcwl0516_present,
};

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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RCWL0516

#endif // PROTOCORE_RCWL0516_H
