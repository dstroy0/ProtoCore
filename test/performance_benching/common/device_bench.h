// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared CCOUNT microbench macros for performance_benching/<layer>/<feature>/main/main.c.
// Built on Clock.cycles / protocore_cycles_to_ns (server/clock/clock.h), the library's wrapper around
// the cycle counter the platform supplies. Every bench includes this and prints one "DB " line per
// benched operation; each bench loops its timing block.
//
// Two arms. On silicon the counter is CCOUNT (JTAG-observable), the bench runs on its own FreeRTOS
// task and never returns, and the lines go out over USB-Serial/JTAG. On host the counter is the
// stand-in in core_setup/hal/host, the bench is a plain main(), and it runs one pass and exits so a
// runner can collect it.

#ifndef PROTOCORE_PERF_DEVICE_BENCH_H
#define PROTOCORE_PERF_DEVICE_BENCH_H

#include "server/clock/clock.h"

#include <stdint.h>
#include <stdio.h>

#if PROTOCORE_VENDOR_SILICON
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <stdlib.h> // exit: the host arm runs one pass, so the timing loop has to end
#endif

/// The rig's pinned CPU clock, in MHz. A compile-time fact, not a runtime read: protocore_cycles_to_ns()
/// takes the frequency as a parameter, and the bench runs the part at one clock. Override with
/// -DDBENCH_CPU_MHZ=<n> to bench at another.
#ifndef DBENCH_CPU_MHZ
#define DBENCH_CPU_MHZ 240u
#endif

/// One cycle-counter read. Clock.cycles writes the count to Clock.cyc.
#define DBENCH_CYCLE_READ(dst)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        Clock.cycles(Clock.internal);                                                                                  \
        (dst) = Clock.cyc;                                                                                             \
    } while (0)

/// The gap between reported lines: one RTOS tick on silicon, nothing on host.
#if PROTOCORE_VENDOR_SILICON
#define DBENCH_SETTLE() vTaskDelay(1)
#else
#define DBENCH_SETTLE() ((void)0)
#endif

/// Warm once, run N iterations, leave the mean cycle count in `out_cy` (a double lvalue).
/// The measurement; every reporting macro is a printf around it.
#define DBENCH_CYCLES(N, expr, out_cy)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm */                                                                                               \
        uint32_t _c0, _c1;                                                                                             \
        DBENCH_CYCLE_READ(_c0);                                                                                        \
        for (uint32_t _i = 0; _i < (uint32_t)(N); _i++)                                                                \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        DBENCH_CYCLE_READ(_c1);                                                                                        \
        (out_cy) = (double)(_c1 - _c0) / (double)(N);                                                                  \
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
        DBENCH_SETTLE();                                                                                               \
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
        DBENCH_SETTLE();                                                                                               \
    } while (0)

/// The bench: prepare the fixtures, then loop the timed ops. Fixtures stay local to it, so a
/// runtime initializer is as legal here as it was in the sketch.
void dbench_run(void);

/// Start-of-cycle line, naming the feature and the clock the cycle counts are taken against.
#define DBENCH_BANNER(label)                                                                                           \
    printf("DB ==== " label " device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)DBENCH_CPU_MHZ)

/// End-of-cycle marker. On silicon it pauses and the caller's loop starts the next pass; on host it
/// ends the process, so the one pass a runner asked for is the one it gets.
#if PROTOCORE_VENDOR_SILICON
#define DBENCH_DONE()                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        printf("DB ==== DONE ====\n");                                                                                 \
        vTaskDelay(5000 / portTICK_PERIOD_MS);                                                                         \
    } while (0)
#else
#define DBENCH_DONE()                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        printf("DB ==== DONE ====\n");                                                                                 \
        fflush(stdout);                                                                                                \
        exit(0);                                                                                                       \
    } while (0)
#endif

/// The entry every bench shares: a boot line, then dbench_run() - pinned to core 1 at priority 24
/// on silicon, called directly on host.
#if PROTOCORE_VENDOR_SILICON
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
#else
#define DBENCH_MAIN(label)                                                                                             \
    int main(void)                                                                                                     \
    {                                                                                                                  \
        printf("\nDB boot: " label " host microbench\n");                                                              \
        dbench_run();                                                                                                  \
        return 0;                                                                                                      \
    }
#endif

#endif // PROTOCORE_PERF_DEVICE_BENCH_H
