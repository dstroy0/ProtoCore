// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the socket pool (services/net/sockpool): the LRU acquire (free
// slot, else recycle the least-recently-used), the id->slot lookup, and touch. Pure fixed-size
// bookkeeping; no real sockets.
//
// Build/flash:  pio run -d performance_benching/device/sockpool -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/sockpool/sockpool.h"
#include <Arduino.h>

#define POOL_N 8

static void sockpool_bench_task(void *)
{
    static SockSlot slots[POOL_N];
    static SockPool pool;

    for (;;)
    {
        Serial.printf("DB ==== sockpool device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        pc_sockpool_init(&pool, slots, POOL_N);
        uint32_t id = 1, now = 0;
        // A churn of acquires: fills, then steadily recycles the LRU slot.
        DBENCH_OP("pc_sockpool_acquire (LRU)", 200000, {
            size_t idx = 0;
            uint32_t evicted = 0;
            sink += (uint32_t)pc_sockpool_acquire(&pool, id++, now++, &idx, &evicted);
        });
        size_t fidx = 0;
        DBENCH_OP("pc_sockpool_find", 200000, sink += pc_sockpool_find(&pool, id - 4, &fidx));
        DBENCH_OP("pc_sockpool_touch", 200000, {
            pc_sockpool_touch(&pool, 0, now++);
            sink += now;
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
    Serial.println("\nDB boot: sockpool device microbench");
    xTaskCreatePinnedToCore(sockpool_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
