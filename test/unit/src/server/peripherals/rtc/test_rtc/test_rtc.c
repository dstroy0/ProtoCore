// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DS1307 / DS3231 time-register conversion (server/peripherals/rtc/rtc.h).
//
// The register layout is the DS3231 data sheet's Figure 1 and the "Clock and Calendar" section:
// seven BCD registers, hours bit 6 selecting 12- or 24-hour mode and bit 5 the AM/PM flag in
// 12-hour mode, and the century bit at bit 7 of the month register. Bit 7 of the seconds register
// is the DS1307's Clock Halt bit. The epoch values are arithmetic derived here from the definition
// of the Unix epoch and the Gregorian leap rule, with each derivation written beside its case.
//
// test_y2k_and_leap_days is the load-bearing case: 2000 is a leap year by the divisible-by-400
// rule while 1900 and 2100 are not, and a register pair that is one day out is a clock that drifts
// a day for four years and then silently corrects itself.

#include "server/peripherals/rtc/rtc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Build the seven registers in 24-hour mode, each field BCD (tens nibble, units nibble).
static uint8_t bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static void regs24(uint8_t r[RTC_REG_COUNT], int y, int mo, int d, int h, int mi, int s)
{
    r[0] = bcd(s);
    r[1] = bcd(mi);
    r[2] = bcd(h); // bit 6 clear: 24-hour mode
    r[3] = 1;
    r[4] = bcd(d);
    r[5] = bcd(mo);
    r[6] = bcd(y - 2000);
}

static uint32_t epoch_of(int y, int mo, int d, int h, int mi, int s)
{
    uint8_t r[RTC_REG_COUNT];
    uint32_t e = 0;
    regs24(r, y, mo, d, h, mi, s);
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_TRUE(Rtc.ok);
    return e;
}

// 1970-01-01 .. 2000-01-01 is 30 years, of which 1972/76/80/84/88/92/96 are leap = 7 extra days.
//   (30 * 365 + 7) * 86400 = 10957 * 86400 = 946684800
// 2000 is divisible by 400, so it IS a leap year and February has 29 days:
//   2000-02-29 = 10957 + 31 + 28 = 11016 days -> 11016 * 86400 = 951782400
//   and the day after it is March 1, which only lands right if February ran to 29.
void test_y2k_and_leap_days(void)
{
    TEST_ASSERT_EQUAL_UINT32(946684800u, epoch_of(2000, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(951782400u, epoch_of(2000, 2, 29, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(951782400u + 86400u, epoch_of(2000, 3, 1, 0, 0, 0));

    // 2016 is a plain divisible-by-four leap year.
    //   1970..2016 = 46 years, leap 1972..2012 every 4th = 11 days -> 46*365 + 11 = 16801 days
    //   +31 (January) +28 = 16860 days to 2016-02-29 -> 16860 * 86400 = 1456704000
    TEST_ASSERT_EQUAL_UINT32(1456704000u, epoch_of(2016, 2, 29, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(1456704000u + 86400u, epoch_of(2016, 3, 1, 0, 0, 0));

    // 2017 is not, so February ends at the 28th and March 1 is one day later.
    //   16801 + 366 = 17167 days to 2017-01-01 -> 1483228800
    //   +31 +28 = 17226 days to 2017-03-01     -> 1488326400
    TEST_ASSERT_EQUAL_UINT32(1483228800u, epoch_of(2017, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(1488326400u, epoch_of(2017, 3, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(1488326400u - 86400u, epoch_of(2017, 2, 28, 0, 0, 0));
}

// The time-of-day fields add to the day's start, and 946684800 above is the anchor.
//   12:34:56 = 12*3600 + 34*60 + 56 = 43200 + 2040 + 56 = 45296
void test_time_of_day_adds_to_the_date(void)
{
    TEST_ASSERT_EQUAL_UINT32(946684800u + 45296u, epoch_of(2000, 1, 1, 12, 34, 56));
    TEST_ASSERT_EQUAL_UINT32(946684800u + 86399u, epoch_of(2000, 1, 1, 23, 59, 59));
    TEST_ASSERT_EQUAL_UINT32(946684800u + 86400u, epoch_of(2000, 1, 2, 0, 0, 0));
}

// DS3231 "Clock and Calendar": bit 6 of the hours register selects 12-hour mode, and in that mode
// bit 5 is AM/PM with logic-high being PM. 12 AM is hour 0 and 12 PM is hour 12, which is the pair
// a naive (h12 + 12*pm) gets wrong in both directions.
void test_twelve_hour_encoding(void)
{
    struct
    {
        uint8_t h12;
        proto_bool pm;
        int hour24;
    } static const CASES[8] = {
        {12, PROTO_FALSE, 0},  // 12:00 AM is midnight
        {1, PROTO_FALSE, 1},   //
        {11, PROTO_FALSE, 11}, //
        {12, PROTO_TRUE, 12},  // 12:00 PM is noon
        {1, PROTO_TRUE, 13},   //
        {6, PROTO_TRUE, 18},   //
        {11, PROTO_TRUE, 23},  //
        {9, PROTO_FALSE, 9},   //
    };
    for (size_t i = 0; i < 8; i++)
    {
        uint8_t r[RTC_REG_COUNT];
        uint32_t got = 0;
        regs24(r, 2000, 1, 1, 0, 0, 0);
        r[2] = (uint8_t)(0x40 | (CASES[i].pm ? 0x20 : 0x00) | bcd(CASES[i].h12));
        Rtc.regs_to_epoch_args.regs = r;
        Rtc.regs_to_epoch_args.epoch = &got;
        Rtc.regs_to_epoch(protocore_rtc_span());
        TEST_ASSERT_TRUE(Rtc.ok);
        TEST_ASSERT_EQUAL_UINT32(946684800u + (uint32_t)CASES[i].hour24 * 3600u, got);
    }
}

// The DS1307 keeps its Clock Halt flag at bit 7 of the seconds register and the DS3231 its century
// flag at bit 7 of the month register. Neither is part of the value, so setting either must not
// move the time - a driver that reads them as BCD tens digits reads 80 seconds and month 18.
void test_clock_halt_and_century_bits_are_masked(void)
{
    uint8_t r[RTC_REG_COUNT];
    uint32_t plain = 0;
    uint32_t flagged = 0;
    regs24(r, 2024, 6, 15, 10, 30, 45);
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &plain;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_TRUE(Rtc.ok);

    r[0] |= 0x80; // DS1307 CH
    r[5] |= 0x80; // DS3231 century
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &flagged;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_TRUE(Rtc.ok);
    TEST_ASSERT_EQUAL_UINT32(plain, flagged);
}

// Figure 1 gives each register a range, so a field outside it is a bad or uninitialized part and
// the reading is refused rather than turned into some other instant.
void test_out_of_range_fields_are_refused(void)
{
    uint8_t r[RTC_REG_COUNT];
    uint32_t e = 0;

    regs24(r, 2020, 1, 1, 0, 0, 0);
    r[0] = bcd(60); // seconds 00-59
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);

    regs24(r, 2020, 1, 1, 0, 0, 0);
    r[1] = bcd(60); // minutes 00-59
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);

    regs24(r, 2020, 1, 1, 0, 0, 0);
    r[2] = bcd(24); // hours 00-23 in 24-hour mode
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);

    regs24(r, 2020, 1, 1, 0, 0, 0);
    r[2] = (uint8_t)(0x40 | bcd(13)); // hours 1-12 in 12-hour mode
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);
    r[2] = (uint8_t)(0x40 | bcd(0));
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);

    regs24(r, 2020, 1, 1, 0, 0, 0);
    r[4] = 0; // date 01-31
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);
    r[4] = bcd(32);
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);

    regs24(r, 2020, 1, 1, 0, 0, 0);
    r[5] = 0; // month 01-12
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);
    r[5] = bcd(13);
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);

    // an all-zero read, which is what an absent or never-set part looks like
    memset(r, 0, sizeof(r));
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);

    regs24(r, 2020, 1, 1, 0, 0, 0);
    Rtc.regs_to_epoch_args.regs = NULL;
    Rtc.regs_to_epoch_args.epoch = &e;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);
    Rtc.regs_to_epoch_args.regs = r;
    Rtc.regs_to_epoch_args.epoch = NULL;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_FALSE(Rtc.ok);
}

// The registers are BCD, not binary: the tens go in the high nibble. A binary write of 2024-06-15
// would read back as some other date entirely.
void test_epoch_to_regs_is_bcd_and_24_hour(void)
{
    uint8_t r[RTC_REG_COUNT];
    // 946684800 + 45296 is 2000-01-01 12:34:56 (see test_time_of_day_adds_to_the_date)
    Rtc.epoch_to_regs_args.epoch = 946684800u + 45296u;
    Rtc.epoch_to_regs_args.regs = r;
    Rtc.epoch_to_regs(protocore_rtc_span());
    TEST_ASSERT_EQUAL_HEX8(0x56, r[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, r[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, r[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, r[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, r[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, r[6]);
    TEST_ASSERT_EQUAL_UINT8(0, r[2] & 0x40); // bit 6 clear: 24-hour mode
    TEST_ASSERT_EQUAL_UINT8(0, r[5] & 0x80); // century bit clear

    // 23:00 must set the 20-hour bit (bit 5) that 24-hour mode gives that position, not the PM flag
    Rtc.epoch_to_regs_args.epoch = 946684800u + 23u * 3600u;
    Rtc.epoch_to_regs_args.regs = r;
    Rtc.epoch_to_regs(protocore_rtc_span());
    TEST_ASSERT_EQUAL_HEX8(0x23, r[2]);
    TEST_ASSERT_EQUAL_UINT8(0, r[2] & 0x40);
}

// The day-of-week register is 1 = Monday .. 7 = Sunday. 1970-01-01, epoch 0, was a Thursday, so it
// is 4; each following day steps by one and wraps after 7.
void test_day_of_week_from_the_epoch(void)
{
    static const uint8_t WANT[8] = {4, 5, 6, 7, 1, 2, 3, 4}; // Thu Fri Sat Sun Mon Tue Wed Thu
    for (uint32_t i = 0; i < 8; i++)
    {
        uint8_t r[RTC_REG_COUNT];
        Rtc.epoch_to_regs_args.epoch = i * 86400u;
        Rtc.epoch_to_regs_args.regs = r;
        Rtc.epoch_to_regs(protocore_rtc_span());
        TEST_ASSERT_EQUAL_UINT8(WANT[i], r[3]);
    }
    // 2000-01-01 was a Saturday: 10957 days after a Thursday, 10957 mod 7 = 2, Thu + 2 = Sat = 6.
    uint8_t r[RTC_REG_COUNT];
    Rtc.epoch_to_regs_args.epoch = 946684800u;
    Rtc.epoch_to_regs_args.regs = r;
    Rtc.epoch_to_regs(protocore_rtc_span());
    TEST_ASSERT_EQUAL_UINT8(6, r[3]);
}

// Encode then decode returns the instant unchanged across the whole span the year register can
// name (2000-2099), so no field is lost or aliased on the way through BCD.
void test_round_trip_over_the_register_range(void)
{
    // 100 years of seconds stepped by a value that is coprime with a day, an hour and a minute, so
    // the walk lands on every hour-of-day, minute and second rather than the same time each step.
    for (uint32_t e = 946684800u; e < 4102444799u; e += 1234567u)
    {
        uint8_t r[RTC_REG_COUNT];
        uint32_t back = 0;
        Rtc.epoch_to_regs_args.epoch = e;
        Rtc.epoch_to_regs_args.regs = r;
        Rtc.epoch_to_regs(protocore_rtc_span());
        Rtc.regs_to_epoch_args.regs = r;
        Rtc.regs_to_epoch_args.epoch = &back;
        Rtc.regs_to_epoch(protocore_rtc_span());
        TEST_ASSERT_TRUE(Rtc.ok);
        TEST_ASSERT_EQUAL_UINT32(e, back);
    }
    // and the last second the year register can express: 2099-12-31 23:59:59.
    //   1970..2099 = 129 years, leap 1972..2096 every 4th = 32 days (2000 counts, 2100 is not in
    //   range) -> 129*365 + 32 = 47117 days to 2099-01-01; 2099 is not leap so Dec 31 is day 364
    //   -> 47481 days * 86400 = 4102358400, + 23:59:59 (86399) = 4102444799
    TEST_ASSERT_EQUAL_UINT32(4102444799u, epoch_of(2099, 12, 31, 23, 59, 59));
}
