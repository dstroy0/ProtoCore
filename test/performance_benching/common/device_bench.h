// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared on-device CCOUNT microbench macros for performance_benching/<layer>/<feature>/main/main.c.
// Built on pc_cycles() / pc_cycles_to_ns() (server/clock/clock.h), the library's wrapper around the
// Xtensa cycle counter (CCOUNT, JTAG-observable). Every bench includes this and prints one "DB "
// line per benched operation over USB-Serial/JTAG; each bench loops its timing block.

#ifndef PROTOCORE_PERF_DEVICE_BENCH_H
#define PROTOCORE_PERF_DEVICE_BENCH_H

#include "server/clock/clock.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc.h"
#include <stdint.h>
#include <stdio.h>

/// Current CPU clock in MHz, read from the RTC clock config.
static inline uint32_t dbench_cpu_mhz(void)
{
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    return conf.freq_mhz;
}

/// Warm once, run N iterations, leave the mean cycle count in `out_cy` (a double lvalue).
/// The measurement; every reporting macro is a printf around it.
#define DBENCH_CYCLES(N, expr, out_cy)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm */                                                                                               \
        uint32_t _c0 = pc_cycles();                                                                                    \
        for (uint32_t _i = 0; _i < (uint32_t)(N); _i++)                                                                \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        (out_cy) = (double)(pc_cycles() - _c0) / (double)(N);                                                          \
    } while (0)

/// One-shot op (build/encode/decode/parse call that isn't a bulk byte stream). Warms once, runs
/// N iterations, reports the mean in cycles / us / ns.
#define DBENCH_OP(label, N, expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        double _cy = 0.0;                                                                                              \
        DBENCH_CYCLES(N, expr, _cy);                                                                                   \
        uint32_t _mhz = dbench_cpu_mhz();                                                                              \
        printf("DB %-30s cyc=%-11.0f us=%-9.2f ns=%.0f\n", label, _cy, _cy / (double)_mhz,                             \
               (_cy * 1000.0) / (double)_mhz);                                                                         \
        vTaskDelay(1);                                                                                                 \
    } while (0)

/// Bulk op over `bytes` (encode/decode/checksum/pack of a byte buffer). Reports cyc/op, ns/byte, MB/s.
#define DBENCH_BULK(label, N, bytes, expr)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        double _cy = 0.0;                                                                                              \
        DBENCH_CYCLES(N, expr, _cy);                                                                                   \
        uint32_t _mhz = dbench_cpu_mhz();                                                                              \
        double _nspb = ((_cy * 1000.0) / (double)_mhz) / (double)(bytes);                                              \
        double _mbs = (_nspb > 0.0) ? (1000.0 / _nspb) : 0.0;                                                          \
        printf("DB %-30s cyc=%-11.0f ns/B=%-8.2f MB/s=%-8.1f (%uB)\n", label, _cy, _nspb, _mbs, (unsigned)(bytes));    \
        vTaskDelay(1);                                                                                                 \
    } while (0)

#endif // PROTOCORE_PERF_DEVICE_BENCH_H
