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
    // The bytes the module runs out of, taken once. Every entry below is called with it.
    uint8_t *w = protocore_mpr121_span();

    for (;;)
    {
        DBENCH_BANNER("mpr121");
        volatile uint16_t sink16 = 0;
        volatile uint32_t sinkb = 0;
        volatile size_t sinksz = 0;

        // The arguments are set outside the timed expression and the entry is called inside it, so
        // what is timed is one call and not the staging that precedes it.
        Mpr121V.touched_args.status_lo = status_lo;
        Mpr121V.touched_args.status_hi = status_hi;
        DBENCH_OP("Mpr121.touched decode", 200000, (Mpr121.touched(w), sink16 += Mpr121V.value));
        Mpr121V.is_touched_args.mask = (uint16_t)(status_lo | (status_hi << 8));
        Mpr121V.is_touched_args.e = 7;
        DBENCH_OP("Mpr121.is_touched e7", 200000, (Mpr121.is_touched(w), sinkb += Mpr121V.ok ? 1u : 0u));
        // Two entries in one timed expression, so this reads as the pair and not as either alone.
        Mpr121V.proximity_args.status_hi = status_hi;
        Mpr121V.overcurrent_args.status_hi = status_hi;
        DBENCH_OP(
            "Mpr121.proximity+overcurrent flags", 200000,
            (Mpr121.proximity(w), sinkb += Mpr121V.ok ? 1u : 0u, Mpr121.overcurrent(w), sinkb += Mpr121V.ok ? 2u : 0u));
        Mpr121V.word10_args.lsb = filt_lsb;
        Mpr121V.word10_args.msb = filt_msb;
        DBENCH_OP("Mpr121.word10 combine", 200000, (Mpr121.word10(w), sink16 += Mpr121V.value));
        Mpr121V.build_init_args.buf = initbuf;
        Mpr121V.build_init_args.cap = sizeof(initbuf);
        Mpr121V.build_init_args.n_electrodes = MPR121_ELECTRODES;
        Mpr121V.build_init_args.touch_thr = 12;
        Mpr121V.build_init_args.release_thr = 6;
        DBENCH_OP("Mpr121.build_init x12", 50000, (Mpr121.build_init(w), sinksz += Mpr121V.n));

        (void)sink16;
        (void)sinkb;
        (void)sinksz;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mpr121")
