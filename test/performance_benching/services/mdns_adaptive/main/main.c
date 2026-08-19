// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the adaptive mDNS beacon scheduler (network_drivers/application/mdns_adaptive):
// the pure scheduling decisions that pick *when* to re-announce. Benched here -
//   - protocore_mdns_refresh_interval()      : the TTL/2 continuous-refresher cadence (overflow-guarded math),
//   - protocore_mdns_beacon_adapt()          : the RF-contention backoff/recovery step (double on crowded air,
//                                        halve back toward base when quiet, clamped to [base, ceiling]),
//   - protocore_mdns_beacon_due()            : the wrap-safe announce-due check run every tick,
//   - protocore_mdns_beacon_presleep_due()   : the auto-sleep beacon (announce now if the record would lapse
//                                        while the radio is off), and
//   - protocore_mdns_contention_sample()     : the frame-counter -> per-window contention value (wrap-safe
//                                        delta, saturated to uint16), also run every tick.
// All of the above are pure, wrap-safe integer math with no heap and no stdlib, so every call exercises
// the real production code path. Deliberately OUT OF SCOPE: the device binding (protocore_mdns_adaptive_begin/
// tick/end) - it drives promiscuous 802.11 capture and re-applies mDNS TXT records over the radio, needs
// PROTOCORE_ENABLE_MDNS + PROTOCORE_ENABLE_PROMISC, and this rig has no associated station to sniff, so it is never
// compiled or benched (contrast performance_benching/device/modbus, a pure codec fully in scope).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/mdns_adaptive -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a capture
// opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/application/mdns_adaptive/mdns_adaptive.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Beacon armed with the same realistic values the host test uses: 120 s TTL -> 60 s base cadence,
    // an 8 min backoff ceiling, back off at >= 5 contenders/window.
    MdnsBeacon b;
    MdnsAdaptive.beacon_init_args.b = &b;
    MdnsAdaptive.beacon_init_args.base_ms = 60000;
    MdnsAdaptive.beacon_init_args.max_ms = 480000;
    MdnsAdaptive.beacon_init_args.hi_thresh = 5;
    MdnsAdaptive.beacon_init(protocore_mdns_adaptive_span());
    // A contention-sampling window and its scratch out-param.
    MdnsContentionWindow w;
    uint16_t c = 0;

    uint8_t *work = protocore_mdns_adaptive_span();
    if (work == NULL)
    {
        return; // the pool was short of this module's borrow
    }

    for (;;)
    {
        DBENCH_BANNER("mdns_adaptive");
        volatile uint32_t sink32 = 0;
        volatile uint32_t sinkb = 0;
        volatile uint16_t sink16 = 0;

        // The operands do not vary across iterations, so each is staged once above its macro;
        // only the call is inside the timed loop.
        MdnsAdaptive.refresh_interval_args.ttl_s = 120;
        // TTL/2 refresher cadence: the *1000/2 math with its 64-bit overflow guard.
        DBENCH_OP("MdnsAdaptive.refresh_interval", 200000,
                  sink32 += (MdnsAdaptive.refresh_interval(work), MdnsAdaptive.ms));

        MdnsAdaptive.beacon_adapt_args.b = &b;
        MdnsAdaptive.beacon_adapt_args.contention = 8;
        // RF-contention backoff/recovery step (mutates cur_ms in place; settles at the ceiling clamp).
        DBENCH_OP("MdnsAdaptive.beacon_adapt", 200000,
                  sink32 += (MdnsAdaptive.beacon_adapt(work), MdnsAdaptive.ms));

        MdnsAdaptive.beacon_due_args.b = &b;
        MdnsAdaptive.beacon_due_args.last_ms = 1000;
        MdnsAdaptive.beacon_due_args.now_ms = 1000 + 60000;
        // Wrap-safe announce-due check (evaluated every tick in production).
        DBENCH_OP("MdnsAdaptive.beacon_due", 200000,
                  sinkb += (MdnsAdaptive.beacon_due(work), MdnsAdaptive.ok));

        MdnsAdaptive.beacon_presleep_due_args.b = &b;
        MdnsAdaptive.beacon_presleep_due_args.last_ms = 0;
        MdnsAdaptive.beacon_presleep_due_args.now_ms = 10000;
        MdnsAdaptive.beacon_presleep_due_args.sleep_ms = 60000;
        // Auto-sleep beacon: would the record lapse mid-sleep? (64-bit sum, no overflow).
        DBENCH_OP("MdnsAdaptive.beacon_presleep_due", 200000,
                  sinkb += (MdnsAdaptive.beacon_presleep_due(work), MdnsAdaptive.ok));

        MdnsAdaptive.contention_init_args.w = &w;
        MdnsAdaptive.contention_init_args.window_ms = 1000;
        MdnsAdaptive.contention_init_args.frames_now = 0;
        MdnsAdaptive.contention_init_args.now_ms = 0;
        MdnsAdaptive.contention_sample_args.w = &w;
        MdnsAdaptive.contention_sample_args.frames_now = 500;
        MdnsAdaptive.contention_sample_args.now_ms = 100000;
        MdnsAdaptive.contention_sample_args.out = &c;
        // Frame-counter -> per-window contention (re-arm the window each call so the full wrap-safe
        // delta + uint16 saturation path is measured, not the every-tick "window not up yet" early-out).
        DBENCH_OP("MdnsAdaptive.contention_sample", 200000,
                  sink16 += (MdnsAdaptive.contention_init(work), MdnsAdaptive.contention_sample(work),
                             MdnsAdaptive.ok));

        (void)sink32;
        (void)sinkb;
        (void)sink16;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mdns_adaptive")
