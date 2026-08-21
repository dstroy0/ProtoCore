// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_RTC_H
#define PROTOCORE_RTC_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#define RTC_REG_COUNT 7

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*regs_to_epoch)(uint8_t *restrict, const uint8_t *, uint32_t *);
    void (*epoch_to_regs)(uint8_t *restrict, uint32_t, uint8_t *);
    proto_bool (*begin)(uint8_t *restrict);
    uint32_t (*read_epoch)(uint8_t *restrict);
    proto_bool (*set_epoch)(uint8_t *restrict, uint32_t);
    void (*time_source)(uint8_t *restrict);
} RtcNs;
PROTOCORE_NS_LAYOUT(RtcNs, regs_to_epoch, epoch_to_regs, begin, read_epoch, set_epoch, time_source);

/**
 * @brief Convert the 7 raw RTC time registers (BCD: sec, min, hour, dow, .
 * @param work PROTOCORE_RTC_BORROW bytes the caller took. Not held past the call.
 * @param regs the 7 register bytes as read from register 0 RTC_REG_COUNT bytes
 * @param epoch out: seconds since 1970-01-01 UTC
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rtc_regs_to_epoch(uint8_t *restrict work, const uint8_t *regs, uint32_t *epoch);
/**
 * @brief Convert a Unix timestamp to the 7 RTC time registers (BCD, .
 * @param work PROTOCORE_RTC_BORROW bytes the caller took. Not held past the call.
 * @param epoch Epoch
 * @param regs RTC_REG_COUNT bytes
 */
void protocore_rtc_epoch_to_regs(uint8_t *restrict work, uint32_t epoch, uint8_t *regs);
/**
 * @brief Initialize the I2C bus for the RTC. true; with no bus seam it is a .
 * @param work PROTOCORE_RTC_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rtc_begin(uint8_t *restrict work);
/**
 * @brief Read the current time from the RTC over I2C.
 * @param work PROTOCORE_RTC_BORROW bytes the caller took. Not held past the call.
 * @return The uint32_t.
 */
uint32_t protocore_rtc_read_epoch(uint8_t *restrict work);
/**
 * @brief Set the RTC to epoch over I2C. true if the write succeeded.
 * @param work PROTOCORE_RTC_BORROW bytes the caller took. Not held past the call.
 * @param epoch Epoch
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_rtc_set_epoch(uint8_t *restrict work, uint32_t epoch);
/**
 * @brief A ::TimeSourceFn wrapper (returns protocore_rtc_read_epoch()) to .
 * @param work PROTOCORE_RTC_BORROW bytes the caller took. Not held past the call.
 */
void protocore_rtc_time_source(uint8_t *restrict work);

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

/** @brief Module namespace. */
PROTOCORE_NS RtcNs Rtc PROTOCORE_UNUSED = {.regs_to_epoch = protocore_rtc_regs_to_epoch,
                                           .epoch_to_regs = protocore_rtc_epoch_to_regs,
                                           .begin = protocore_rtc_begin,
                                           .read_epoch = protocore_rtc_read_epoch,
                                           .set_epoch = protocore_rtc_set_epoch,
                                           .time_source = protocore_rtc_time_source};

PROTOCORE_END_DECLS

#endif // PROTOCORE_RTC_H
