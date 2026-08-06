// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared on-device CCOUNT microbench macros for performance_benching/device/<service>/main.cpp. Ported from
// penetration_testing/rig_firmware/src/main_cryptobench.cpp's BENCH_OP/BENCH_BULK onto pc_cycles() /
// pc_cycles_to_ns() (server/clock/clock.h) - the library's own documented wrapper around the Xtensa
// cycle counter (CCOUNT, JTAG-observable), rather than calling ESP.getCycleCount() directly. Every
// performance_benching/device/<service>/main.cpp includes this and prints one "DB ..." line per benched operation
// over USB-CDC serial; a capture opened at any time still catches a full run because each sketch
// loops the timing block (same pattern as main_cryptobench.cpp).

#ifndef PROTOCORE_PERF_DEVICE_BENCH_H
#define PROTOCORE_PERF_DEVICE_BENCH_H

#include "server/clock/clock.h"
#include <Arduino.h>

/// One-shot op (build/encode/decode/parse call that isn't a bulk byte stream). Warms once, runs
/// N iterations, reports the mean in cycles / us / ns.
#define DBENCH_OP(label, N, expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm */                                                                                               \
        uint32_t _c0 = pc_cycles();                                                                                    \
        for (uint32_t _i = 0; _i < (uint32_t)(N); _i++)                                                                \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        uint32_t _dc = pc_cycles() - _c0;                                                                              \
        double _cy = (double)_dc / (double)(N);                                                                        \
        uint32_t _mhz = getCpuFrequencyMhz();                                                                          \
        Serial.printf("DB %-30s cyc=%-11.0f us=%-9.2f ns=%.0f\n", label, _cy, _cy / (double)_mhz,                      \
                      (_cy * 1000.0) / (double)_mhz);                                                                  \
        vTaskDelay(1);                                                                                                 \
    } while (0)

/// Bulk op over `bytes` (encode/decode/checksum/pack of a byte buffer). Reports cyc/op, ns/byte, MB/s.
#define DBENCH_BULK(label, N, bytes, expr)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm */                                                                                               \
        uint32_t _c0 = pc_cycles();                                                                                    \
        for (uint32_t _i = 0; _i < (uint32_t)(N); _i++)                                                                \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        uint32_t _dc = pc_cycles() - _c0;                                                                              \
        double _cy = (double)_dc / (double)(N);                                                                        \
        uint32_t _mhz = getCpuFrequencyMhz();                                                                          \
        double _nspb = ((_cy * 1000.0) / (double)_mhz) / (double)(bytes);                                              \
        double _mbs = (_nspb > 0.0) ? (1000.0 / _nspb) : 0.0;                                                          \
        Serial.printf("DB %-30s cyc=%-11.0f ns/B=%-8.2f MB/s=%-8.1f (%uB)\n", label, _cy, _nspb, _mbs,                 \
                      (unsigned)(bytes));                                                                              \
        vTaskDelay(1);                                                                                                 \
    } while (0)

#endif // PROTOCORE_PERF_DEVICE_BENCH_H
