// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rtc.h
 * @brief I2C real-time-clock driver (DS1307 / DS3231) - a battery-backed time source.
 *
 * A DS1307 or DS3231 keeps the wall-clock time running from a coin cell when the ESP32 is off
 * or offline. This reads it (and can set it) over I2C, and plugs into the time-source chain so
 * `protocore_time_now()` - and the NTP server - can use it: GPS when locked, the RTC when GPS and
 * the internet are gone, upstream NTP otherwise. Both chips expose the same seven BCD time
 * registers at address 0x68, so one driver serves both. Zero heap; gated by PROTOCORE_ENABLE_RTC.
 *
 * The BCD <-> Unix-epoch conversion (12/24-hour, leap years, range validation) is pure and
 * host-tested; only the register read/write touches hardware, over the shared I2C bus owner.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RTC_H
#define PROTOCORE_RTC_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_INLINE

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_RTC

/** @brief Number of time registers read from the RTC (seconds..year). */
#define RTC_REG_COUNT 7

/**
 * @brief Convert the 7 raw RTC time registers (BCD: sec, min, hour, dow, date, month, year) to
 * a Unix timestamp. Pure - no I2C. Handles both 24-hour and 12-hour (AM/PM) hour encodings and
 * masks the DS1307 clock-halt / DS3231 century bits.
 * @param regs   the 7 register bytes as read from register 0.
 * @param epoch  out: seconds since 1970-01-01 UTC.
 * @return true on a valid time; false if a field is out of range (bad/uninitialized RTC).
 */
proto_bool protocore_rtc_regs_to_epoch(const uint8_t regs[RTC_REG_COUNT], uint32_t *epoch);

/**
 * @brief Convert a Unix timestamp to the 7 RTC time registers (BCD, 24-hour). Pure - no I2C.
 * The day-of-week register is filled (1=Mon..7=Sun) for completeness.
 */
void protocore_rtc_epoch_to_regs(uint32_t epoch, uint8_t regs[RTC_REG_COUNT]);

/** @brief Initialize the I2C bus for the RTC. @return true; with no bus seam it is a no-op. */
proto_bool protocore_rtc_begin(void);

/**
 * @brief Read the current time from the RTC over I2C.
 * @return seconds since 1970-01-01 UTC, or 0 if the RTC is absent / holds an invalid time.
 */
uint32_t protocore_rtc_read_epoch(void);

/** @brief Set the RTC to @p epoch over I2C. @return true if the write succeeded. */
proto_bool protocore_rtc_set_epoch(uint32_t epoch);

/**
 * @brief A ::TimeSourceFn wrapper (returns protocore_rtc_read_epoch()) to register with
 * protocore_time_source_add(). @return the RTC time, or 0 when unavailable.
 */
uint32_t protocore_rtc_time_source(void);

#endif // PROTOCORE_ENABLE_RTC

PROTOCORE_END_DECLS

#endif // PROTOCORE_RTC_H
