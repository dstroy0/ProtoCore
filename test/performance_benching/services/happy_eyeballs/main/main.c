// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the dual-stack Happy Eyeballs selection layer
// (network_drivers/transport/happy_eyeballs): the RFC 6724 destination-preference score (protocore_he_pref), the
// candidate list sort + RFC 8305 address-family interleave (protocore_he_order), and the RFC 8305 Connection Attempt
// Delay gate (protocore_he_attempt_due). This is the pure decision layer over the shipped protocore_ip value type -
// no sockets, no DNS, no heap - so, like performance_benching/device/modbus, every call here runs the real production
// code path. There is nothing to stub: the app owns the sockets and DNS, this only decides *which*
// address to try next and *when*, so no transport half exists to touch. The candidate addresses are the
// exact spec-conformant fixtures from test/test_happy_eyeballs. protocore_he_order mutates its list in place,
// so each timed iteration first memcpy-restores the scrambled (v4-first) starting order - this makes the
// sort+interleave do real work every time instead of hitting the already-sorted fast path.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/happy_eyeballs -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/transport/happy_eyeballs/happy_eyeballs.h"
#include "shared/ip/ip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

void dbench_run(void)
{
    // Candidate fixtures copied verbatim from test/test_happy_eyeballs.cpp (known-good, spec-conformant).
    protocore_ip g6;
    Ip.args.text = "2606:4700::1";
    Ip.args.out = &g6;
    Ip.parse(ip_work);                                               // global IPv6
    protocore_ip g4 = protocore_ip_from_v4_octets(93, 184, 216, 34); // global IPv4

    // 3-address mixed template, v4-first: the sort must move the two v6 ahead, then interleave alternates.
    protocore_ip tmpl3[3];
    tmpl3[0] = protocore_ip_from_v4_octets(93, 184, 216, 34);
    Ip.args.text = "2606:4700::1";
    Ip.args.out = &tmpl3[1];
    Ip.parse(ip_work);
    Ip.args.text = "2606:4700::2";
    Ip.args.out = &tmpl3[2];
    Ip.parse(ip_work);

    // 5-address mixed template (three v6 + two v4) to exercise the longer interleave path.
    protocore_ip tmpl5[5];
    Ip.args.text = "2606:4700::1";
    Ip.args.out = &tmpl5[0];
    Ip.parse(ip_work);
    tmpl5[1] = protocore_ip_from_v4_octets(8, 8, 8, 8);
    Ip.args.text = "2606:4700::2";
    Ip.args.out = &tmpl5[2];
    Ip.parse(ip_work);
    tmpl5[3] = protocore_ip_from_v4_octets(1, 1, 1, 1);
    Ip.args.text = "2606:4700::3";
    Ip.args.out = &tmpl5[4];
    Ip.parse(ip_work);

    protocore_ip work3[3];
    protocore_ip work5[5];

    // The bytes this module's one operand rides in; every entry takes them.
    uint8_t *he = protocore_happy_eyeballs_span();

    for (;;)
    {
        DBENCH_BANNER("happy_eyeballs");
        volatile int sinki = 0;
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;

        // RFC 6724 preference score - the innermost comparator, called O(n^2) times by the sort.
        // Staged inside the argument: DBENCH_OP re-evaluates it, so the operands belong in the
        // timed expression rather than above it.
        DBENCH_OP("HappyEyeballs.pref v6", 200000,
                  (HappyEyeballsV.pref_args.ip = &g6, HappyEyeballs.pref(he), sinki += HappyEyeballsV.n));
        DBENCH_OP("HappyEyeballs.pref v4", 200000,
                  (HappyEyeballsV.pref_args.ip = &g4, HappyEyeballs.pref(he), sinki += HappyEyeballsV.n));
        // Full candidate ordering: stable insertion-sort by preference + RFC 8305 family interleave.
        // Restore the scrambled order first so every iteration times the real reorder, not the fast path.
        DBENCH_OP("HappyEyeballs.order x3 (sort+ilv)", 50000,
                  (memcpy(work3, tmpl3, sizeof(tmpl3)), HappyEyeballsV.order_args.list = work3,
                   HappyEyeballsV.order_args.n = 3, HappyEyeballs.order(he), sink8 += work3[1].bytes[0]));
        DBENCH_OP("HappyEyeballs.order x5 (sort+ilv)", 50000,
                  (memcpy(work5, tmpl5, sizeof(tmpl5)), HappyEyeballsV.order_args.list = work5,
                   HappyEyeballsV.order_args.n = 5, HappyEyeballs.order(he), sink8 += work5[1].bytes[0]));
        // Connection Attempt Delay gate (wrap-safe modular compare).
        DBENCH_OP("HappyEyeballs.attempt_due", 200000,
                  (HappyEyeballsV.attempt_due_args.last_start_ms = 1000,
                   HappyEyeballsV.attempt_due_args.now_ms = 1000 + 250,
                   HappyEyeballsV.attempt_due_args.attempt_delay_ms = PROTOCORE_HE_ATTEMPT_DELAY_MS,
                   HappyEyeballs.attempt_due(he), sinkb ^= HappyEyeballsV.ok));

        (void)sinki;
        (void)sink8;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("happy_eyeballs")
