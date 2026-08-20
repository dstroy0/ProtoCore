// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the VL53L0X codec (server/peripherals/vl53l0x): the range-register
// decode (hi/lo -> mm), the data-ready check, and the range-status decode/validity. Pure register
// math - the I2C transfer is real-hardware and out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/vl53l0x -t upload --upload-port COM7
#include "device_bench.h"
#include "server/peripherals/vl53l0x/vl53l0x.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // The bytes the module runs out of, taken once. Every entry below is called with it.
    uint8_t *w = protocore_vl53l0x_span();

    for (;;)
    {
        DBENCH_BANNER("vl53l0x");
        volatile uint32_t sink = 0;
        // The arguments are set outside the timed expression and the entry is called inside it, so
        // what is timed is one call and not the staging that precedes it.
        Vl53l0xV.range_mm_args.hi = 0x03;
        Vl53l0xV.range_mm_args.lo = 0xE8;
        DBENCH_OP("Vl53l0x.range_mm", 200000, (Vl53l0x.range_mm(w), sink += Vl53l0xV.mm));
        Vl53l0xV.data_ready_args.interrupt_status = 0x01;
        DBENCH_OP("Vl53l0x.data_ready", 200000, (Vl53l0x.data_ready(w), sink += Vl53l0xV.ok));
        Vl53l0xV.range_status_args.range_status_reg = 0x58;
        DBENCH_OP("Vl53l0x.range_status", 200000, (Vl53l0x.range_status(w), sink += Vl53l0xV.status));
        Vl53l0xV.range_valid_args.range_status_reg = 0x58;
        DBENCH_OP("Vl53l0x.range_valid", 200000, (Vl53l0x.range_valid(w), sink += Vl53l0xV.ok));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("vl53l0x")
