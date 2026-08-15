// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the multi-interface link-manager policy (server/signaling/link_manager):
// a caller-owned table of interfaces (kind + priority + up/down) with a deterministic "best link that
// is up" selection (protocore_link_select), initial-egress compute (protocore_link_init), and up/down state change
// with escalation/failover + change detection (protocore_link_set). All three are pure integer table scans -
// no heap, no stdlib, no PHY bring-up, no netif reconfigure (those belong to the app and the stack;
// this only decides which interface should be active). So every call here exercises the real
// production code path, exactly like the modbus pure-codec worked example - nothing is stubbed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/link_manager -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/signaling/link_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Bind the @p n interfaces at @p ifaces to @p m and seed its active egress. */
static void link_init(LinkManager *m, LinkIface *ifaces, size_t n)
{
    Link.args.m = m;
    Link.args.ifaces = ifaces;
    Link.args.n = n;
    Link.init(Link.internal);
}

/** @brief The index of the best interface that is up in @p m, or -1 when none is. */
static int link_select(const LinkManager *m)
{
    Link.args.m_ro = m;
    Link.select(Link.internal);
    return Link.i32;
}

/** @brief Set interface @p idx of @p m to @p up and rescan; whether the active egress moved. */
static proto_bool link_set(LinkManager *m, size_t idx, proto_bool up, int *from, int *to)
{
    Link.args.m = m;
    Link.args.idx = idx;
    Link.args.up = up;
    Link.set(Link.internal);
    *from = Link.from;
    *to = Link.to;
    return Link.changed;
}

void dbench_run(void)
{
    // Realistic 3-interface table straight out of test/test_link_manager: wired Eth (prio 20),
    // WiFi STA (prio 10), softAP (prio 5) - the priority order that drives escalate-to-Eth /
    // fail-over-to-WiFi.
    static LinkIface ifaces[3] = {
        {LINK_KIND_ETH, 20, true},
        {LINK_KIND_WIFI_STA, 10, true},
        {LINK_KIND_WIFI_AP, 5, true},
    };
    static LinkManager m;
    link_init(&m, ifaces, 3);

    for (;;)
    {
        DBENCH_BANNER("link_manager");
        volatile int sink = 0;
        volatile bool bsink = false;
        int from = 0, to = 0;

        // Best-link-up selection: full priority scan over the 3-interface table (all up).
        DBENCH_OP("Link.select all-up", 200000, sink += link_select(&m));

        // Initial-egress compute over caller storage: seeds active via one Link.select scan.
        DBENCH_OP("Link.init recompute", 200000, link_init(&m, ifaces, 3));

        // State change + recompute + change detection (idx 0 held up -> escalation path, full rescan).
        DBENCH_OP("Link.set escalate", 100000, bsink = link_set(&m, 0, PROTO_TRUE, &from, &to));

        // Fail-over path: drop the top-priority link, rescan picks the next best up.
        DBENCH_OP("Link.set failover", 100000, bsink = link_set(&m, 0, PROTO_FALSE, &from, &to));

        (void)sink;
        (void)bsink;
        (void)from;
        (void)to;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("link_manager")
