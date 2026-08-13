// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Sigfox codec (services/radio/sigfox): protocore_sigfox_build_uplink()
// formats a payload as the AT$SF hex uplink frame. Pure; the UART link to the Sigfox modem is out
// of scope.
//
// Build/flash:  idf.py -C test/performance_benching/sigfox -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/sigfox/sigfox.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const uint8_t payload[12] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x21, 0x40, 0x30, 0x00, 0xAB, 0xCD};

    for (;;)
    {
        DBENCH_BANNER("sigfox");
        volatile size_t sink = 0;
        static char out[64];
        DBENCH_OP("protocore_sigfox_build_uplink (12B)", 200000,
                  sink += protocore_sigfox_build_uplink(payload, sizeof(payload), out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sigfox")
