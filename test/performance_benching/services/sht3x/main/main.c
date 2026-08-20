// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SHT3x codec (server/peripherals/sht3x): the CRC-8 (used to validate
// each I2C word), the raw->milli-degree / raw->milli-percent conversions, and the 6-byte response
// parser. Pure integer math - the I2C read is real-hardware and out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/sht3x -t upload --upload-port COM7
#include "device_bench.h"
#include "server/peripherals/sht3x/sht3x.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // A real SHT3x measurement response: T word + CRC, RH word + CRC.
    static const uint8_t resp[6] = {0x61, 0x3D, 0x42, 0x5C, 0xE7, 0x3E};

    for (;;)
    {
        DBENCH_BANNER("sht3x");
        volatile int32_t sink = 0;
        DBENCH_OP("protocore_sht3x_crc8 (2 bytes)", 200000, sink += protocore_sht3x_crc8(resp, 2));
        DBENCH_OP("protocore_sht3x_temp_mc", 200000, sink += protocore_sht3x_temp_mc(0x613D));
        DBENCH_OP("protocore_sht3x_rh_mpct", 200000, sink += protocore_sht3x_rh_mpct(0x425C));
        int32_t t, rh;
        Sht3xV.parse_args.resp = resp;
        Sht3xV.parse_args.temp_mc = &t;
        Sht3xV.parse_args.rh_mpct = &rh;
        DBENCH_OP("protocore_sht3x_parse (6B resp)", 200000, {
            Sht3x.parse(protocore_sht3x_span());
            sink += Sht3xV.ok ? t : 0;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sht3x")
