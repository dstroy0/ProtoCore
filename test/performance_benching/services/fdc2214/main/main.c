// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the FDC2114/2214 capacitance-to-digital codec
// (services/peripherals/fdc2214): combining a channel's DATA MSB/LSB register pair into the 28-bit result,
// pulling the 4 error flags out of the MSB register, scaling a 28-bit result to a sensor
// frequency (data/2^28 * fref, a 64-bit multiply+shift), and building the single-channel (CH0)
// continuous-conversion config sequence - all pure, no I2C. Worked example for
// performance_benching/device/<service>/ peripheral drivers: this rig has no FDC2214 breakout attached, so
// protocore_fdc2214_begin/read_ch0 (the I2C-over-Wire half, gated on `#if defined(ARDUINO)` inside
// fdc2214.cpp) are out of scope everywhere - only the deterministic CPU-side codec is ever
// benched, and since we never call those two functions, no hardware stub is needed to satisfy
// the linker (contrast with services that reference an external stub function directly).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/fdc2214 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines.
#include "device_bench.h"
#include "services/peripherals/fdc2214/fdc2214.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Register pair from test_fdc2214.cpp: error flags 0x3 in the MSB register's top nibble,
    // data MSB 0xABC, LSB register 0x1234 -> 28-bit result 0x0ABC1234.
    static const uint16_t msb_reg = 0x3ABC;
    static const uint16_t lsb_reg = 0x1234;
    // Half-scale data at a 40 MHz reference -> 20 MHz sensor frequency.
    static const uint32_t data28 = 1u << 27;
    static const uint32_t fref_hz = 40000000u;
    // CH0 bring-up parameters from test_fdc2214.cpp.
    static const uint16_t rcount = 0xFFFF;
    static const uint16_t settlecount = 0x0400;
    static uint8_t cfg[FDC2214_CONFIG_MAX];

    for (;;)
    {
        DBENCH_BANNER("fdc2214");
        volatile uint32_t sink32 = 0;
        volatile uint8_t sink8 = 0;
        volatile uint64_t sink64 = 0;
        volatile size_t sinksz = 0;
        DBENCH_OP("protocore_fdc2214_data", 200000, sink32 += protocore_fdc2214_data(msb_reg, lsb_reg));
        DBENCH_OP("protocore_fdc2214_error", 200000, sink8 += protocore_fdc2214_error(msb_reg));
        DBENCH_OP("protocore_fdc2214_sensor_freq_hz", 200000, sink64 += protocore_fdc2214_sensor_freq_hz(data28, fref_hz));
        DBENCH_BULK("protocore_fdc2214_build_config", 50000, FDC2214_CONFIG_MAX,
                    sinksz += protocore_fdc2214_build_config(cfg, sizeof(cfg), rcount, settlecount));
        (void)sink32;
        (void)sink8;
        (void)sink64;
        (void)sinksz;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("fdc2214")
