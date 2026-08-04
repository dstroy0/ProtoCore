// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_health.h
 * @brief Hardware-health diagnostics: rail droop, SPI CRC backoff, GPIO short, cap leakage
 *        (PC_ENABLE_HW_HEALTH).
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

PROTO_BEGIN_DECLS

#if PC_ENABLE_HW_HEALTH

/** @brief Rail sample verdict (the sole return of pc_hwhealth_rail_sample). */
typedef enum PROTO_ENUM_PACKED
{
    HW_RAIL_OK = 0,      ///< at or above the warn threshold.
    HW_RAIL_SAG = 1,     ///< below warn, at or above crit.
    HW_RAIL_BROWNOUT = 2 ///< below the crit threshold.
} HwRailVerdict;

/** @brief GPIO short-circuit verdict (the sole return of pc_hwhealth_gpio_short). */
typedef enum PROTO_ENUM_PACKED
{
    HW_GPIO_OK = 0,        ///< readback matches the driven level.
    HW_GPIO_SHORT_GND = 1, ///< drove high, read low: shorted to ground.
    HW_GPIO_SHORT_VCC = 2  ///< drove low, read high: shorted to Vcc.
} HwGpioVerdict;

/** @brief Capacitor-leakage verdict (the sole return of pc_hwhealth_cap_leak). */
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

/** @brief Initialize a rail monitor. @p min_mv starts at @p nominal_mv. */
void pc_hwhealth_rail_init(HwRailMonitor *m, uint32_t nominal_mv, uint32_t warn_mv, uint32_t crit_mv);

/** @brief Record one rail sample; updates the worst-droop min + counters. @return HW_RAIL_*. */
HwRailVerdict pc_hwhealth_rail_sample(HwRailMonitor *m, uint32_t mv);

/** @brief Serialize a rail monitor: `{"nominal_mv":..,"min_mv":..,"sag":..,"brownout":..}`. */
size_t pc_hwhealth_rail_json(const HwRailMonitor *m, char *out, size_t cap);

/** @brief Initialize a SPI backoff state machine. */
void pc_hwhealth_spi_init(HwSpiBackoff *s, uint32_t start_hz, uint32_t min_hz, uint32_t max_hz, uint16_t fail_trip,
                          uint16_t ok_trip);

/** @brief Feed one transfer's CRC result; adjusts the clock with hysteresis. @return the new clock (hz). */
uint32_t pc_hwhealth_spi_result(HwSpiBackoff *s, proto_bool crc_ok);

/** @brief Short-circuit test from a driven level and its readback. @return HW_GPIO_*. */
HwGpioVerdict pc_hwhealth_gpio_short(proto_bool driven_high, proto_bool read_high);

/**
 * @brief Leakage test comparing a measured RC decay time to the expected one.
 * @param measured_ms observed decay time.
 * @param expected_ms nominal decay time for a healthy cap.
 * @param tol_pct     tolerance band (percent) around expected.
 * @return HW_CAP_OK / HW_CAP_LEAK (too fast) / HW_CAP_HIGH_ESR (too slow).
 */
HwCapVerdict pc_hwhealth_cap_leak(uint32_t measured_ms, uint32_t expected_ms, uint8_t tol_pct);

#endif // PC_ENABLE_HW_HEALTH

PROTO_END_DECLS

#endif // PROTOCORE_HW_HEALTH_H
