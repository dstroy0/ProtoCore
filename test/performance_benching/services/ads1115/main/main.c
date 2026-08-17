// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the ADS1115 ADC codec (server/peripherals/ads1115): building the
// 16-bit single-shot config word (channel/gain/data-rate) and converting a signed raw sample to
// microvolts - both pure, no I2C. Worked example for performance_benching/device/<service>/ peripheral drivers: this
// rig has no ADS1115 breakout attached, so protocore_ads1115_begin/read_raw/read_uv (the I2C-over-Wire
// half) are out of scope everywhere - only the deterministic CPU-side codec is ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ads1115 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines.
#include "device_bench.h"
#include "server/peripherals/ads1115/ads1115.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // The bytes the module runs out of, taken once. Every entry below is called with it.
    uint8_t *w = protocore_ads1115_span();

    for (;;)
    {
        DBENCH_BANNER("ads1115");
        volatile uint16_t sink16 = 0;
        volatile int32_t sink32 = 0;

        // The arguments are set outside the timed expression and the entry is called inside it, so
        // what is timed is one call and not the staging that precedes it.
        Ads1115.config_single_args.channel = 0;
        Ads1115.config_single_args.gain = ADS1115_GAIN_1;
        Ads1115.config_single_args.dr = ADS1115_DR_128;
        DBENCH_OP("Ads1115.config_single", 200000, (Ads1115.config_single(w), sink16 += Ads1115.word));
        Ads1115.raw_to_uv_args.raw = 16384;
        Ads1115.raw_to_uv_args.gain = ADS1115_GAIN_2;
        DBENCH_OP("Ads1115.raw_to_uv", 200000, (Ads1115.raw_to_uv(w), sink32 += Ads1115.uv));
        (void)sink16;
        (void)sink32;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ads1115")
