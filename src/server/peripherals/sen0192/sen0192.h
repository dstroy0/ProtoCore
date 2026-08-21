// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SEN0192_H
#define PROTOCORE_SEN0192_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is PROTOCORE_SEN0192_BORROW bytes the CALLER took, at an address it knows. It is not held past the call, so
 * nothing here aliases it. How those bytes are carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*motion_init)(uint8_t *, Sen0192Motion *, uint32_t, proto_bool);
    proto_bool (*motion_update)(uint8_t *, Sen0192Motion *, proto_bool, uint32_t);
    proto_bool (*motion_tick)(uint8_t *, Sen0192Motion *, uint32_t);
    proto_bool (*motion_present)(uint8_t *, const Sen0192Motion *);
    uint32_t (*motion_events)(uint8_t *, const Sen0192Motion *);
    uint32_t (*motion_active_age_ms)(uint8_t *, const Sen0192Motion *, uint32_t);
    proto_bool (*begin)(uint8_t *);
    proto_bool (*poll)(uint8_t *);
    void (*present)(uint8_t *);
    void (*motion_count)(uint8_t *);
} Sen0192Ns;
PROTOCORE_NS_LAYOUT(Sen0192Ns, motion_init, motion_update, motion_tick, motion_present, motion_events,
                    motion_active_age_ms, begin, poll, present, motion_count);

/**
 * @brief Initialize a tracker: active_high sets the motion polarity, hold_ms .
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @param m M
 * @param hold_ms Hold ms
 * @param active_high Active high
 */
void protocore_sen0192_motion_init(uint8_t *work, Sen0192Motion *m, uint32_t hold_ms, proto_bool active_high);
/**
 * @brief Feed one sampled line level at now_ms.
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @param m M
 * @param level_high Level high
 * @param now_ms Now ms
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sen0192_motion_update(uint8_t *work, Sen0192Motion *m, proto_bool level_high, uint32_t now_ms);
/**
 * @brief Re-evaluate presence against the hold window at now_ms without a .
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @param m M
 * @param now_ms Now ms
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sen0192_motion_tick(uint8_t *work, Sen0192Motion *m, uint32_t now_ms);
/**
 * @brief Current presence (respecting the hold window).
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @param m M
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sen0192_motion_present(uint8_t *work, const Sen0192Motion *m);
/**
 * @brief Number of clear -> present transitions since init.
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @param m M
 * @return The uint32_t.
 */
uint32_t protocore_sen0192_motion_events(uint8_t *work, const Sen0192Motion *m);
/**
 * @brief Milliseconds since the last active-level sample (0 if none yet).
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @param m M
 * @param now_ms Now ms
 * @return The uint32_t.
 */
uint32_t protocore_sen0192_motion_active_age_ms(uint8_t *work, const Sen0192Motion *m, uint32_t now_ms);
/**
 * @brief Configure PROTOCORE_SEN0192_PIN as an input and start tracking .
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sen0192_begin(uint8_t *work);
/**
 * @brief Sample the pin now (via protocore_millis()). true iff a new .
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sen0192_poll(uint8_t *work);
/**
 * @brief Current presence.
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 */
void protocore_sen0192_present(uint8_t *work);
/**
 * @brief Count of motion events (clear -> present transitions) since .
 * @param work PROTOCORE_SEN0192_BORROW bytes the caller took. Not held past the call.
 */
void protocore_sen0192_motion_count(uint8_t *work);

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

/** @brief Module namespace. */
PROTOCORE_NS Sen0192Ns Sen0192 PROTOCORE_UNUSED = {.motion_init = protocore_sen0192_motion_init,
                                                   .motion_update = protocore_sen0192_motion_update,
                                                   .motion_tick = protocore_sen0192_motion_tick,
                                                   .motion_present = protocore_sen0192_motion_present,
                                                   .motion_events = protocore_sen0192_motion_events,
                                                   .motion_active_age_ms = protocore_sen0192_motion_active_age_ms,
                                                   .begin = protocore_sen0192_begin,
                                                   .poll = protocore_sen0192_poll,
                                                   .present = protocore_sen0192_present,
                                                   .motion_count = protocore_sen0192_motion_count};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SEN0192_H
