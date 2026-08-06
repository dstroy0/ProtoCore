// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the southbound driver registry (services/net/southbound): the
// name -> driver dispatch for read / write / read_block, over a registered mock driver. Pure
// bookkeeping + an indirect call; the real fieldbus driver is the application's.
//
// Build/flash:  pio run -d performance_benching/device/southbound -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/southbound/southbound.h"
#include <Arduino.h>

static int32_t g_points[64];
static int drv_read(void *, uint32_t point, int32_t *out)
{
    if (point >= 64)
    {
        return -1;
    }
    *out = g_points[point];
    return 0;
}
static int drv_write(void *, uint32_t point, int32_t value)
{
    if (point >= 64)
    {
        return -1;
    }
    g_points[point] = value;
    return 0;
}
static int drv_read_block(void *, uint32_t first, int32_t *out, size_t n)
{
    for (size_t i = 0; i < n && first + i < 64; i++)
    {
        out[i] = g_points[first + i];
    }
    return (int)n;
}

static void southbound_bench_task(void *)
{
    static const SouthboundDriver drv = {"plc1", drv_read, drv_write, drv_read_block, nullptr, nullptr};
    pc_southbound_clear();
    pc_southbound_register(&drv);

    for (;;)
    {
        Serial.printf("DB ==== southbound device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        int32_t v = 0;
        DBENCH_OP("pc_southbound_read (dispatch)", 200000, sink += pc_southbound_read("plc1", 5, &v));
        DBENCH_OP("pc_southbound_write (dispatch)", 200000, sink += pc_southbound_write("plc1", 5, sink));
        int32_t block[16];
        DBENCH_OP("pc_southbound_read_block x16", 100000, sink += pc_southbound_read_block("plc1", 0, block, 16));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: southbound device microbench");
    xTaskCreatePinnedToCore(southbound_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
