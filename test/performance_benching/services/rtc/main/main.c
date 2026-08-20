// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the RTC codec (server/peripherals/rtc): the pure BCD-register <-> Unix
// epoch conversions protocore_rtc_regs_to_epoch() / protocore_rtc_epoch_to_regs() (24h/12h encodings, leap
// years). The I2C register read/write (protocore_rtc_begin/read_epoch/set_epoch) is real-hardware and out
// of scope here; only the deterministic conversion math is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/rtc -t upload --upload-port COM7
#include "device_bench.h"
#include "server/peripherals/rtc/rtc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // 2026-07-04 12:34:56, Sat: s,m,h,dow,date,month,year (BCD) - from test/test_rtc.
    static const uint8_t regs[RTC_REG_COUNT] = {0x56, 0x34, 0x12, 0x06, 0x04, 0x07, 0x26};

    for (;;)
    {
        DBENCH_BANNER("rtc");
        volatile uint32_t sink = 0;
        uint32_t epoch = 0;
        DBENCH_OP("protocore_rtc_regs_to_epoch", 200000, sink += protocore_rtc_regs_to_epoch(regs, &epoch));
        uint8_t out[RTC_REG_COUNT];
        DBENCH_OP("protocore_rtc_epoch_to_regs", 200000, {
            Rtc.epoch_to_regs_args.epoch = 1751632496u;
            Rtc.epoch_to_regs_args.regs = out;
            Rtc.epoch_to_regs(protocore_rtc_span());
            sink += out[0];
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("rtc")
