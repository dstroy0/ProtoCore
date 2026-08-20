// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rtc.c
 * @brief DS1307/DS3231 RTC driver - implementation. See rtc.h.
 *
 * The date math is H. Hinnant's proleptic-Gregorian days<->civil algorithms, so it is exact
 * for any date and needs no lookup tables or stdlib time functions.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_RTC

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_RTC needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/i2c.h"
#include "server/peripherals/rtc/rtc.h"

PROTOCORE_BEGIN_DECLS

static int bcd2int(uint8_t b)
{
    return (b >> 4) * 10 + (b & 0x0F);
}
static uint8_t int2bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

// Days from 1970-01-01 for a civil (y, m, d), and its inverse.
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    // feeds year = 2000 + bcd2int(r[6]) (uint8_t, so
    // bcd2int >= 0) minus at most 1 for m<=2, so y >= 1999
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}
static void civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    // caller passes days = epoch/86400u, and epoch
    // is uint32_t so days is always >= 0
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    *d = (int)(doy - (153u * mp + 2u) / 5u + 1u);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes
} RtcOwnCtx;
static RtcOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_rtc_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_I2C_DEVICE_BORROW).buf;
    }
    return s_own.span;
}

void protocore_rtc_epoch_to_regs(uint8_t *restrict work);
void protocore_rtc_read_epoch(uint8_t *restrict work);
void protocore_rtc_regs_to_epoch(uint8_t *restrict work);

void protocore_rtc_regs_to_epoch(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *r = RtcV.regs_to_epoch_args.regs;
    uint32_t *epoch = RtcV.regs_to_epoch_args.epoch;

    if (!r || !epoch)
    {
        RtcV.ok = PROTO_FALSE;
        return;
    }
    int sec = bcd2int(r[0] & 0x7F); // mask the DS1307 clock-halt bit
    int min = bcd2int(r[1] & 0x7F);
    int hour;
    if (r[2] & 0x40) // 12-hour mode
    {
        int h12 = bcd2int(r[2] & 0x1F);
        if (h12 < 1 || h12 > 12)
        {
            RtcV.ok = PROTO_FALSE;
            return;
        }
        proto_bool pm = (r[2] & 0x20) != 0;
        hour = (h12 % 12) + (pm ? 12 : 0);
    }
    else
    {
        hour = bcd2int(r[2] & 0x3F);
    }
    int date = bcd2int(r[4] & 0x3F);
    int month = bcd2int(r[5] & 0x1F); // mask the DS3231 century bit
    int year = 2000 + bcd2int(r[6]);
    if (sec > 59 || min > 59 || hour > 23 || date < 1 || date > 31 || month < 1 || month > 12)
    {
        RtcV.ok = PROTO_FALSE;
        return;
    }
    // int64: days*86400 exceeds a 32-bit long (Windows host and ESP32 both) past ~2038.
    int64_t t = (int64_t)days_from_civil(year, month, date) * 86400 + hour * 3600 + min * 60 + sec;
    if (t < 0 || t > 0xFFFFFFFFLL)
    // is always >= 2000, so days_from_civil (and t) is always positive;
    // t > 0xFFFFFFFF (year rollover past 2106) is real and tested below
    {
        RtcV.ok = PROTO_FALSE;
        return;
    }
    *epoch = (uint32_t)t;
    RtcV.ok = PROTO_TRUE;
}

void protocore_rtc_epoch_to_regs(uint8_t *restrict work)
{
    (void)work;
    uint32_t epoch = RtcV.epoch_to_regs_args.epoch;
    uint8_t *r = RtcV.epoch_to_regs_args.regs;

    long days = (long)(epoch / 86400u);
    int rem = (int)(epoch % 86400u);
    int y = 0;
    int m = 0;
    int d = 0;
    civil_from_days(days, &y, &m, &d);
    r[0] = int2bcd(rem % 60);
    r[1] = int2bcd((rem % 3600) / 60);
    r[2] = int2bcd(rem / 3600);                   // 24-hour mode (bit 6 clear)
    r[3] = (uint8_t)((((days % 7) + 3) % 7) + 1); // 1 = Mon .. 7 = Sun
    r[4] = int2bcd(d);
    r[5] = int2bcd(m); // century bit clear
    r[6] = int2bcd(y - 2000);
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

// All RTC I2C-binding state, owned by one instance (internal linkage): the bus frame, which is a
// register-pointer byte followed by the seven time registers. It is a member rather than a local
// because a transfer is composed in place, and eight bytes is the widest this part moves.
typedef struct
{
    uint8_t frame[1 + RTC_REG_COUNT];
} RtcCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define RTC_OFF_CTX 0u
static_assert(RTC_OFF_CTX + sizeof(RtcCtx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(RTC_OFF_CTX % _Alignof(RtcCtx) == 0,
              "RTC_OFF_CTX is not a multiple of alignof(RtcCtx) - RTC_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define RTC_CTX(w) ((RtcCtx *)(void *)((w) + RTC_OFF_CTX))

void protocore_rtc_begin(uint8_t *restrict work)
{
    (void)work;

    protocore_i2c_begin();
    RtcV.ok = PROTO_TRUE;
}

void protocore_rtc_read_epoch(uint8_t *restrict work)
{

    uint8_t reg = 0x00; // register 0: seconds
    if (!protocore_i2c_write_read(PROTOCORE_RTC_I2C_ADDR, &reg, 1, RTC_CTX(work)->frame, RTC_REG_COUNT))
    {
        RtcV.epoch = 0;
        return;
    }
    uint32_t e = 0;
    RtcV.regs_to_epoch_args.regs = RTC_CTX(work)->frame;
    RtcV.regs_to_epoch_args.epoch = &e;
    protocore_rtc_regs_to_epoch(work);
    RtcV.epoch = RtcV.ok ? e : 0;
}

void protocore_rtc_set_epoch(uint8_t *restrict work)
{
    uint32_t epoch = RtcV.set_epoch_args.epoch;

    RTC_CTX(work)->frame[0] = 0x00; // point at register 0, then the seven registers follow it
    RtcV.epoch_to_regs_args.epoch = epoch;
    RtcV.epoch_to_regs_args.regs = &RTC_CTX(work)->frame[1];
    protocore_rtc_epoch_to_regs(work);
    RtcV.ok = protocore_i2c_write(PROTOCORE_RTC_I2C_ADDR, RTC_CTX(work)->frame, sizeof(RTC_CTX(work)->frame));
}

void protocore_rtc_time_source(uint8_t *restrict work)
{
    (void)work;

    protocore_rtc_read_epoch(work);
}

/** @brief The operands and the outcome. */
RtcVars RtcV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RTC
