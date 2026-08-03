// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the GPIO pin-mapper / browser diag core (server/signaling/gpio_map):
// the four pure, host-tested functions that turn a caller-owned pin table into the browser panel's
// JSON and back - pc_gpio_dir_name() (direction -> short name), pc_gpio_json() (serialize the pin
// table into a caller buffer, no allocation), pc_gpio_parse_set() (parse a `pin=<n>&level=<0|1>`
// control POST body), and pc_gpio_is_output() (the guard that gates a drive). All four are pure and
// touch no peripheral. The digital half - pc_gpio_begin_pins/read/write (pinMode / digitalRead /
// digitalWrite) - is deliberately out of scope: this rig maps no real pins, and those calls do live
// bus/pin I/O, so they are never invoked here (contrast performance_benching/device/modbus, a codec with no hardware
// at all; here only the codec half of a hardware feature is benched).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/gpio_map -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/signaling/gpio_map.h"
#include <Arduino.h>

static void gpio_map_bench_task(void *)
{
    // Realistic pin table copied from test/test_gpio_map/test_gpio_map.cpp (known-good, spec-conformant):
    // a driven LED output plus a pulled-up BOOT input, extended with a couple more mapped pins so the
    // serializer walks a fuller table.
    static pc_gpio_pin pins[4] = {
        {2, "LED", pc_gpio_dir::PC_GPIO_OUT, 1},
        {0, "BOOT", pc_gpio_dir::PC_GPIO_IN_PULLUP, 0},
        {4, "RELAY", pc_gpio_dir::PC_GPIO_OUT, 0},
        {15, "SENSE", pc_gpio_dir::PC_GPIO_IN_PULLDOWN, 1},
    };
    static const uint8_t kCount = 4;

    // A well-formed control body straight out of the parser's unit test.
    static const char body[] = "pin=2&level=1";
    static const size_t body_len = sizeof(body) - 1;

    static char json[PC_GPIO_JSON_BUF];

    for (;;)
    {
        Serial.printf("DB ==== gpio_map device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool bsink = false;
        volatile const char *csink = nullptr;

        // Direction -> short name: a tiny switch, the cheapest op; large N.
        DBENCH_OP("pc_gpio_dir_name", 200000, csink = pc_gpio_dir_name(pins[3].dir));

        // Serialize the whole pin table to JSON (the browser GET path). This is the heaviest op
        // (per-pin fmtbuf formatting), so bench it two ways: cyc/op and throughput over the bytes it
        // emits.
        DBENCH_OP("pc_gpio_json (4 pins)", 20000, sink = (size_t)pc_gpio_json(pins, kCount, json, sizeof(json)));
        {
            int _n = pc_gpio_json(pins, kCount, json, sizeof(json));
            DBENCH_BULK("pc_gpio_json bytes", 20000, (size_t)_n,
                        sink = (size_t)pc_gpio_json(pins, kCount, json, sizeof(json)));
        }

        // Parse the control POST body `pin=2&level=1` (the browser POST path); large N.
        uint8_t out_pin = 0;
        uint8_t out_level = 0;
        DBENCH_OP("pc_gpio_parse_set", 100000, bsink = pc_gpio_parse_set(body, body_len, &out_pin, &out_level));

        // Output guard that gates a drive (linear scan of the table); large N.
        DBENCH_OP("pc_gpio_is_output", 100000, bsink = pc_gpio_is_output(pins, kCount, 2));

        (void)sink;
        (void)bsink;
        (void)csink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: gpio_map device microbench");
    xTaskCreatePinnedToCore(gpio_map_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
