// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sen0192.h
 * @brief DFRobot SEN0192 10.525 GHz microwave Doppler motion sensor (PROTOCORE_ENABLE_SEN0192).
 *
 * The SEN0192 is a 3-pin part (V / G / digital OUT) whose OUT line asserts while it senses motion
 * (Doppler shift) within its adjustable range. Unlike the framed serial of an LD2410, it carries no
 * protocol - it is a single digital line - so the "driver" is a debounced presence tracker over that
 * line: assert presence on an active sample and hold it for a configurable window after the last active
 * sample, so brief gaps between Doppler returns don't make presence flap.
 *
 * The presence state machine (::Sen0192Motion) is pure and host-tested - it takes a sampled line level
 * and a timestamp and needs no clock or GPIO. The binding reads PROTOCORE_SEN0192_PIN each poll (via
 * protocore_millis()) and feeds it in; only that read reaches the pin seam. The OUT polarity and hold window come
 * from ServerConfig (PROTOCORE_SEN0192_ACTIVE_HIGH / PROTOCORE_SEN0192_HOLD_MS / PROTOCORE_SEN0192_PIN).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SEN0192_H
#define PROTOCORE_SEN0192_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SEN0192

PROTOCORE_BEGIN_DECLS

// PROTOCORE_SEN0192_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/**
 * @brief Debounced motion-presence tracker over a single digital line.
 *
 * Presence asserts on an active-level sample and is held for @c hold_ms after the last active sample; an
 * inactive stretch longer than @c hold_ms clears it. Pure: time is passed in, so it is fully host-testable.
 */
typedef struct
{
    proto_bool present;      ///< presence currently asserted (respecting the hold window)
    proto_bool seeded;       ///< a first sample has been fed (so the hold timing is meaningful)
    proto_bool active_high;  ///< the active (motion) state is a logic HIGH
    uint32_t hold_ms;        ///< presence is held this long after the last active sample
    uint32_t last_active_ms; ///< timestamp of the last active-level sample
    uint32_t motion_events;  ///< count of clear -> present transitions (rising edges of presence)
} Sen0192Motion;

/** @brief What motion_init takes: m, hold_ms, active_high. */
typedef struct
{
    Sen0192Motion *m;
    uint32_t hold_ms;
    proto_bool active_high;
} Sen0192MotionInitArgs;

/** @brief What motion_update takes: m, level_high, now_ms. */
typedef struct
{
    Sen0192Motion *m;
    proto_bool level_high;
    uint32_t now_ms;
} Sen0192MotionUpdateArgs;

/** @brief What motion_tick takes: m, now_ms. */
typedef struct
{
    Sen0192Motion *m;
    uint32_t now_ms;
} Sen0192MotionTickArgs;

/** @brief What motion_present takes: m. */
typedef struct
{
    const Sen0192Motion *m;
} Sen0192MotionPresentArgs;

/** @brief What motion_events takes: m. */
typedef struct
{
    const Sen0192Motion *m;
} Sen0192MotionEventsArgs;

/** @brief What motion_active_age_ms takes: m, now_ms. */
typedef struct
{
    const Sen0192Motion *m;
    uint32_t now_ms;
} Sen0192MotionActiveAgeMsArgs;

/**
 * @brief DFRobot SEN0192 10.525 GHz microwave Doppler motion sensor (PROTOCORE_ENABLE_SEN0192).
 *
 * A caller sets the members a call takes, invokes it through ::Sen0192 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Sen0192.motion_init_args.m = ...;
 *   Sen0192.motion_init_args.hold_ms = ...;
 *   Sen0192.motion_init_args.active_high = ...;
 *   Sen0192.motion_init(work);
 *
 * @var Sen0192Ns::motion_init_args  what motion_init takes: m, hold_ms, active_high
 * @var Sen0192Ns::motion_update_args  what motion_update takes: m, level_high, now_ms
 * @var Sen0192Ns::motion_tick_args  what motion_tick takes: m, now_ms
 * @var Sen0192Ns::motion_present_args  what motion_present takes: m
 * @var Sen0192Ns::motion_events_args  what motion_events takes: m
 * @var Sen0192Ns::motion_active_age_ms_args  what motion_active_age_ms takes: m, now_ms
 * @var Sen0192Ns::ok  true iff this sample started a new presence (a clear -> present ...
 * @var Sen0192Ns::n  the count a call reports
 * @var Sen0192Ns::ms  the milliseconds a call reports
 * @var Sen0192Ns::motion_init  initialize a tracker: active_high sets the motion polarity, hold_ms ...
 * @var Sen0192Ns::motion_update  feed one sampled line level at now_ms
 * @var Sen0192Ns::motion_tick  re-evaluate presence against the hold window at now_ms without a ...
 * @var Sen0192Ns::motion_present  current presence (respecting the hold window)
 * @var Sen0192Ns::motion_events  number of clear -> present transitions since init
 * @var Sen0192Ns::motion_active_age_ms  milliseconds since the last active-level sample (0 if none yet)
 * @var Sen0192Ns::begin  configure PROTOCORE_SEN0192_PIN as an input and start tracking ...
 * @var Sen0192Ns::poll  sample the pin now (via protocore_millis()). true iff a new ...
 * @var Sen0192Ns::present  current presence
 * @var Sen0192Ns::motion_count  count of motion events (clear -> present transitions) since ...
 *
 * @c work is PROTOCORE_SEN0192_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Sen0192MotionInitArgs motion_init_args;
    Sen0192MotionUpdateArgs motion_update_args;
    Sen0192MotionTickArgs motion_tick_args;
    Sen0192MotionPresentArgs motion_present_args;
    Sen0192MotionEventsArgs motion_events_args;
    Sen0192MotionActiveAgeMsArgs motion_active_age_ms_args;

    proto_bool ok;
    uint32_t n;
    uint32_t ms;

    void (*const motion_init)(uint8_t *restrict work);
    void (*const motion_update)(uint8_t *restrict work);
    void (*const motion_tick)(uint8_t *restrict work);
    void (*const motion_present)(uint8_t *restrict work);
    void (*const motion_events)(uint8_t *restrict work);
    void (*const motion_active_age_ms)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const present)(uint8_t *restrict work);
    void (*const motion_count)(uint8_t *restrict work);
} Sen0192Ns;

/** @brief The one symbol this module exports. */
extern Sen0192Ns Sen0192;

/**
 * @brief The PROTOCORE_SEN0192_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_sen0192_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SEN0192

#endif // PROTOCORE_SEN0192_H
