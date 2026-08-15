// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the trusted-reverse-proxy forwarded-client resolver
// (server/security/forwarded_trust): protocore_forwarded_trust_add_cidr() (text CIDR -> table insert),
// protocore_forwarded_trust_contains() (per-request trusted-upstream membership test), and
// protocore_forwarded_effective_ip() (the full resolve: honor the Forwarded/X-Forwarded-For client only
// when the real TCP peer is a trusted upstream and the token is a valid, specified address, else
// keep the TCP peer) - all pure (no sockets, no heap), so every call here exercises the real
// production code path. Like performance_benching/device/modbus and performance_benching/device/auth_lockout (a
// required parent of this feature), this is a pure protocol/state codec with no hardware involved - there is nothing to
// stub for this service.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/forwarded_trust -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/security/forwarded_trust/forwarded_trust.h"
#include "shared/ip/ip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// v4()/v6() mirror the helpers in test/test_forwarded_trust/test_forwarded_trust.cpp (already
// known-good) so every address below is built the same way the host test builds it.
static protocore_ip v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return protocore_ip_from_v4_octets(a, b, c, d);
}

static protocore_ip v6(const char *s)
{
    protocore_ip ip;
    ip.family = PROTOCORE_IP_NONE;
    protocore_ip_parse(s, &ip); // known-good literal, checked once by the host test
    return ip;
}

// Fresh add_cidr(): reset the table first so every benched call is a genuine parse-and-insert into
// an empty slot, not a "table full" short-circuit (PROTOCORE_TRUSTED_PROXY_MAX defaults to 2).
static bool add_v4_cidr_fresh()
{
    protocore_forwarded_trust_reset();
    return protocore_forwarded_trust_add_cidr("10.0.0.0/8");
}

static bool add_v6_cidr_fresh()
{
    protocore_forwarded_trust_reset();
    return protocore_forwarded_trust_add_cidr("2001:db8::/32");
}

void dbench_run(void)
{
    // Membership/resolve fixtures lifted straight out of test_v4_cidr_membership /
    // test_trusted_peer_honors_forwarded / test_untrusted_peer_ignores_forwarded.
    protocore_ip v4_in = v4(10, 4, 4, 1);         // inside 10.0.0.0/8
    protocore_ip v4_out = v4(11, 0, 0, 1);        // outside 10.0.0.0/8
    protocore_ip trusted_proxy = v4(10, 1, 2, 3); // trusted upstream, forwards a valid client
    protocore_ip attacker = v4(203, 0, 113, 66);  // NOT in the trusted range
    const char *fwd_client_str = "198.51.100.42"; // client the trusted proxy forwards
    const char *fwd_spoof_str = "198.51.100.1";   // victim address the attacker tries to spoof in

    for (;;)
    {
        DBENCH_BANNER("forwarded_trust");
        volatile bool sinkb = false;

        // Config-time cost: parse a CIDR string and insert it into an empty table.
        DBENCH_OP("protocore_forwarded_trust_add_cidr (v4)", 50000, sinkb ^= add_v4_cidr_fresh());
        DBENCH_OP("protocore_forwarded_trust_add_cidr (v6)", 50000, sinkb ^= add_v6_cidr_fresh());

        // Populate the table with both trusted upstreams (not benched) for the per-request hot
        // paths below - this fills the default 2-slot PROTOCORE_TRUSTED_PROXY_MAX table exactly.
        protocore_forwarded_trust_reset();
        protocore_forwarded_trust_add_cidr("10.0.0.0/8");
        protocore_forwarded_trust_add_cidr("2001:db8::/32");

        // Hot path: the per-request trusted-upstream membership test (hit and miss).
        DBENCH_OP("protocore_forwarded_trust_contains (v4 hit)", 200000,
                  sinkb ^= protocore_forwarded_trust_contains(&v4_in));
        DBENCH_OP("protocore_forwarded_trust_contains (v4 miss)", 200000,
                  sinkb ^= protocore_forwarded_trust_contains(&v4_out));

        protocore_ip out;
        // Hot path: a trusted proxy's valid forwarded client is honored.
        DBENCH_OP("protocore_forwarded_effective_ip (honored)", 100000,
                  sinkb ^= protocore_forwarded_effective_ip(&trusted_proxy, fwd_client_str, &out));
        // THE security property: an untrusted (direct) peer's forwarded header is ignored - no
        // spoofing an abuse-prevention lockout onto a victim's address.
        DBENCH_OP("protocore_forwarded_effective_ip (untrusted, spoof blocked)", 100000,
                  sinkb ^= protocore_forwarded_effective_ip(&attacker, fwd_spoof_str, &out));
        (void)sinkb;

        DBENCH_DONE();
    }
}

DBENCH_MAIN("forwarded_trust")
