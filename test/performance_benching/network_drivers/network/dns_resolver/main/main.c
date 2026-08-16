// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DNS answer classifier/verifier (network_drivers/network/dns_resolver):
// protocore_dns_resolver_classify() buckets a host-order IPv4 word into an RFC special-purpose-range
// category, and protocore_dns_resolver_verify() uses it to reject spoof / DNS-rebinding indicators
// (unspecified / broadcast / loopback / multicast) - both pure, branch-heavy, no lwIP involved.
// Out of scope: protocore_dns_resolver_resolve() (and therefore protocore_dns_resolver_resolve_verified(),
// which calls it) - on ARDUINO builds that is the real lwIP dns_gethostbyname() marshalled to
// tcpip_thread, a blocking DNS query over the network. This rig has no network association to
// resolve against and no host-side test hook exists on-device (protocore_dns_resolver_test_set_resolve()
// is compiled only "#if !defined(ARDUINO)"), so only the deterministic CPU-side classifier/verifier
// is ever benched here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/dns_resolver -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/network/dns/dns_resolver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IPV4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void dbench_run(void)
{
    // Sample addresses lifted straight out of test/test_dns_resolver/test_dns_resolver.cpp -
    // already known-good, spec-conformant classification fixtures.
    static const uint32_t ip_public = IPV4(8, 8, 8, 8);      // falls through every range check
    static const uint32_t ip_private = IPV4(10, 0, 0, 5);    // matches the first range check
    static const uint32_t ip_loopback = IPV4(127, 0, 0, 1);  // rejected by verify()
    static const uint32_t ip_multicast = IPV4(224, 0, 0, 1); // rejected by verify()

    for (;;)
    {
        DBENCH_BANNER("dns_resolver");
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;
        Resolver.addr.ip = ip_public;
        DBENCH_OP("Resolver.classify (public)", 200000, Resolver.classify(Resolver.internal);
                  sink8 += (uint8_t)Resolver.cls);

        Resolver.addr.ip = ip_private;
        DBENCH_OP("Resolver.classify (private)", 200000, Resolver.classify(Resolver.internal);
                  sink8 += (uint8_t)Resolver.cls);

        Resolver.addr.ip = ip_public;
        DBENCH_OP("Resolver.verify (accept public)", 200000, Resolver.verify(Resolver.internal); sinkb ^= Resolver.ok);

        Resolver.addr.ip = ip_loopback;
        DBENCH_OP("Resolver.verify (reject loopback)", 200000, Resolver.verify(Resolver.internal);
                  sinkb ^= Resolver.ok);
        Resolver.addr.ip = ip_multicast;
        DBENCH_OP("Resolver.verify (reject multicast)", 200000, Resolver.verify(Resolver.internal);
                  sinkb ^= Resolver.ok);
        (void)sink8;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("dns_resolver")
