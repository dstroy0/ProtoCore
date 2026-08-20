// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the network-adaptation decision core (server/net/netadapt):
// Netadapt.window sizes the TCP receive window / RX buffer from the free heap (reserve +
// quarter-of-spare, clamped to [min,max]) and Netadapt.dhcp_fallback decides when to stop
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
    static uint8_t netadapt_work[16]; // the borrow an entry takes; Netadapt never reads it

    for (;;)
    {
        DBENCH_BANNER("netadapt");
        volatile uint32_t sink32 = 0;
        volatile uint32_t sinkb = 0;

        // The entry call stays inside DBENCH_OP so the timed loop measures the decision, not the
        // read that follows it. The args are staged once: they do not change across iterations.
        NetadaptV.window_args.reserve = 8000;
        NetadaptV.window_args.min_win = 1024;
        NetadaptV.window_args.max_win = 16384;

        // TCP window sizing: scaling case (free=40000, reserve=8000 -> 32000/4 = 8000, in-band),
        // taken straight from test/test_netadapt (test_window_scales_with_heap).
        NetadaptV.window_args.free_heap = 40000;
        DBENCH_OP("Netadapt.window scale", 200000, (Netadapt.window(netadapt_work), sink32 += NetadaptV.u32));
        // Ceiling-clamp case (huge heap -> clamped to max_win); exercises the upper clamp branch.
        NetadaptV.window_args.free_heap = 200000;
        DBENCH_OP("Netadapt.window clamp", 200000, (Netadapt.window(netadapt_work), sink32 += NetadaptV.u32));
        // Low-heap floor case (heap <= reserve -> min_win); exercises the early-return branch.
        NetadaptV.window_args.free_heap = 5000;
        DBENCH_OP("Netadapt.window floor", 200000, (Netadapt.window(netadapt_work), sink32 += NetadaptV.u32));

        NetadaptV.dhcp_fallback_args.timeout_ms = 10000;
        NetadaptV.dhcp_fallback_args.max_attempts = 5;

        // DHCP->static fallback: within budget (both triggers false - the full-check path).
        NetadaptV.dhcp_fallback_args.elapsed_ms = 9000;
        NetadaptV.dhcp_fallback_args.attempts = 1;
        DBENCH_OP("Netadapt.dhcp_fallback wait", 200000,
                  (Netadapt.dhcp_fallback(netadapt_work), sinkb += NetadaptV.ok ? 1u : 0u));
        // Fallback fires on the attempt budget.
        NetadaptV.dhcp_fallback_args.elapsed_ms = 1000;
        NetadaptV.dhcp_fallback_args.attempts = 5;
        DBENCH_OP("Netadapt.dhcp_fallback trip", 200000,
                  (Netadapt.dhcp_fallback(netadapt_work), sinkb += NetadaptV.ok ? 1u : 0u));

        (void)sink32;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("netadapt")
