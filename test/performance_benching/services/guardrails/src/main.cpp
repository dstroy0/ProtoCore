// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the heap/stack guardrails core (services/security/guardrails): the two
// pure, host-tested primitives - pc_guardrail_eval() (compares a pc_health snapshot against the
// heap/frag/stack floors and returns a PC_BREACH_* bitmask) and pc_health_json() (serializes a
// snapshot to JSON). Both are deterministic, no hardware involved, so every call here exercises the
// real production code path.
//
// Deliberately out of scope: pc_guardrails_sample()/pc_guardrails_check(), the live sampler that
// reads esp_get_free_heap_size / heap_caps_get_largest_free_block / uxTaskGetStackHighWaterMark.
// That half depends on live device/RTOS state (not pure, not deterministic) and is the guardrails
// analogue of the stubbed-out I2C bus in performance_benching/device/ads1115 - benching it would time the heap
// allocator and scheduler, not the guardrail logic, so it is skipped everywhere.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/guardrails -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/guardrails/guardrails.h"
#include <Arduino.h>

static void guardrails_bench_task(void *)
{
    // Known-good, spec-conformant snapshots + floors lifted straight from test/test_guardrails.
    static const pc_health clear = {20000, 15000, 10000, 2048}; // all above the floors -> PC_NONE
    static const pc_health breach = {100, 50, 100, 100};        // all below the floors -> HEAP|FRAG|STACK
    static const uint32_t heap_min = 8192, frag_min = 4096, stack_min = 512;
    static char json[128];

    for (;;)
    {
        Serial.printf("DB ==== guardrails device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint8_t sink8 = 0;
        volatile int sinki = 0;

        // Threshold evaluator - a handful of unsigned compares + bit-ORs; cheap, so large N.
        DBENCH_OP("pc_guardrail_eval all-clear", 200000,
                  sink8 += pc_guardrail_eval(&clear, heap_min, frag_min, stack_min));
        DBENCH_OP("pc_guardrail_eval all-breach", 200000,
                  sink8 += pc_guardrail_eval(&breach, heap_min, frag_min, stack_min));

        // JSON serializer - one snprintf of four uint32s; still cheap, moderate N.
        DBENCH_OP("pc_health_json", 50000, sinki += pc_health_json(&clear, json, sizeof(json)));

        (void)sink8;
        (void)sinki;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: guardrails device microbench");
    xTaskCreatePinnedToCore(guardrails_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
