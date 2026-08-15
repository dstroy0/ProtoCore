// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_health.h
 * @brief Hardware-health diagnostics: rail droop, SPI CRC backoff, GPIO short, cap leakage
 *        (PROTOCORE_ENABLE_HW_HEALTH).
 *
 * Four pure decision cores an app feeds with samples it reads from the hardware (ADC millivolts, a SPI
 * CRC pass/fail, a driven-vs-readback GPIO level, a capacitor decay time). Each turns raw measurements
 * into an actionable verdict for a "/health" panel or a fail-safe hook, without touching a peripheral
 * itself:
 *
 *  - **Power-rail voltage-drop logger**: track a rail's worst droop and count sag / brownout crossings.
 *  - **SPI-bus CRC audit + clock backoff**: a hysteretic state machine that halves the SPI clock after a
 *    run of CRC failures and steps it back up after a run of good transfers.
 *  - **GPIO short-circuit test**: compare a driven level to its readback to spot a short to ground / Vcc.
 *  - **Capacitor-leakage diag**: compare a measured RC decay time to the expected one to spot a leaky
 *    cap (too fast) or a high-ESR / open path (too slow).
 *
 * Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_HW_HEALTH_H
#define PROTOCORE_HW_HEALTH_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_HW_HEALTH

PROTOCORE_BEGIN_DECLS

/** @brief Rail sample verdict (the sole return of protocore_hwhealth_rail_sample). */
typedef enum PROTO_ENUM_PACKED
{
    HW_RAIL_OK = 0,      ///< at or above the warn threshold.
    HW_RAIL_SAG = 1,     ///< below warn, at or above crit.
    HW_RAIL_BROWNOUT = 2 ///< below the crit threshold.
} HwRailVerdict;

/** @brief GPIO short-circuit verdict (the sole return of protocore_hwhealth_gpio_short). */
typedef enum PROTO_ENUM_PACKED
{
    HW_GPIO_OK = 0,        ///< readback matches the driven level.
    HW_GPIO_SHORT_GND = 1, ///< drove high, read low: shorted to ground.
    HW_GPIO_SHORT_VCC = 2  ///< drove low, read high: shorted to Vcc.
} HwGpioVerdict;

/** @brief Capacitor-leakage verdict (the sole return of protocore_hwhealth_cap_leak). */
typedef enum PROTO_ENUM_PACKED
{
    HW_CAP_OK = 0,      ///< decay time within tolerance of expected.
    HW_CAP_LEAK = 1,    ///< decays too fast: leaky capacitor.
    HW_CAP_HIGH_ESR = 2 ///< decays too slow: high-ESR / open charge path.
} HwCapVerdict;

/** @brief Rolling monitor for one power rail (in millivolts). */
typedef struct
{
    uint32_t nominal_mv;
    uint32_t warn_mv; ///< below this -> SAG.
    uint32_t crit_mv; ///< below this -> BROWNOUT.
    uint32_t min_mv;  ///< lowest sample seen (worst droop).
    uint32_t sag_events;
    uint32_t brownout_events;
} HwRailMonitor;

/** @brief Hysteretic SPI clock backoff state. */
typedef struct
{
    uint32_t hz;     ///< current clock.
    uint32_t min_hz; ///< floor.
    uint32_t max_hz; ///< ceiling.
    uint16_t fail_streak;
    uint16_t ok_streak;
    uint16_t fail_trip; ///< consecutive failures that halve the clock.
    uint16_t ok_trip;   ///< consecutive successes that double the clock.
} HwSpiBackoff;

/** @brief The rail a call watches, and the reading it just took. */
typedef struct
{
    HwRailMonitor *m;          ///< the monitor a call acts on
    const HwRailMonitor *m_ro; ///< the same monitor, where a call only reads it
    uint32_t nominal_mv;       ///< the rail's nominal level
    uint32_t warn_mv;          ///< below this a reading is a sag
    uint32_t crit_mv;          ///< below this it is a brownout
    uint32_t mv;               ///< the reading just taken
} HwRailArgs;

/** @brief The bus a backoff governs, and how it just fared. */
typedef struct
{
    HwSpiBackoff *s;    ///< the backoff state a call acts on
    uint32_t start_hz;  ///< the clock it starts at
    uint32_t min_hz;    ///< its floor
    uint32_t max_hz;    ///< its ceiling
    uint16_t fail_trip; ///< consecutive failures before it halves
    uint16_t ok_trip;   ///< consecutive successes before it doubles
    proto_bool crc_ok;  ///< the transfer just completed checked out
} HwSpiArgs;

/** @brief A pin driven against what it read back, and a discharge against what was expected. */
typedef struct
{
    proto_bool driven_high; ///< the level the pin was driven to
    proto_bool read_high;   ///< the level it read back
    uint32_t measured_ms;   ///< the discharge just timed
    uint32_t expected_ms;   ///< what it should have been
    uint8_t tol_pct;        ///< the tolerance band around that, as a percentage
} HwProbeArgs;

/** @brief Where a report is written. */
typedef struct
{
    char *out;  ///< where the JSON lands
    size_t cap; ///< how much room it has
} HwOutArgs;

/** @brief The checks' own calls, described only in hw_health.c. */
struct HwHealthInternal;

/**
 * @brief The hardware health checks over caller-owned monitors.
 *
 * A caller sets the members a call takes, invokes it through ::HwHealth, and reads the outcome off
 * the same handle. Every monitor is the caller's.
 *
 * @var HwHealthNs::rail        the rail a call watches, and the reading it just took
 * @var HwHealthNs::spi         the bus a backoff governs, and how it just fared
 * @var HwHealthNs::probe       a pin driven against what it read back, and a timed discharge
 * @var HwHealthNs::out_args    where a report is written
 * @var HwHealthNs::rail_verdict  what a rail sample decided
 * @var HwHealthNs::gpio_verdict  what a pin probe decided
 * @var HwHealthNs::cap_verdict   what a discharge probe decided
 * @var HwHealthNs::hz          the clock a backoff settled on
 * @var HwHealthNs::n           bytes a report wrote, or 0 when it did not fit
 * @var HwHealthNs::rail_init   arm a rail monitor at its thresholds
 * @var HwHealthNs::rail_sample judge one reading and tally what it was
 * @var HwHealthNs::rail_json   report the rail's tallies
 * @var HwHealthNs::spi_init    arm a bus backoff between its floor and ceiling
 * @var HwHealthNs::spi_result  feed one transfer's outcome in and take the new clock
 * @var HwHealthNs::gpio_short  a pin that does not read back what it was driven to
 * @var HwHealthNs::cap_leak    a discharge outside its tolerance band
 * @var HwHealthNs::internal    the calls that judge and tally
 *
 * No storage member: every call works in the caller's monitor.
 */
typedef struct
{
    HwRailArgs rail;
    HwSpiArgs spi;
    HwProbeArgs probe;
    HwOutArgs out_args;

    HwRailVerdict rail_verdict;
    HwGpioVerdict gpio_verdict;
    HwCapVerdict cap_verdict;
    uint32_t hz;
    size_t n;

    void (*rail_init)(struct HwHealthInternal *ctx);
    void (*rail_sample)(struct HwHealthInternal *ctx);
    void (*rail_json)(struct HwHealthInternal *ctx);
    void (*spi_init)(struct HwHealthInternal *ctx);
    void (*spi_result)(struct HwHealthInternal *ctx);
    void (*gpio_short)(struct HwHealthInternal *ctx);
    void (*cap_leak)(struct HwHealthInternal *ctx);

    struct HwHealthInternal *internal;
} HwHealthNs;

/** @brief The one symbol this module exports. */
extern HwHealthNs HwHealth;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HW_HEALTH

#endif // PROTOCORE_HW_HEALTH_H
