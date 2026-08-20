// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the AD9238 SPI configuration-port codec (server/peripherals/ad9238):
// building the 16-bit instruction word (R/W + 2-bit byte-count + 13-bit address, MSB first), the
// single-register write and read transaction framing, and the "device update" shadow-register
// transfer transaction - all pure (no SPI clocking, no real silicon). Worked example for
// performance_benching/device/<service>/ pure protocol codecs (contrast with performance_benching/device/ads1115, a
// peripheral driver where the bus transaction itself is stubbed): this rig has no AD9238 board attached, and the sample
// data path (parallel CMOS/LVDS bus) is out of scope everywhere - only the deterministic CPU-side SPI
// configuration-port codec is ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ad9238 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/ad9238/ad9238.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static uint8_t out3[3];
    static uint8_t out2[2];
    static uint8_t ad9238_work[16]; // the borrow an entry takes; Ad9238 never reads it

    for (;;)
    {
        DBENCH_BANNER("ad9238");
        volatile bool sinkb = false;
        volatile size_t sinkz = 0;

        // Each entry call stays inside DBENCH_OP so the timed loop measures the encode, not the
        // read that follows it. The args do not change across iterations, so they are staged once.
        Ad9238.build_instruction_args.read = PROTO_FALSE;
        Ad9238.build_instruction_args.reg_addr = (uint16_t)AD9238_REG_POWER_DOWN;
        Ad9238.build_instruction_args.nbytes = 1;
        Ad9238.build_instruction_args.out2 = out2;
        DBENCH_OP("Ad9238.build_instruction", 200000, (Ad9238.build_instruction(ad9238_work), sinkb = Ad9238.ok));

        Ad9238.build_write_args.reg_addr = (uint16_t)AD9238_REG_POWER_DOWN;
        Ad9238.build_write_args.value = 0x01;
        Ad9238.build_write_args.out = out3;
        Ad9238.build_write_args.cap = sizeof(out3);
        DBENCH_OP("Ad9238.build_write", 200000, (Ad9238.build_write(ad9238_work), sinkz += Ad9238.n));

        Ad9238.build_read_args.reg_addr = (uint16_t)AD9238_REG_CHIP_ID;
        Ad9238.build_read_args.out = out2;
        Ad9238.build_read_args.cap = sizeof(out2);
        DBENCH_OP("Ad9238.build_read", 200000, (Ad9238.build_read(ad9238_work), sinkz += Ad9238.n));

        Ad9238.build_transfer_args.out = out3;
        Ad9238.build_transfer_args.cap = sizeof(out3);
        DBENCH_OP("Ad9238.build_transfer", 200000, (Ad9238.build_transfer(ad9238_work), sinkz += Ad9238.n));

        (void)sinkb;
        (void)sinkz;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ad9238")
