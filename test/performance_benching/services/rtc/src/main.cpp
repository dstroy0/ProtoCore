// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the RTC codec (services/peripherals/rtc): the pure BCD-register <-> Unix
// epoch conversions pc_rtc_regs_to_epoch() / pc_rtc_epoch_to_regs() (24h/12h encodings, leap
// years). The I2C register read/write (pc_rtc_begin/read_epoch/set_epoch) is real-hardware and out
// of scope here; only the deterministic conversion math is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/rtc -t upload --upload-port COM7
#include "device_bench.h"
#include "services/peripherals/rtc/rtc.h"
#include <Arduino.h>

static void rtc_bench_task(void *)
{
    // 2026-07-04 12:34:56, Sat: s,m,h,dow,date,month,year (BCD) - from test/test_rtc.
    static const uint8_t regs[RTC_REG_COUNT] = {0x56, 0x34, 0x12, 0x06, 0x04, 0x07, 0x26};

    for (;;)
    {
        Serial.printf("DB ==== rtc device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        uint32_t epoch = 0;
        DBENCH_OP("pc_rtc_regs_to_epoch", 200000, sink += pc_rtc_regs_to_epoch(regs, &epoch));
        uint8_t out[RTC_REG_COUNT];
        DBENCH_OP("pc_rtc_epoch_to_regs", 200000, {
            pc_rtc_epoch_to_regs(1751632496u, out);
            sink += out[0];
        });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: rtc device microbench");
    xTaskCreatePinnedToCore(rtc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
