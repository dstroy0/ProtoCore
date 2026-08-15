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
    for (;;)
    {
        DBENCH_BANNER("vl53l0x");
        volatile uint32_t sink = 0;
        DBENCH_OP("protocore_vl53l0x_range_mm", 200000, sink += protocore_vl53l0x_range_mm(0x03, 0xE8));
        DBENCH_OP("protocore_vl53l0x_data_ready", 200000, sink += protocore_vl53l0x_data_ready(0x01));
        DBENCH_OP("protocore_vl53l0x_range_status", 200000, sink += protocore_vl53l0x_range_status(0x58));
        DBENCH_OP("protocore_vl53l0x_range_valid", 200000, sink += protocore_vl53l0x_range_valid(0x58));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("vl53l0x")
