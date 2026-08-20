// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the heap/stack guardrails core (server/core/guardrails): the two
// pure, host-tested primitives - protocore_guardrail_eval() (compares a protocore_health snapshot against the
// heap/frag/stack floors and returns a PROTOCORE_BREACH_* bitmask) and protocore_health_json() (serializes a
// snapshot to JSON). Both are deterministic, no hardware involved, so every call here exercises the
// real production code path.
//
// Deliberately out of scope: protocore_guardrails_sample()/protocore_guardrails_check(), the live sampler that
// reads esp_get_free_heap_size / heap_caps_get_largest_free_block / uxTaskGetStackHighWaterMark.
// That half depends on live device/RTOS state (not pure, not deterministic) and is the guardrails
// analogue of the stubbed-out I2C bus in performance_benching/device/ads1115 - benching it would time the heap
// allocator and scheduler, not the guardrail logic, so it is skipped everywhere.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/guardrails -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/core/guardrails/guardrails.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Known-good, spec-conformant snapshots + floors lifted straight from test/test_guardrails.
    static const protocore_health clear = {20000, 15000, 10000, 2048}; // all above the floors -> PROTOCORE_NONE
    static const protocore_health breach = {100, 50, 100, 100};        // all below the floors -> HEAP|FRAG|STACK
    static const uint32_t heap_min = 8192, frag_min = 4096, stack_min = 512;
    static char json[128];

    for (;;)
    {
        DBENCH_BANNER("guardrails");
        volatile uint8_t sink8 = 0;
        volatile int sinki = 0;

        Guardrails.floors.heap_min = heap_min;
        Guardrails.floors.frag_min_block = frag_min;
        Guardrails.floors.stack_min = stack_min;

        // Threshold evaluator - a handful of unsigned compares + bit-ORs; cheap, so large N.
        Guardrails.health = &clear;
        DBENCH_OP("Guardrails.eval all-clear", 200000, Guardrails.eval(protocore_guardrails_span());
                  sink8 += Guardrails.breaches);

        Guardrails.health = &breach;
        DBENCH_OP("Guardrails.eval all-breach", 200000, Guardrails.eval(protocore_guardrails_span());
                  sink8 += Guardrails.breaches);

        // JSON serializer - one snprintf of four uint32s; still cheap, moderate N.
        Guardrails.health = &clear;
        Guardrails.out.out = json;
        Guardrails.out.cap = sizeof(json);
        DBENCH_OP("Guardrails.json", 50000, Guardrails.json(protocore_guardrails_span()); sinki += Guardrails.n);

        (void)sink8;
        (void)sinki;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("guardrails")
