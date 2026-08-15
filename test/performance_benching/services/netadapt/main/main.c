// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the network-adaptation decision core (server/net/netadapt):
// protocore_netadapt_window() sizes the TCP receive window / RX buffer from the free heap (reserve +
// quarter-of-spare, clamped to [min,max]) and protocore_netadapt_dhcp_fallback() decides when to stop
// waiting on DHCP and switch to a static IP. Both are pure integer decisions - zero heap, no stdlib,
// no lwIP/netif touched - so like performance_benching/device/modbus (a pure codec) every call here exercises the
// real production path. There is no peripheral/transport half to stub: the app applies the results
// (setting the lwIP window / configuring the netif) and that side is deliberately out of scope.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/netadapt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/net/netadapt/netadapt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("netadapt");
        volatile uint32_t sink32 = 0;
        volatile uint32_t sinkb = 0;

        // TCP window sizing: scaling case (free=40000, reserve=8000 -> 32000/4 = 8000, in-band),
        // taken straight from test/test_netadapt (test_window_scales_with_heap).
        DBENCH_OP("protocore_netadapt_window scale", 200000, sink32 += protocore_netadapt_window(40000, 8000, 1024, 16384));
        // Ceiling-clamp case (huge heap -> clamped to max_win); exercises the upper clamp branch.
        DBENCH_OP("protocore_netadapt_window clamp", 200000, sink32 += protocore_netadapt_window(200000, 8000, 1024, 16384));
        // Low-heap floor case (heap <= reserve -> min_win); exercises the early-return branch.
        DBENCH_OP("protocore_netadapt_window floor", 200000, sink32 += protocore_netadapt_window(5000, 8000, 1024, 16384));

        // DHCP->static fallback: within budget (both triggers false - the full-check path).
        DBENCH_OP("protocore_netadapt_dhcp_fallback wait", 200000,
                  sinkb += protocore_netadapt_dhcp_fallback(9000, 1, 10000, 5) ? 1u : 0u);
        // Fallback fires on the attempt budget.
        DBENCH_OP("protocore_netadapt_dhcp_fallback trip", 200000,
                  sinkb += protocore_netadapt_dhcp_fallback(1000, 5, 10000, 5) ? 1u : 0u);

        (void)sink32;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("netadapt")
