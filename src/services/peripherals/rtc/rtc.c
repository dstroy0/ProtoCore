// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rtc.c
 * @brief DS1307/DS3231 RTC driver - implementation. See rtc.h.
 *
 * The date math is H. Hinnant's proleptic-Gregorian days<->civil algorithms, so it is exact
 * for any date and needs no lookup tables or stdlib time functions.
 */

#include "services/peripherals/rtc/rtc.h"
#include "protocore_config.h"

#if PC_ENABLE_RTC

#if PROTOCORE_HOT
#include "services/peripherals/i2c.h"
#include <Wire.h>
#endif
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

proto_bool pc_rtc_regs_to_epoch(const uint8_t r[RTC_REG_COUNT], uint32_t *epoch)
{
    if (!r || !epoch)
    {
        return PROTO_FALSE;
    }
    int sec = bcd2int(r[0] & 0x7F); // mask the DS1307 clock-halt bit
    int min = bcd2int(r[1] & 0x7F);
    int hour;
    if (r[2] & 0x40) // 12-hour mode
    {
        int h12 = bcd2int(r[2] & 0x1F);
        if (h12 < 1 || h12 > 12)
        {
            return PROTO_FALSE;
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
        return PROTO_FALSE;
    }
    // int64: days*86400 exceeds a 32-bit long (Windows host and ESP32 both) past ~2038.
    int64_t t = (int64_t)days_from_civil(year, month, date) * 86400 + hour * 3600 + min * 60 + sec;
    if (t < 0 || t > 0xFFFFFFFFLL)
    // is always >= 2000, so days_from_civil (and t) is always positive;
    // t > 0xFFFFFFFF (year rollover past 2106) is real and tested below
    {
        return PROTO_FALSE;
    }
    *epoch = (uint32_t)t;
    return PROTO_TRUE;
}

void pc_rtc_epoch_to_regs(uint32_t epoch, uint8_t r[RTC_REG_COUNT])
{
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

#if PROTOCORE_HOT

proto_bool pc_rtc_begin()
{
    pc_i2c_begin();
    return PROTO_TRUE;
}

uint32_t pc_rtc_read_epoch()
{
    Wire.beginTransmission(PC_RTC_I2C_ADDR);
    Wire.write((uint8_t)0x00); // point at register 0 (seconds)
    if (Wire.endTransmission() != 0)
    {
        return 0; // no RTC on the bus
    }
    if (Wire.requestFrom((int)PC_RTC_I2C_ADDR, (int)RTC_REG_COUNT) != RTC_REG_COUNT)
    {
        return 0;
    }
    uint8_t r[RTC_REG_COUNT];
    for (int i = 0; i < RTC_REG_COUNT; i++)
    {
        r[i] = (uint8_t)Wire.read();
    }
    uint32_t e = 0;
    return pc_rtc_regs_to_epoch(r, &e) ? e : 0;
}

proto_bool pc_rtc_set_epoch(uint32_t epoch)
{
    uint8_t r[RTC_REG_COUNT];
    pc_rtc_epoch_to_regs(epoch, r);
    Wire.beginTransmission(PC_RTC_I2C_ADDR);
    Wire.write((uint8_t)0x00);
    for (int i = 0; i < RTC_REG_COUNT; i++)
    {
        Wire.write(r[i]);
    }
    return Wire.endTransmission() == 0;
}

uint32_t pc_rtc_time_source()
{
    return pc_rtc_read_epoch();
}

#else // host build: no I2C. The BCD<->epoch conversions above are host-tested.

proto_bool pc_rtc_begin()
{
    return PROTO_TRUE;
}
uint32_t pc_rtc_read_epoch()
{
    return 0;
}
proto_bool pc_rtc_set_epoch(uint32_t)
{
    return PROTO_FALSE;
}
uint32_t pc_rtc_time_source()
{
    return 0;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_RTC
