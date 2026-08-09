// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared host microbench macros for performance_benching/<layer>/<feature>/host.c. Times an
// expression over N iterations against CLOCK_MONOTONIC and prints one table row per benched
// operation. The host number is a relative baseline on a desktop/RPi core; the device cost comes
// from the matching main/main.c CCOUNT bench (device_bench.h).

#ifndef PROTOCORE_PERF_HOST_BENCH_H
#define PROTOCORE_PERF_HOST_BENCH_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

/// Monotonic nanoseconds.
static inline double hbench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/// Run `expr` N times, leave the mean nanoseconds per iteration in `out_ns` (a double lvalue).
#define HBENCH_NS(iters, expr, out_ns)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        double _t0 = hbench_now_ns();                                                                                  \
        for (uint64_t _i = 0; _i < (uint64_t)(iters); _i++)                                                            \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        (out_ns) = (hbench_now_ns() - _t0) / (double)(iters);                                                          \
    } while (0)

/// Column headers for the result table.
static inline void hbench_header(void)
{
    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");
}

/// One result row; MB/s is 0 when the op has no byte count.
static inline void hbench_row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

#endif // PROTOCORE_PERF_HOST_BENCH_H
