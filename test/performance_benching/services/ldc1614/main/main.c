// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the TI LDC1614 inductance-to-digital codec
// (server/peripherals/ldc1614): combining a DATA MSB/LSB register pair into the 28-bit result
// (protocore_ldc1614_data), pulling the 4 error flags off the MSB register (protocore_ldc1614_error),
// scaling a 28-bit result to a sensor frequency in Hz (protocore_ldc1614_sensor_freq_hz, the
// data / 2^28 * fref math), and emitting the single-channel CH0 continuous-conversion
// bring-up as (reg, msb, lsb) triples (protocore_ldc1614_build_config). All four are pure - no
// I2C, no heap. This rig has no LDC1614 breakout attached, so the Wire binding
// (protocore_ldc1614_begin / protocore_ldc1614_read_ch0, the I2C-over-Wire half) is deliberately out of
// scope everywhere here; only the deterministic CPU-side codec is ever benched (same posture
// as performance_benching/device/ads1115, the peripheral-driver worked example).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ldc1614 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/ldc1614/ldc1614.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Known-good sample vectors lifted straight from test/test_ldc1614/test_ldc1614.cpp:
    // MSB reg 0xF123 (error nibble 0xF, data MSB 0x123) + LSB reg 0x4567 -> 28-bit 0x01234567.
    const uint16_t msb_reg = 0xF123;
    const uint16_t lsb_reg = 0x4567;
    // A quarter-scale result (1<<27) against a 40 MHz reference clock scales to 20 MHz.
    const uint32_t data28 = 1u << 27;
    const uint32_t fref_hz = 40000000u;
    static uint8_t cfg[LDC1614_CONFIG_MAX];
    // The bytes the module runs out of, taken once. Every entry below is called with it.
    uint8_t *w = protocore_ldc1614_span();

    for (;;)
    {
        DBENCH_BANNER("ldc1614");
        volatile uint32_t sink32 = 0;
        volatile uint8_t sink8 = 0;
        volatile uint64_t sink64 = 0;
        volatile size_t sinksz = 0;

        // The arguments are set outside the timed expression and the entry is called inside it, so
        // what is timed is one call and not the staging that precedes it.
        Ldc1614.data_args.msb_reg = msb_reg;
        Ldc1614.data_args.lsb_reg = lsb_reg;
        DBENCH_OP("Ldc1614.data (28b combine)", 200000, (Ldc1614.data(w), sink32 += Ldc1614.value));
        Ldc1614.error_args.msb_reg = msb_reg;
        DBENCH_OP("Ldc1614.error (flag nibble)", 200000, (Ldc1614.error(w), sink8 += Ldc1614.flags));
        Ldc1614.sensor_freq_hz_args.data28 = data28;
        Ldc1614.sensor_freq_hz_args.fref_hz = fref_hz;
        DBENCH_OP("Ldc1614.sensor_freq_hz", 200000, (Ldc1614.sensor_freq_hz(w), sink64 += Ldc1614.hz));
        // Config builder emits 21 bytes (7 register writes * 3 bytes) - benched as a bulk producer.
        Ldc1614.build_config_args.buf = cfg;
        Ldc1614.build_config_args.cap = sizeof(cfg);
        Ldc1614.build_config_args.rcount = 0xFFFF;
        Ldc1614.build_config_args.settlecount = 0x0400;
        DBENCH_BULK("Ldc1614.build_config", 100000, LDC1614_CONFIG_MAX,
                    (Ldc1614.build_config(w), sinksz += Ldc1614.n));

        (void)sink32;
        (void)sink8;
        (void)sink64;
        (void)sinksz;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ldc1614")
