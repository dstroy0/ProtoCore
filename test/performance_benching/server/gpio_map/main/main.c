// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the GPIO pin-mapper / browser diag core (server/signaling/gpio_map):
// the four pure, host-tested functions that turn a caller-owned pin table into the browser panel's
// JSON and back - protocore_gpio_dir_name() (direction -> short name), protocore_gpio_json() (serialize the pin
// table into a caller buffer, no allocation), protocore_gpio_parse_set() (parse a `pin=<n>&level=<0|1>`
// control POST body), and protocore_gpio_is_output() (the guard that gates a drive). All four are pure and
// touch no peripheral. The digital half - protocore_gpio_begin_pins/read/write (pinMode / digitalRead /
// digitalWrite) - is deliberately out of scope: this rig maps no real pins, and those calls do live
// bus/pin I/O, so they are never invoked here (contrast performance_benching/device/modbus, a codec with no hardware
// at all; here only the codec half of a hardware feature is benched).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/gpio_map -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/signaling/gpio_map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t gpio_map_work[16]; // the borrow an entry takes; GpioMap never reads it

/** @brief The short name for direction @p dir. */
static const char *gpio_dir_name(protocore_gpio_dir dir)
{
    GpioMap.args.dir = dir;
    GpioMap.dir_name(gpio_map_work);
    return GpioMap.text;
}

/** @brief Serialize the @p count pins at @p pins into @p out; the characters written. */
static int32_t gpio_json(const protocore_gpio_pin *pins, uint8_t count, char *out, uint32_t cap)
{
    GpioMap.args.pins = pins;
    GpioMap.args.count = count;
    GpioMap.out_args.out = out;
    GpioMap.out_args.cap = cap;
    GpioMap.json(gpio_map_work);
    return GpioMap.n;
}

/** @brief Parse `pin=<n>&level=<0|1>` out of @p body into @p pin and @p level. */
static proto_bool gpio_parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level)
{
    GpioMap.parse_args.body = body;
    GpioMap.parse_args.len = len;
    GpioMap.parse_args.pin_out = pin;
    GpioMap.parse_args.level_out = level;
    GpioMap.parse_set(gpio_map_work);
    return GpioMap.ok;
}

/** @brief Whether @p pin is mapped as an output in the @p count pins at @p pins. */
static proto_bool gpio_is_output(const protocore_gpio_pin *pins, uint8_t count, uint8_t pin)
{
    GpioMap.args.pins = pins;
    GpioMap.args.count = count;
    GpioMap.args.pin = pin;
    GpioMap.is_output(gpio_map_work);
    return GpioMap.ok;
}

void dbench_run(void)
{
    // Realistic pin table copied from test/test_gpio_map/test_gpio_map.cpp (known-good, spec-conformant):
    // a driven LED output plus a pulled-up BOOT input, extended with a couple more mapped pins so the
    // serializer walks a fuller table.
    static protocore_gpio_pin pins[4] = {
        {2, "LED", PROTOCORE_GPIO_OUT, 1},
        {0, "BOOT", PROTOCORE_GPIO_IN_PULLUP, 0},
        {4, "RELAY", PROTOCORE_GPIO_OUT, 0},
        {15, "SENSE", PROTOCORE_GPIO_IN_PULLDOWN, 1},
    };
    static const uint8_t kCount = 4;

    // A well-formed control body straight out of the parser's unit test.
    static const char body[] = "pin=2&level=1";
    static const size_t body_len = sizeof(body) - 1;

    static char json[PROTOCORE_GPIO_JSON_BUF];

    for (;;)
    {
        DBENCH_BANNER("gpio_map");
        volatile size_t sink = 0;
        volatile bool bsink = false;
        volatile const char *csink = NULL;

        // Direction -> short name: a tiny switch, the cheapest op; large N.
        DBENCH_OP("GpioMap.dir_name", 200000, csink = gpio_dir_name(pins[3].dir));

        // Serialize the whole pin table to JSON (the browser GET path). This is the heaviest op
        // (per-pin fmtbuf formatting), so bench it two ways: cyc/op and throughput over the bytes it
        // emits.
        DBENCH_OP("GpioMap.json (4 pins)", 20000, sink = (size_t)gpio_json(pins, kCount, json, sizeof(json)));
        {
            int32_t _n = gpio_json(pins, kCount, json, sizeof(json));
            DBENCH_BULK("GpioMap.json bytes", 20000, (size_t)_n,
                        sink = (size_t)gpio_json(pins, kCount, json, sizeof(json)));
        }

        // Parse the control POST body `pin=2&level=1` (the browser POST path); large N.
        uint8_t out_pin = 0;
        uint8_t out_level = 0;
        DBENCH_OP("GpioMap.parse_set", 100000, bsink = gpio_parse_set(body, body_len, &out_pin, &out_level));

        // Output guard that gates a drive (linear scan of the table); large N.
        DBENCH_OP("GpioMap.is_output", 100000, bsink = gpio_is_output(pins, kCount, 2));

        (void)sink;
        (void)bsink;
        (void)csink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("gpio_map")
