// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the removable-storage hot-swap state machine
// (server/storage/hotswap): the ABSENT/READY/FAULTED safeties that fault a volume after a run of
// consecutive I/O errors, reset the run on any success, rate-limit remount probes, and serialize a
// one-line /health status. Everything benched here is the pure host-testable core - each core
// function takes an explicit `now` and touches no clock, and protocore_hotswap_json() reads only the
// statically-initialized owned singleton - so there is no filesystem, no SD/SPI bus, and no
// protocore_millis() call on any path. The device binding (protocore_hotswap_begin / _poll / _io) is
// deliberately out of scope: it exists only to drive an app's mount/unmount/card-detect callbacks
// against real removable media, which this rig has none of, and it does no CPU work of its own worth
// timing beyond the core transitions already measured below.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/hotswap -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/storage/hotswap/hotswap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Two cores parked in known, stable states so every timing block does the identical branch each
    // iteration (values copied straight from test/test_hotswap: threshold 3, 2000 ms probe interval).
    static HotswapCore ready;  // parked READY for the per-write io / redundant-probe hot paths
    static HotswapCore absent; // parked not-READY so due() exercises the wrap-safe unsigned delta
    static HotswapCore scratch;
    static char json[64];
    // The bytes the module runs out of, taken once. Every entry below is called with it.
    uint8_t *w = protocore_hotswap_span();

    for (;;)
    {
        // Re-establish the known states at the top of each run.
        Hotswap.core_init_args.c = &ready;
        Hotswap.core_init_args.fail_threshold = 3;
        Hotswap.core_init_args.probe_interval_ms = 2000;
        Hotswap.core_init_args.now = 100000;
        Hotswap.core_init(protocore_hotswap_span());
        Hotswap.core_probe_args.c = &ready;
        Hotswap.core_probe_args.present = true;
        Hotswap.core_probe_args.mounted = true;
        Hotswap.core_probe_args.now = 100000;
        Hotswap.core_probe(protocore_hotswap_span()); // -> READY
        Hotswap.core_init_args.c = &absent;
        Hotswap.core_init_args.fail_threshold = 3;
        Hotswap.core_init_args.probe_interval_ms = 2000;
        Hotswap.core_init_args.now = 100000;
        Hotswap.core_init(protocore_hotswap_span()); // stays ABSENT

        DBENCH_BANNER("hotswap");
        volatile uint32_t sink = 0;

        // The steady-state per-write outcome: a success while READY clears the failure run and keeps
        // the volume READY, so this is the exact branch a healthy filesystem write takes every time.
        // The arguments are set outside the timed expression and the entry is called inside it, so
        // what is timed is one call and not the staging that precedes it.
        Hotswap.core_io_args.c = &ready;
        Hotswap.core_io_args.ok = true;
        DBENCH_OP("Hotswap.core_io ok", 200000, (Hotswap.core_io(w), sink += Hotswap.ok ? 1u : 0u));

        // A redundant probe of an already-mounted volume: the full mount-decision work with no
        // transition, so it stays READY and repeats identically.
        Hotswap.core_probe_args.c = &ready;
        Hotswap.core_probe_args.present = true;
        Hotswap.core_probe_args.mounted = true;
        Hotswap.core_probe_args.now = 100000;
        DBENCH_OP("Hotswap.core_probe mount", 200000, (Hotswap.core_probe(w), sink += Hotswap.ok ? 1u : 0u));

        // The cheap per-loop "is a remount due?" check; a not-READY core reaches the wrap-safe
        // (now - last_probe_ms) >= interval delta rather than the READY early-out.
        Hotswap.core_due_args.c = &absent;
        Hotswap.core_due_args.now = 105000;
        DBENCH_OP("Hotswap.core_due", 200000, (Hotswap.core_due(w), sink += Hotswap.ok ? 1u : 0u));

        // Cold init of a fresh core (clamps the threshold, back-dates the first probe).
        Hotswap.core_init_args.c = &scratch;
        Hotswap.core_init_args.fail_threshold = 3;
        Hotswap.core_init_args.probe_interval_ms = 2000;
        Hotswap.core_init_args.now = 100000;
        DBENCH_OP("Hotswap.core_init", 200000, Hotswap.core_init(w));

        // The /health serializer: snprintf of `{"storage":..,"mounts":N,"faults":N}` from the owned
        // singleton - the heaviest pure op here, hence the smaller N.
        Hotswap.json_args.out = json;
        Hotswap.json_args.cap = sizeof(json);
        DBENCH_OP("Hotswap.json", 50000, (Hotswap.json(w), sink += Hotswap.n));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("hotswap")
