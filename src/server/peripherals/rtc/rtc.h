// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RTC

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define RTC_REG_COUNT 7

/** @brief What regs_to_epoch takes: regs, epoch. */
typedef struct
{
    const uint8_t *regs; ///< the 7 register bytes as read from register 0 RTC_REG_COUNT bytes.
    uint32_t *epoch;     ///< out: seconds since 1970-01-01 UTC
} RtcRegsToEpochArgs;

/** @brief What epoch_to_regs takes: epoch, regs. */
typedef struct
{
    uint32_t epoch;
    uint8_t *regs; ///< RTC_REG_COUNT bytes.
} RtcEpochToRegsArgs;

/** @brief What set_epoch takes: epoch. */
typedef struct
{
    uint32_t epoch;
} RtcSetEpochArgs;

/**
 * @brief I2C real-time-clock driver (DS1307 / DS3231) - a battery-backed time source. A DS1307 or DS3231 keeps the ...
 *
 * A caller sets the members a call takes, invokes it through ::Rtc with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Rtc.regs_to_epoch_args.regs = ...;
 *   Rtc.regs_to_epoch_args.epoch = ...;
 *   Rtc.regs_to_epoch(work);
 *   // Rtc.ok is what the call reports
 *
 * @var RtcNs::regs_to_epoch_args  what regs_to_epoch takes: regs, epoch
 * @var RtcNs::epoch_to_regs_args  what epoch_to_regs takes: epoch, regs
 * @var RtcNs::set_epoch_args  what set_epoch takes: epoch
 * @var RtcNs::ok  true on a valid time; false if a field is out of range ...
 * @var RtcNs::epoch  seconds since 1970-01-01 UTC, or 0 if the RTC is absent / holds an ...
 * @var RtcNs::regs_to_epoch  convert the 7 raw RTC time registers (BCD: sec, min, hour, dow, ...
 * @var RtcNs::epoch_to_regs  convert a Unix timestamp to the 7 RTC time registers (BCD, ...
 * @var RtcNs::begin  initialize the I2C bus for the RTC. true; with no bus seam it is a ...
 * @var RtcNs::read_epoch  read the current time from the RTC over I2C
 * @var RtcNs::set_epoch  set the RTC to epoch over I2C. true if the write succeeded
 * @var RtcNs::time_source  A ::TimeSourceFn wrapper (returns protocore_rtc_read_epoch()) to ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    RtcRegsToEpochArgs regs_to_epoch_args;
    RtcEpochToRegsArgs epoch_to_regs_args;
    RtcSetEpochArgs set_epoch_args;

    proto_bool ok;
    uint32_t epoch;

    void (*const regs_to_epoch)(uint8_t *restrict work);
    void (*const epoch_to_regs)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read_epoch)(uint8_t *restrict work);
    void (*const set_epoch)(uint8_t *restrict work);
    void (*const time_source)(uint8_t *restrict work);
} RtcNs;

/** @brief The one symbol this module exports. */
extern RtcNs Rtc;

/**
 * @brief The PROTOCORE_I2C_DEVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_rtc_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RTC

#endif // PROTOCORE_RTC_H
