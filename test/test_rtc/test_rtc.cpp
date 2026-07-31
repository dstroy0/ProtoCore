// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the DS1307/DS3231 RTC conversions (services/peripherals/rtc): the BCD time registers
// <-> Unix epoch, in both 24-hour and 12-hour encodings, with leap years, the clock-halt /
// century bit masks, range validation, and a round-trip bijection over the RTC's 2000-2099
// range. The I2C read/write is ESP32-only and not exercised here.

#include "services/peripherals/rtc/rtc.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// 2000-01-01 00:00:00 UTC is the classic anchor: Unix epoch 946684800.
void test_known_epoch_2000()
{
    uint8_t r[7] = {0x00, 0x00, 0x00, 0x07, 0x01, 0x01, 0x00}; // s,m,h,dow,date,month,year
    uint32_t e = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(r, &e));
    TEST_ASSERT_EQUAL_UINT32(946684800u, e);
}

// 2026-07-04 12:34:56 UTC (BCD registers) decodes to a known timestamp.
void test_decode_datetime()
{
    uint8_t r[7] = {0x56, 0x34, 0x12, 0x06, 0x04, 0x07, 0x26};
    uint32_t e = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(r, &e));
    TEST_ASSERT_EQUAL_UINT32(1783168496u, e);
}

void test_12hour_mode_equivalence()
{
    // 14:00 as 24-hour (0x14) and as 12-hour PM 2 (0x40|0x20|0x02) must be the same time.
    uint8_t base[7] = {0x00, 0x00, 0x14, 0x01, 0x01, 0x06, 0x24}; // 2024-06-01 14:00:00
    uint8_t twelve[7] = {0x00, 0x00, 0x62, 0x01, 0x01, 0x06, 0x24};
    uint32_t a = 0, b = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(base, &a));
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(twelve, &b));
    TEST_ASSERT_EQUAL_UINT32(a, b);
}

void test_12hour_midnight_and_noon()
{
    uint8_t midnight[7] = {0x00, 0x00, 0x52, 0x01, 0x01, 0x06, 0x24}; // 12h AM 12 -> 00:00
    uint8_t noon[7] = {0x00, 0x00, 0x72, 0x01, 0x01, 0x06, 0x24};     // 12h PM 12 -> 12:00
    uint8_t h24_0[7] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x06, 0x24};
    uint8_t h24_12[7] = {0x00, 0x00, 0x12, 0x01, 0x01, 0x06, 0x24};
    uint32_t am = 0, mid = 0, pm = 0, noon_ref = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(midnight, &am));
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(h24_0, &mid));
    TEST_ASSERT_EQUAL_UINT32(mid, am);
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(noon, &pm));
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(h24_12, &noon_ref));
    TEST_ASSERT_EQUAL_UINT32(noon_ref, pm);
}

void test_roundtrip_over_range()
{
    const uint32_t samples[] = {
        946684800u,  // 2000-01-01 00:00:00
        1000000000u, // 2001-09-09
        1583020800u, // 2020-03-01 (day after a leap day)
        1783168496u, // 2026-07-04 12:34:56
        4102444799u, // 2099-12-31 23:59:59 (top of the RTC's range)
    };
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    {
        uint8_t r[7];
        pc_rtc_epoch_to_regs(samples[i], r);
        uint32_t back = 0;
        TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(r, &back));
        TEST_ASSERT_EQUAL_UINT32(samples[i], back);
    }
}

void test_leap_day()
{
    uint8_t r[7] = {0x00, 0x00, 0x00, 0x04, 0x29, 0x02, 0x24}; // 2024-02-29 (valid leap day)
    uint32_t e = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(r, &e));
    uint8_t back[7];
    pc_rtc_epoch_to_regs(e, back);
    TEST_ASSERT_EQUAL_UINT8(0x29, back[4]); // date 29
    TEST_ASSERT_EQUAL_UINT8(0x02, back[5]); // February
}

void test_masks_ch_and_century()
{
    // The DS1307 clock-halt bit (sec bit7) and the DS3231 century bit (month bit7) must be
    // masked off, not fold into the value.
    uint8_t r[7] = {0x80 | 0x30, 0x00, 0x00, 0x01, 0x01, 0x80 | 0x06, 0x24}; // CH+30s, century+June
    uint32_t e = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(r, &e));
    uint8_t back[7];
    pc_rtc_epoch_to_regs(e, back);
    TEST_ASSERT_EQUAL_UINT8(0x30, back[0]); // 30 seconds (CH bit gone)
    TEST_ASSERT_EQUAL_UINT8(0x06, back[5]); // June (century bit gone)
}

void test_invalid_guards()
{
    uint32_t e = 0;
    uint8_t bad_sec[7] = {0x60, 0x00, 0x00, 0x01, 0x01, 0x01, 0x24}; // 60 seconds (max 59)
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_sec, &e));
    uint8_t bad_month[7] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x24}; // month 0
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_month, &e));
    uint8_t bad_date[7] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x24}; // date 0
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_date, &e));
    uint8_t bad_hour[7] = {0x00, 0x00, 0x24, 0x01, 0x01, 0x01, 0x24}; // 24 hours (24h mode)
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_hour, &e));
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_sec, nullptr)); // null out
}

// The other half of the null guard: a null register pointer must also be rejected (not just a
// null epoch out-pointer, which test_invalid_guards already covers).
void test_null_regs_pointer()
{
    uint32_t e = 0;
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(nullptr, &e));
}

// The upper-bound half of each range guard: minutes > 59, day-of-month > 31, and month > 12.
// test_invalid_guards already exercises the lower-bound / zero halves (sec, date, month, hour).
void test_invalid_guards_upper_bounds()
{
    uint32_t e = 0;
    uint8_t bad_min[7] = {0x00, 0x60, 0x00, 0x01, 0x01, 0x01, 0x24}; // 60 minutes (max 59)
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_min, &e));
    uint8_t bad_date[7] = {0x00, 0x00, 0x00, 0x01, 0x32, 0x01, 0x24}; // date 32 (max 31)
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_date, &e));
    uint8_t bad_month[7] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x13, 0x24}; // month 13 (max 12)
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(bad_month, &e));
}

// 12-hour mode with an out-of-range h12 digit (0, or 13+) must be rejected before it is ever
// folded into a 24-hour value.
void test_12hour_invalid_h12()
{
    uint32_t e = 0;
    uint8_t h12_zero[7] = {0x00, 0x00, 0x40, 0x01, 0x01, 0x06, 0x24}; // 12h mode, h12 = 0
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(h12_zero, &e));
    uint8_t h12_thirteen[7] = {0x00, 0x00, 0x53, 0x01, 0x01, 0x06, 0x24}; // 12h mode, h12 = 13
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(h12_thirteen, &e));
}

// A year field that decodes past 2106 (the 32-bit Unix epoch rollover) must be rejected rather
// than silently truncated - the registers themselves have no range check on the year byte.
void test_epoch_overflow_rejected()
{
    uint8_t r[7] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0xFF}; // year = 2000 + bcd2int(0xFF) = 2165
    uint32_t e = 0;
    TEST_ASSERT_FALSE(pc_rtc_regs_to_epoch(r, &e));
}

void test_host_i2c_stubs()
{
    // Host build: no I2C bus. begin() reports ready, reads yield 0, set fails, time source is 0.
    TEST_ASSERT_TRUE(pc_rtc_begin());
    TEST_ASSERT_EQUAL_UINT32(0, pc_rtc_read_epoch());
    TEST_ASSERT_FALSE(pc_rtc_set_epoch(1730882977u));
    TEST_ASSERT_EQUAL_UINT32(0, pc_rtc_time_source());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_known_epoch_2000);
    RUN_TEST(test_decode_datetime);
    RUN_TEST(test_12hour_mode_equivalence);
    RUN_TEST(test_12hour_midnight_and_noon);
    RUN_TEST(test_roundtrip_over_range);
    RUN_TEST(test_leap_day);
    RUN_TEST(test_masks_ch_and_century);
    RUN_TEST(test_invalid_guards);
    RUN_TEST(test_null_regs_pointer);
    RUN_TEST(test_invalid_guards_upper_bounds);
    RUN_TEST(test_12hour_invalid_h12);
    RUN_TEST(test_epoch_overflow_rejected);
    RUN_TEST(test_host_i2c_stubs);
    return UNITY_END();
}
