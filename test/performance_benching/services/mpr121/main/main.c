// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NXP MPR121 capacitive-touch codec (server/peripherals/mpr121):
// decoding the 16-bit touch-status word into a 12-electrode bitmask (protocore_mpr121_touched), the
// per-electrode touched test (protocore_mpr121_is_touched), the proximity / over-current status flags,
// combining an LSB/MSB register pair into a 10-bit filtered/baseline value (protocore_mpr121_word10),
// and emitting the whole register bring-up sequence as (register, value) byte pairs
// (protocore_mpr121_build_init) - all pure, all host-tested. This rig has no MPR121 breakout wired to
// the I2C bus, so the Wire binding (protocore_mpr121_begin / read_touched / read_filtered) is out of
// scope everywhere - only the deterministic CPU-side codec is ever benched here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/mpr121 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/mpr121/mpr121.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Realistic status words (spec-conformant patterns lifted from test/test_mpr121): a low byte
    // touching electrodes 0/2/5/7, a high byte with electrodes 8/9 plus the proximity (bit 12)
    // and over-current (bit 15) flags set so the mask/flag paths all see live bits.
    const uint8_t status_lo = 0xA5;
    const uint8_t status_hi = 0x93;
    // A 10-bit filtered-capacitance register pair (LSB, MSB).
    const uint8_t filt_lsb = 0x2A;
    const uint8_t filt_msb = 0x01;
    static uint8_t initbuf[MPR121_INIT_MAX];

    for (;;)
    {
        DBENCH_BANNER("mpr121");
        volatile uint16_t sink16 = 0;
        volatile uint32_t sinkb = 0;
        volatile size_t sinksz = 0;

        DBENCH_OP("protocore_mpr121_touched decode", 200000, sink16 += protocore_mpr121_touched(status_lo, status_hi));
        DBENCH_OP("protocore_mpr121_is_touched e7", 200000,
                  sinkb += protocore_mpr121_is_touched((uint16_t)(status_lo | (status_hi << 8)), 7) ? 1u : 0u);
        DBENCH_OP("pc_mpr121_prox+ovcf flags", 200000,
                  sinkb += (protocore_mpr121_proximity(status_hi) ? 1u : 0u) +
                           (protocore_mpr121_overcurrent(status_hi) ? 2u : 0u));
        DBENCH_OP("protocore_mpr121_word10 combine", 200000, sink16 += protocore_mpr121_word10(filt_lsb, filt_msb));
        DBENCH_OP("protocore_mpr121_build_init x12", 50000,
                  sinksz += protocore_mpr121_build_init(initbuf, sizeof(initbuf), MPR121_ELECTRODES, 12, 6));

        (void)sink16;
        (void)sinkb;
        (void)sinksz;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mpr121")
