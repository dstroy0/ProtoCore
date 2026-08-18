// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the hardware-health decision cores (server/signaling/hw_health.h).
//
// No external standard governs a rail droop logger or an SPI clock backoff, so every threshold
// expectation here is category 3: arithmetic derived from the header's own field definitions, with
// the derivation written beside it. The one exception is the report, and it is the load-bearing
// case: rail_json emits JSON, so test_rail_json_is_an_rfc8259_object holds it to RFC 8259 sec 4's
// object grammar and sec 6's int production ("leading zeros are not allowed"), which is a published
// grammar rather than whatever this builder happens to produce.

#include "server/signaling/hw_health.h"
#include <string.h>

#include <unity.h>

static uint8_t hw_health_work[16]; // the borrow an entry takes; HwHealth never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static void rail_init(HwRailMonitor *m, uint32_t nominal, uint32_t warn, uint32_t crit)
{
    HwHealth.rail.m = m;
    HwHealth.rail.nominal_mv = nominal;
    HwHealth.rail.warn_mv = warn;
    HwHealth.rail.crit_mv = crit;
    HwHealth.rail_init(hw_health_work);
}

static HwRailVerdict rail_sample(HwRailMonitor *m, uint32_t mv)
{
    HwHealth.rail.m = m;
    HwHealth.rail.mv = mv;
    HwHealth.rail_sample(hw_health_work);
    return HwHealth.rail_verdict;
}

static size_t rail_json(const HwRailMonitor *m, char *out, size_t cap)
{
    HwHealth.rail.m_ro = m;
    HwHealth.out_args.out = out;
    HwHealth.out_args.cap = cap;
    HwHealth.rail_json(hw_health_work);
    return HwHealth.n;
}

static void spi_init(HwSpiBackoff *s, uint32_t start, uint32_t lo, uint32_t hi, uint16_t fail_trip, uint16_t ok_trip)
{
    HwHealth.spi.s = s;
    HwHealth.spi.start_hz = start;
    HwHealth.spi.min_hz = lo;
    HwHealth.spi.max_hz = hi;
    HwHealth.spi.fail_trip = fail_trip;
    HwHealth.spi.ok_trip = ok_trip;
    HwHealth.spi_init(hw_health_work);
}

static uint32_t spi_result(HwSpiBackoff *s, proto_bool crc_ok)
{
    HwHealth.spi.s = s;
    HwHealth.spi.crc_ok = crc_ok;
    HwHealth.spi_result(hw_health_work);
    return HwHealth.hz;
}

static HwGpioVerdict gpio_short(proto_bool driven_high, proto_bool read_high)
{
    HwHealth.probe.driven_high = driven_high;
    HwHealth.probe.read_high = read_high;
    HwHealth.gpio_short(hw_health_work);
    return HwHealth.gpio_verdict;
}

static HwCapVerdict cap_leak(uint32_t measured_ms, uint32_t expected_ms, uint8_t tol_pct)
{
    HwHealth.probe.measured_ms = measured_ms;
    HwHealth.probe.expected_ms = expected_ms;
    HwHealth.probe.tol_pct = tol_pct;
    HwHealth.cap_leak(hw_health_work);
    return HwHealth.cap_verdict;
}

// hw_health.h defines warn_mv and crit_mv as "below this", so both comparisons are strict and a
// reading exactly at a threshold sits in the band above it: warn is OK, crit is SAG.
void test_rail_thresholds_are_strictly_below(void)
{
    HwRailMonitor m;
    rail_init(&m, 3300, 3100, 2900);

    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, rail_sample(&m, 3300));
    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, rail_sample(&m, 3101));
    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, rail_sample(&m, 3100));
    TEST_ASSERT_EQUAL_INT(HW_RAIL_SAG, rail_sample(&m, 3099));
    TEST_ASSERT_EQUAL_INT(HW_RAIL_SAG, rail_sample(&m, 2900));
    TEST_ASSERT_EQUAL_INT(HW_RAIL_BROWNOUT, rail_sample(&m, 2899));
}

// min_mv is seeded at nominal and only ever falls, so it is the minimum of nominal and every
// reading: min(3300, 3300, 3050, 2800, 3290) = 2800. The two crossings are counted once each.
void test_rail_min_is_the_worst_droop_and_events_tally(void)
{
    HwRailMonitor m;
    rail_init(&m, 3300, 3100, 2900);
    TEST_ASSERT_EQUAL_UINT32(3300u, m.min_mv);

    (void)rail_sample(&m, 3300);
    (void)rail_sample(&m, 3050);
    (void)rail_sample(&m, 2800);
    (void)rail_sample(&m, 3290);

    TEST_ASSERT_EQUAL_UINT32(2800u, m.min_mv);
    TEST_ASSERT_EQUAL_UINT32(1u, m.sag_events);
    TEST_ASSERT_EQUAL_UINT32(1u, m.brownout_events);

    // A reading above nominal never raises the worst droop.
    (void)rail_sample(&m, 4000);
    TEST_ASSERT_EQUAL_UINT32(2800u, m.min_mv);
}

// RFC 8259 sec 4: object = begin-object [ member *( value-separator member ) ] end-object, and
// member = string name-separator value. Sec 6: int = zero / ( digit1-9 *DIGIT ), so a zero tally is
// the single character "0" and no count carries a leading zero. The report is one flat object of
// four number members in the order the builder writes them.
void test_rail_json_is_an_rfc8259_object(void)
{
    HwRailMonitor m;
    rail_init(&m, 3300, 3100, 2900);
    (void)rail_sample(&m, 3050);
    (void)rail_sample(&m, 3050);
    (void)rail_sample(&m, 2800);

    char buf[96];
    size_t n = rail_json(&m, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"nominal_mv\":3300,\"min_mv\":2800,\"sag\":2,\"brownout\":1}", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    // A monitor that has seen nothing reports zero as "0", not "" and not "00".
    HwRailMonitor fresh;
    rail_init(&fresh, 3300, 3100, 2900);
    (void)rail_json(&fresh, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"nominal_mv\":3300,\"min_mv\":3300,\"sag\":0,\"brownout\":0}", buf);
}

// A buffer too small for the whole object reports 0 rather than emitting a truncated one: half an
// object is not JSON, and a reader would fail to parse it instead of seeing a short report.
void test_rail_json_fails_closed_on_a_short_buffer(void)
{
    HwRailMonitor m;
    rail_init(&m, 3300, 3100, 2900);

    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0u, rail_json(&m, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_size_t(0u, rail_json(&m, tiny, 0));
    TEST_ASSERT_EQUAL_size_t(0u, rail_json(&m, NULL, sizeof(tiny)));
    TEST_ASSERT_EQUAL_size_t(0u, rail_json(NULL, tiny, sizeof(tiny)));
}

// fail_trip is the run length that halves the clock, so the first fail_trip-1 failures leave it
// alone: 8 MHz >> 1 = 4 MHz on the third. A success clears the run, so the count restarts.
void test_spi_halves_only_on_a_full_fail_streak(void)
{
    HwSpiBackoff s;
    spi_init(&s, 8000000u, 1000000u, 8000000u, 3, 4);

    TEST_ASSERT_EQUAL_UINT32(8000000u, spi_result(&s, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(8000000u, spi_result(&s, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_FALSE));

    // Two failures then a success: the run is broken, so a third failure does not halve.
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_FALSE));
}

// ok_trip successes double it: 4 MHz << 1 = 8 MHz on the fourth consecutive good transfer.
void test_spi_doubles_only_on_a_full_ok_streak(void)
{
    HwSpiBackoff s;
    spi_init(&s, 8000000u, 1000000u, 8000000u, 1, 4);
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_FALSE));

    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(8000000u, spi_result(&s, PROTO_TRUE));
}

// The clock never leaves [min_hz, max_hz]: 2 MHz >> 1 = 1 MHz is the floor and a further halving
// would give 500 kHz, which clamps; 8 MHz << 1 = 16 MHz is past the ceiling, which clamps.
void test_spi_clock_stays_between_floor_and_ceiling(void)
{
    HwSpiBackoff s;
    spi_init(&s, 2000000u, 1000000u, 8000000u, 1, 1);

    TEST_ASSERT_EQUAL_UINT32(1000000u, spi_result(&s, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(1000000u, spi_result(&s, PROTO_FALSE));

    TEST_ASSERT_EQUAL_UINT32(2000000u, spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(8000000u, spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(8000000u, spi_result(&s, PROTO_TRUE));
}

// init clamps the start clock into the band, and a zero trip count means "trip on the first
// transfer" rather than "never trip".
void test_spi_init_clamps_the_start_clock_and_defaults_a_zero_trip(void)
{
    // fail_trip 2, so one failure reports the clock without changing it.
    HwSpiBackoff below;
    spi_init(&below, 500000u, 1000000u, 8000000u, 2, 2);
    TEST_ASSERT_EQUAL_UINT32(1000000u, spi_result(&below, PROTO_FALSE));

    HwSpiBackoff above;
    spi_init(&above, 20000000u, 1000000u, 8000000u, 2, 2);
    TEST_ASSERT_EQUAL_UINT32(8000000u, spi_result(&above, PROTO_FALSE));

    HwSpiBackoff zero;
    spi_init(&zero, 4000000u, 1000000u, 8000000u, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(2000000u, spi_result(&zero, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(4000000u, spi_result(&zero, PROTO_TRUE));
}

// Doubling 0xF0000000 in 32 bits gives 0x1E0000000, which truncates to 0xE0000000 - a value BELOW
// the clock it came from. The wrap is caught and the ceiling installed instead of a clock that just
// went down on a good transfer.
void test_spi_doubling_wrap_clamps_to_the_ceiling(void)
{
    HwSpiBackoff s;
    spi_init(&s, 0xF0000000u, 0xF0000000u, 0xFFFFFFFFu, 1, 1);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, spi_result(&s, PROTO_TRUE));
}

// The pin's readback against what it was driven to: matching is OK, and each mismatch names the rail
// it is stuck on. Driving high and reading low is a pull to ground; driving low and reading high is
// a pull to Vcc.
void test_gpio_readback_mismatch_names_the_rail(void)
{
    TEST_ASSERT_EQUAL_INT(HW_GPIO_OK, gpio_short(PROTO_TRUE, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(HW_GPIO_OK, gpio_short(PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT(HW_GPIO_SHORT_GND, gpio_short(PROTO_TRUE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT(HW_GPIO_SHORT_VCC, gpio_short(PROTO_FALSE, PROTO_TRUE));
}

// band = expected * tol_pct / 100, and the accepted window is [expected-band, expected+band]
// inclusive. At expected 100 and tol 10 that is band 10 and the window [90, 110], so 90 and 110 pass
// while 89 is a leak and 111 is a high-ESR path.
void test_cap_tolerance_window_is_inclusive(void)
{
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(100, 100, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(90, 100, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(110, 100, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_LEAK, cap_leak(89, 100, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_HIGH_ESR, cap_leak(111, 100, 10));
}

// The band is integer arithmetic, so it truncates: 7 * 10 / 100 = 0 leaves the window [7, 7], and
// either neighbor is out. A zero expected time carries no window at all and never judges.
void test_cap_band_truncates_and_a_zero_expectation_never_judges(void)
{
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(7, 7, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_LEAK, cap_leak(6, 7, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_HIGH_ESR, cap_leak(8, 7, 10));

    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(50, 0, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(0, 0, 0));
}

// A band as wide as the expectation would put the low edge at or below zero. It clamps to 0 rather
// than wrapping around the unsigned range and rejecting everything: 50 * 100 / 100 = 50, so the
// window is [0, 100].
void test_cap_band_wider_than_expected_clamps_the_low_edge(void)
{
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(0, 50, 100));
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, cap_leak(100, 50, 100));
    TEST_ASSERT_EQUAL_INT(HW_CAP_HIGH_ESR, cap_leak(101, 50, 100));
}

// A call with no monitor reports the benign verdict and writes nothing, rather than following a
// null pointer.
void test_a_missing_monitor_is_refused(void)
{
    rail_init(NULL, 3300, 3100, 2900);
    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, rail_sample(NULL, 1000));

    spi_init(NULL, 8000000u, 1000000u, 8000000u, 1, 1);
    TEST_ASSERT_EQUAL_UINT32(0u, spi_result(NULL, PROTO_TRUE));
}
