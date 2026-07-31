// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DNS answer classifier/verifier (services/net/dns_resolver):
// pc_dns_resolver_classify() buckets a host-order IPv4 word into an RFC special-purpose-range
// category, and pc_dns_resolver_verify() uses it to reject spoof / DNS-rebinding indicators
// (unspecified / broadcast / loopback / multicast) - both pure, branch-heavy, no lwIP involved.
// Out of scope: pc_dns_resolver_resolve() (and therefore pc_dns_resolver_resolve_verified(),
// which calls it) - on ARDUINO builds that is the real lwIP dns_gethostbyname() marshalled to
// tcpip_thread, a blocking DNS query over the network. This rig has no network association to
// resolve against and no host-side test hook exists on-device (pc_dns_resolver_test_set_resolve()
// is compiled only "#if !defined(ARDUINO)"), so only the deterministic CPU-side classifier/verifier
// is ever benched here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/dns_resolver -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/dns_resolver/dns_resolver.h"
#include <Arduino.h>

#define IPV4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static void dns_resolver_bench_task(void *)
{
    // Sample addresses lifted straight out of test/test_dns_resolver/test_dns_resolver.cpp -
    // already known-good, spec-conformant classification fixtures.
    static const uint32_t ip_public = IPV4(8, 8, 8, 8);      // falls through every range check
    static const uint32_t ip_private = IPV4(10, 0, 0, 5);    // matches the first range check
    static const uint32_t ip_loopback = IPV4(127, 0, 0, 1);  // rejected by verify()
    static const uint32_t ip_multicast = IPV4(224, 0, 0, 1); // rejected by verify()

    for (;;)
    {
        Serial.printf("DB ==== dns_resolver device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;
        DBENCH_OP("pc_dns_resolver_classify (public)", 200000, sink8 += (uint8_t)pc_dns_resolver_classify(ip_public));
        DBENCH_OP("pc_dns_resolver_classify (private)", 200000, sink8 += (uint8_t)pc_dns_resolver_classify(ip_private));
        DBENCH_OP("pc_dns_resolver_verify (accept public)", 200000, sinkb ^= pc_dns_resolver_verify(ip_public));
        DBENCH_OP("pc_dns_resolver_verify (reject loopback)", 200000, sinkb ^= pc_dns_resolver_verify(ip_loopback));
        DBENCH_OP("pc_dns_resolver_verify (reject multicast)", 200000, sinkb ^= pc_dns_resolver_verify(ip_multicast));
        (void)sink8;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: dns_resolver device microbench");
    xTaskCreatePinnedToCore(dns_resolver_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
