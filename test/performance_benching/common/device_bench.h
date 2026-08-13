// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared on-device CCOUNT microbench macros for performance_benching/<layer>/<feature>/main/main.c.
// Built on protocore_cycles() / protocore_cycles_to_ns() (server/clock/clock.h), the library's wrapper around the
// Xtensa cycle counter (CCOUNT, JTAG-observable). Every bench includes this and prints one "DB "
// line per benched operation over USB-Serial/JTAG; each bench loops its timing block.

#ifndef PROTOCORE_PERF_DEVICE_BENCH_H
#define PROTOCORE_PERF_DEVICE_BENCH_H

#include "server/clock/clock.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>

/// The rig's pinned CPU clock, in MHz. A compile-time fact, not a runtime read: protocore_cycles_to_ns()
/// takes the frequency as a parameter, and the bench runs the part at one clock. Override with
/// -DDBENCH_CPU_MHZ=<n> to bench at another.
#ifndef DBENCH_CPU_MHZ
#define DBENCH_CPU_MHZ 240u
#endif

/// Warm once, run N iterations, leave the mean cycle count in `out_cy` (a double lvalue).
/// The measurement; every reporting macro is a printf around it.
#define DBENCH_CYCLES(N, expr, out_cy)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm */                                                                                               \
        uint32_t _c0 = protocore_cycles();                                                                             \
        for (uint32_t _i = 0; _i < (uint32_t)(N); _i++)                                                                \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        (out_cy) = (double)(protocore_cycles() - _c0) / (double)(N);                                                   \
    } while (0)

/// One-shot op (build/encode/decode/parse call that isn't a bulk byte stream). Warms once, runs
/// N iterations, reports the mean in cycles / us / ns.
#define DBENCH_OP(label, N, expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        double _cy = 0.0;                                                                                              \
        DBENCH_CYCLES(N, expr, _cy);                                                                                   \
        printf("DB %-30s cyc=%-11.0f us=%-9.2f ns=%.0f\n", label, _cy, _cy / (double)DBENCH_CPU_MHZ,                   \
               (_cy * 1000.0) / (double)DBENCH_CPU_MHZ);                                                               \
        vTaskDelay(1);                                                                                                 \
    } while (0)

/// Bulk op over `bytes` (encode/decode/checksum/pack of a byte buffer). Reports cyc/op, ns/byte, MB/s.
#define DBENCH_BULK(label, N, bytes, expr)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        double _cy = 0.0;                                                                                              \
        DBENCH_CYCLES(N, expr, _cy);                                                                                   \
        double _nspb = ((_cy * 1000.0) / (double)DBENCH_CPU_MHZ) / (double)(bytes);                                    \
        double _mbs = (_nspb > 0.0) ? (1000.0 / _nspb) : 0.0;                                                          \
        printf("DB %-30s cyc=%-11.0f ns/B=%-8.2f MB/s=%-8.1f (%uB)\n", label, _cy, _nspb, _mbs, (unsigned)(bytes));    \
        vTaskDelay(1);                                                                                                 \
    } while (0)

/// The bench: prepare the fixtures, then loop the timed ops. Runs on its own task and never returns.
/// Fixtures stay local to it, so a runtime initializer is as legal here as it was in the sketch.
void dbench_run(void);

/// Start-of-cycle line, naming the feature and the clock the cycle counts are taken against.
#define DBENCH_BANNER(label)                                                                                           \
    printf("DB ==== " label " device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)DBENCH_CPU_MHZ)

/// End-of-cycle marker and the gap before the next pass.
#define DBENCH_DONE()                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        printf("DB ==== DONE ====\n");                                                                                 \
        vTaskDelay(5000 / portTICK_PERIOD_MS);                                                                         \
    } while (0)

/// The entry every bench shares: a boot line, then dbench_run() pinned to core 1 at priority 24.
#define DBENCH_MAIN(label)                                                                                             \
    static void dbench_task(void *arg)                                                                                 \
    {                                                                                                                  \
        (void)arg;                                                                                                     \
        dbench_run();                                                                                                  \
    }                                                                                                                  \
    void app_main(void);                                                                                               \
    void app_main(void)                                                                                                \
    {                                                                                                                  \
        vTaskDelay(2500 / portTICK_PERIOD_MS);                                                                         \
        printf("\nDB boot: " label " device microbench\n");                                                            \
        xTaskCreatePinnedToCore(dbench_task, "dbench", 16384, NULL, 24, NULL, 1);                                      \
    }

#endif // PROTOCORE_PERF_DEVICE_BENCH_H
