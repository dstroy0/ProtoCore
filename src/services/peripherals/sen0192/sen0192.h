// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sen0192.h
 * @brief DFRobot SEN0192 10.525 GHz microwave Doppler motion sensor (PC_ENABLE_SEN0192).
 *
 * The SEN0192 is a 3-pin part (V / G / digital OUT) whose OUT line asserts while it senses motion
 * (Doppler shift) within its adjustable range. Unlike the framed serial of an LD2410, it carries no
 * protocol - it is a single digital line - so the "driver" is a debounced presence tracker over that
 * line: assert presence on an active sample and hold it for a configurable window after the last active
 * sample, so brief gaps between Doppler returns don't make presence flap.
 *
 * The presence state machine (::Sen0192Motion) is pure and host-tested - it takes a sampled line level
 * and a timestamp and needs no clock or GPIO. The binding reads PC_SEN0192_PIN each poll (via
 * pc_millis()) and feeds it in; only that read reaches the pin seam. The OUT polarity and hold window come
 * from ServerConfig (PC_SEN0192_ACTIVE_HIGH / PC_SEN0192_HOLD_MS / PC_SEN0192_PIN).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SEN0192_H
#define PROTOCORE_SEN0192_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SEN0192

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

/** @brief Initialize a tracker: @p active_high sets the motion polarity, @p hold_ms the presence hold. */
void pc_sen0192_motion_init(Sen0192Motion *m, uint32_t hold_ms, proto_bool active_high);

/**
 * @brief Feed one sampled line level at @p now_ms.
 * @return true iff this sample started a new presence (a clear -> present edge).
 */
proto_bool pc_sen0192_motion_update(Sen0192Motion *m, proto_bool level_high, uint32_t now_ms);

/**
 * @brief Re-evaluate presence against the hold window at @p now_ms without a new sample (call each tick so
 *        presence clears on time even when no fresh sample arrives). @return the current presence.
 */
proto_bool pc_sen0192_motion_tick(Sen0192Motion *m, uint32_t now_ms);

/** @brief Current presence (respecting the hold window). */
proto_bool pc_sen0192_motion_present(const Sen0192Motion *m);

/** @brief Number of clear -> present transitions since init. */
uint32_t pc_sen0192_motion_events(const Sen0192Motion *m);

/** @brief Milliseconds since the last active-level sample (0 if none yet). */
uint32_t pc_sen0192_motion_active_age_ms(const Sen0192Motion *m, uint32_t now_ms);

// --- Pin binding (GPIO poll) -----------------------------------------

/**
 * @brief Configure PC_SEN0192_PIN as an input and start tracking (polarity / hold from ServerConfig).
 * @return true where the pin was configured, false where there is no pin seam.
 */
proto_bool pc_sen0192_begin(void);

/** @brief Sample the pin now (via pc_millis()). @return true iff a new presence just started. */
proto_bool pc_sen0192_poll(void);

/** @brief Current presence. */
proto_bool pc_sen0192_present(void);

/** @brief Count of motion events (clear -> present transitions) since pc_sen0192_begin(). */
uint32_t pc_sen0192_motion_count(void);

#endif // PC_ENABLE_SEN0192

PROTO_END_DECLS

#endif // PROTOCORE_SEN0192_H
