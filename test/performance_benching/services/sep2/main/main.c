// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEEE 2030.5 (SEP2) codec (services/energy/sep2): the XML
// resource builders - DeviceCapability, EndDevice, and a DERControl event. Pure string logic;
// no transport.
//
// Build/flash:  idf.py -C test/performance_benching/sep2 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/energy/sep2/sep2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("sep2");
        volatile size_t sink = 0;
        static char out[512];
        DBENCH_OP("protocore_sep2_device_capability", 200000,
                  sink += protocore_sep2_device_capability(300, "/edev", "/derp", out, sizeof(out)));
        DBENCH_OP("protocore_sep2_end_device", 200000,
                  sink += protocore_sep2_end_device(0x0123456789ABull, "3E4F...LFDI", "/edev/1", out, sizeof(out)));
        DBENCH_OP("protocore_sep2_der_control", 200000,
                  sink += protocore_sep2_der_control("D7A1B2C3", 1720700000u, 3600u, -1500, out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sep2")
