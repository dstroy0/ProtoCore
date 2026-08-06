// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/hw_health: rail droop, SPI CRC backoff, GPIO short, cap leakage.

#include "server/signaling/hw_health.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_rail_monitor(void)
{
    HwRailMonitor m;
    pc_hwhealth_rail_init(&m, 3300, 3100, 2900); // nominal 3.3V, warn 3.1V, crit 2.9V
    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, pc_hwhealth_rail_sample(&m, 3300));
    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, pc_hwhealth_rail_sample(&m, 3100)); // boundary: == warn is OK
    TEST_ASSERT_EQUAL_INT(HW_RAIL_SAG, pc_hwhealth_rail_sample(&m, 3050));
    TEST_ASSERT_EQUAL_INT(HW_RAIL_SAG, pc_hwhealth_rail_sample(&m, 2900)); // == crit is still SAG
    TEST_ASSERT_EQUAL_INT(HW_RAIL_BROWNOUT, pc_hwhealth_rail_sample(&m, 2800));
    TEST_ASSERT_EQUAL_UINT32(2800, m.min_mv);
    TEST_ASSERT_EQUAL_UINT32(2, m.sag_events);
    TEST_ASSERT_EQUAL_UINT32(1, m.brownout_events);

    char buf[96];
    size_t n = pc_hwhealth_rail_json(&m, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_EQUAL_STRING("{\"nominal_mv\":3300,\"min_mv\":2800,\"sag\":2,\"brownout\":1}", buf);
    // Overflow path returns 0.
    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_hwhealth_rail_json(&m, tiny, sizeof(tiny)));
}

void test_spi_backoff(void)
{
    HwSpiBackoff s;
    pc_hwhealth_spi_init(&s, 8000000, 1000000, 8000000, 3, 4); // 8MHz, floor 1MHz, ceil 8MHz
    // Two failures: not yet tripped.
    TEST_ASSERT_EQUAL_UINT32(8000000, pc_hwhealth_spi_result(&s, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(8000000, pc_hwhealth_spi_result(&s, PROTO_FALSE));
    // Third consecutive failure halves the clock.
    TEST_ASSERT_EQUAL_UINT32(4000000, pc_hwhealth_spi_result(&s, PROTO_FALSE));
    // A success resets the fail streak (no change yet).
    TEST_ASSERT_EQUAL_UINT32(4000000, pc_hwhealth_spi_result(&s, PROTO_TRUE));
    // Three more failures halve again.
    pc_hwhealth_spi_result(&s, PROTO_FALSE);
    pc_hwhealth_spi_result(&s, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT32(2000000, pc_hwhealth_spi_result(&s, PROTO_FALSE));
    // Four consecutive successes step back up.
    pc_hwhealth_spi_result(&s, PROTO_TRUE);
    pc_hwhealth_spi_result(&s, PROTO_TRUE);
    pc_hwhealth_spi_result(&s, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT32(4000000, pc_hwhealth_spi_result(&s, PROTO_TRUE));
}

void test_spi_backoff_clamps(void)
{
    HwSpiBackoff s;
    pc_hwhealth_spi_init(&s, 2000000, 1000000, 8000000, 1, 1);
    // Fail once (trip=1): halve to 1MHz floor. Fail again: stays at floor.
    TEST_ASSERT_EQUAL_UINT32(1000000, pc_hwhealth_spi_result(&s, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(1000000, pc_hwhealth_spi_result(&s, PROTO_FALSE));
    // Succeed to climb: 2M, 4M, 8M, then clamp at ceil.
    TEST_ASSERT_EQUAL_UINT32(2000000, pc_hwhealth_spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(4000000, pc_hwhealth_spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(8000000, pc_hwhealth_spi_result(&s, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(8000000, pc_hwhealth_spi_result(&s, PROTO_TRUE));
}

void test_gpio_short(void)
{
    TEST_ASSERT_EQUAL_INT(HW_GPIO_OK, pc_hwhealth_gpio_short(PROTO_TRUE, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(HW_GPIO_OK, pc_hwhealth_gpio_short(PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT(HW_GPIO_SHORT_GND, pc_hwhealth_gpio_short(PROTO_TRUE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT(HW_GPIO_SHORT_VCC, pc_hwhealth_gpio_short(PROTO_FALSE, PROTO_TRUE));
}

void test_cap_leak(void)
{
    // Expected 100ms decay, 10% tolerance -> [90, 110].
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, pc_hwhealth_cap_leak(100, 100, 10));
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, pc_hwhealth_cap_leak(90, 100, 10));        // boundary lo
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, pc_hwhealth_cap_leak(110, 100, 10));       // boundary hi
    TEST_ASSERT_EQUAL_INT(HW_CAP_LEAK, pc_hwhealth_cap_leak(80, 100, 10));      // too fast
    TEST_ASSERT_EQUAL_INT(HW_CAP_HIGH_ESR, pc_hwhealth_cap_leak(130, 100, 10)); // too slow
    // Degenerate expected.
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, pc_hwhealth_cap_leak(50, 0, 10));
}

void test_rail_ok_spi_clamps_probes()
{
    HwRailMonitor m;
    pc_hwhealth_rail_init(&m, 3300, 3000, 2800);
    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, pc_hwhealth_rail_sample(&m, 3300)); // nominal -> OK
    HwSpiBackoff s;
    pc_hwhealth_spi_init(&s, 1000000, 100000, 8000000, 2, 2);
    for (int i = 0; i < 80; i++)
    {
        pc_hwhealth_spi_result(&s, PROTO_FALSE); // repeated CRC fails drive toward min_hz
    }
    for (int i = 0; i < 80; i++)
    {
        pc_hwhealth_spi_result(&s, PROTO_TRUE); // repeated successes ramp toward max_hz
    }
    pc_hwhealth_gpio_short(PROTO_TRUE, PROTO_FALSE);
    pc_hwhealth_cap_leak(90, 100, 5);
    TEST_PASS();
}

// Null-pointer guards on every entry point (no crash; safe defaults), and the SPI init
// start-clock clamps (below the floor -> min_hz, above the ceiling -> max_hz).
void test_hwhealth_null_guards_and_init_clamps(void)
{
    char buf[64];
    pc_hwhealth_rail_init(NULL, 3300, 3100, 2900); // no-op, no crash
    TEST_ASSERT_EQUAL_INT(HW_RAIL_OK, pc_hwhealth_rail_sample(NULL, 1000));
    TEST_ASSERT_EQUAL_size_t(0, pc_hwhealth_rail_json(NULL, buf, sizeof(buf))); // null monitor

    HwRailMonitor m;
    pc_hwhealth_rail_init(&m, 3300, 3100, 2900);
    TEST_ASSERT_EQUAL_size_t(0, pc_hwhealth_rail_json(&m, NULL, sizeof(buf))); // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_hwhealth_rail_json(&m, buf, 0));            // zero cap

    pc_hwhealth_spi_init(NULL, 1, 1, 1, 1, 1); // no-op, no crash
    TEST_ASSERT_EQUAL_UINT32(0, pc_hwhealth_spi_result(NULL, PROTO_TRUE));

    // init clamps the start clock into [min_hz, max_hz]; read it back via a non-tripping
    // result (fail_trip=2, so one failure leaves hz unchanged).
    HwSpiBackoff below;
    pc_hwhealth_spi_init(&below, 500000, 1000000, 8000000, 2, 2); // start < min_hz
    TEST_ASSERT_EQUAL_UINT32(1000000, pc_hwhealth_spi_result(&below, PROTO_FALSE));
    HwSpiBackoff above;
    pc_hwhealth_spi_init(&above, 20000000, 1000000, 8000000, 2, 2); // start > max_hz
    TEST_ASSERT_EQUAL_UINT32(8000000, pc_hwhealth_spi_result(&above, PROTO_FALSE));
}

// Remaining branch gaps: trip=0 defaulting to 1, the hz<<1 overflow clamp in the SPI
// backoff, and the tolerance-band underflow guard (band >= expected) in cap_leak.
void test_hwhealth_trip_defaults_overflow_and_band_clamp(void)
{
    // fail_trip=0 / ok_trip=0 default to 1 (ternary false branch): trips on the very
    // first sample instead of requiring a streak.
    HwSpiBackoff s;
    pc_hwhealth_spi_init(&s, 4000000, 1000000, 8000000, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(2000000, pc_hwhealth_spi_result(&s, PROTO_FALSE)); // fail_trip defaulted to 1
    TEST_ASSERT_EQUAL_UINT32(4000000, pc_hwhealth_spi_result(&s, PROTO_TRUE));  // ok_trip defaulted to 1

    // hz<<1 overflow: start near the top of the 32-bit range so doubling wraps below hz;
    // the wrap must be detected and clamped to max_hz rather than left wrapped.
    HwSpiBackoff ov;
    pc_hwhealth_spi_init(&ov, 4026531840UL, 4026531840UL, 4294967295UL, 1, 1);
    TEST_ASSERT_EQUAL_UINT32(4294967295UL, pc_hwhealth_spi_result(&ov, PROTO_TRUE));

    // Tolerance band >= expected: lo clamps to 0 instead of underflowing.
    TEST_ASSERT_EQUAL_INT(HW_CAP_OK, pc_hwhealth_cap_leak(0, 50, 100)); // band(50) >= expected(50)
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hwhealth_null_guards_and_init_clamps);
    RUN_TEST(test_hwhealth_trip_defaults_overflow_and_band_clamp);
    RUN_TEST(test_rail_monitor);
    RUN_TEST(test_spi_backoff);
    RUN_TEST(test_spi_backoff_clamps);
    RUN_TEST(test_gpio_short);
    RUN_TEST(test_cap_leak);
    RUN_TEST(test_rail_ok_spi_clamps_probes);
    return UNITY_END();
}
