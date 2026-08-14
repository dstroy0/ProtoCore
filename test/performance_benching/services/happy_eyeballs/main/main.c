// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Candidate fixtures copied verbatim from test/test_happy_eyeballs.cpp (known-good, spec-conformant).
    protocore_ip g6;
    protocore_ip_parse("2606:4700::1", &g6);                         // global IPv6
    protocore_ip g4 = protocore_ip_from_v4_octets(93, 184, 216, 34); // global IPv4

    // 3-address mixed template, v4-first: the sort must move the two v6 ahead, then interleave alternates.
    protocore_ip tmpl3[3];
    tmpl3[0] = protocore_ip_from_v4_octets(93, 184, 216, 34);
    protocore_ip_parse("2606:4700::1", &tmpl3[1]);
    protocore_ip_parse("2606:4700::2", &tmpl3[2]);

    // 5-address mixed template (three v6 + two v4) to exercise the longer interleave path.
    protocore_ip tmpl5[5];
    protocore_ip_parse("2606:4700::1", &tmpl5[0]);
    tmpl5[1] = protocore_ip_from_v4_octets(8, 8, 8, 8);
    protocore_ip_parse("2606:4700::2", &tmpl5[2]);
    tmpl5[3] = protocore_ip_from_v4_octets(1, 1, 1, 1);
    protocore_ip_parse("2606:4700::3", &tmpl5[4]);

    protocore_ip work3[3];
    protocore_ip work5[5];

    for (;;)
    {
        DBENCH_BANNER("happy_eyeballs");
        volatile int sinki = 0;
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;

        // RFC 6724 preference score - the innermost comparator, called O(n^2) times by the sort.
        DBENCH_OP("protocore_he_pref v6", 200000, sinki += protocore_he_pref(&g6));
        DBENCH_OP("protocore_he_pref v4", 200000, sinki += protocore_he_pref(&g4));
        // Full candidate ordering: stable insertion-sort by preference + RFC 8305 family interleave.
        // Restore the scrambled order first so every iteration times the real reorder, not the fast path.
        DBENCH_OP("protocore_he_order x3 (sort+ilv)", 50000,
                  (memcpy(work3, tmpl3, sizeof(tmpl3)), protocore_he_order(work3, 3), sink8 += work3[1].bytes[0]));
        DBENCH_OP("protocore_he_order x5 (sort+ilv)", 50000,
                  (memcpy(work5, tmpl5, sizeof(tmpl5)), protocore_he_order(work5, 5), sink8 += work5[1].bytes[0]));
        // Connection Attempt Delay gate (wrap-safe modular compare).
        DBENCH_OP("protocore_he_attempt_due", 200000,
                  sinkb ^= protocore_he_attempt_due(1000, 1000 + 250, PROTOCORE_HE_ATTEMPT_DELAY_MS));

        (void)sinki;
        (void)sink8;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("happy_eyeballs")
