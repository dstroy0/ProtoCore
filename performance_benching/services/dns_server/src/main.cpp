// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the authoritative DNS server codec (services/net/dns_server):
// pc_dns_server_build_response() (QNAME parse + compressed A answer / NXDOMAIN) and the
// built-in name->IP table (pc_dns_server_add / pc_dns_server_lookup) - all pure (no clock, no
// sockets, no heap). Worked example category: a pure protocol codec with no hardware involved
// (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction is stubbed).
// pc_dns_server_begin() (binds UDP/53 via the transport UDP service) is out of scope everywhere
// on this rig - it is a thin wrapper with no CPU-bound work of its own to measure.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/dns_server -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "protocore_config.h" // PC_DNS_SERVER_MAX_RECORDS
#include "services/net/dns_server/dns_server.h"
#include <Arduino.h>
#include <stdio.h> // snprintf

// Build a DNS query: header + one question (QNAME / QTYPE / QCLASS=IN). Copied verbatim from
// test/test_dns_server/test_dns_server.cpp (already known-good, spec-conformant wire encoding).
static size_t make_query(uint8_t *buf, uint16_t id, const char *name, uint16_t qtype, bool rd)
{
    size_t n = 0;
    buf[n++] = (uint8_t)(id >> 8);
    buf[n++] = (uint8_t)id;
    buf[n++] = rd ? 0x01 : 0x00; // flags hi: standard query, RD bit
    buf[n++] = 0x00;             // flags lo
    buf[n++] = 0x00;
    buf[n++] = 0x01; // QDCOUNT = 1
    for (int i = 0; i < 6; i++)
    {
        buf[n++] = 0x00; // AN/NS/AR = 0
    }
    const char *p = name;
    while (*p)
    {
        const char *dot = strchr(p, '.');
        size_t label = dot ? (size_t)(dot - p) : strlen(p);
        buf[n++] = (uint8_t)label;
        memcpy(buf + n, p, label);
        n += label;
        p += label;
        if (*p == '.')
        {
            p++;
        }
    }
    buf[n++] = 0x00; // end of QNAME
    buf[n++] = (uint8_t)(qtype >> 8);
    buf[n++] = (uint8_t)qtype;
    buf[n++] = 0x00;
    buf[n++] = 0x01; // QCLASS = IN
    return n;
}

// Resolvers matching test_dns_server.cpp's resolve_foo/resolve_none: a fixed A-record hit and an
// always-miss, used to bench the answer path and the NXDOMAIN path independently of table state.
static uint32_t resolve_foo(const char *name)
{
    return strcmp(name, "foo.lan") == 0 ? 0xC0A80105u : 0; // 192.168.1.5
}
static uint32_t resolve_none(const char *name)
{
    (void)name;
    return 0;
}

static void dns_server_bench_task(void *)
{
    static uint8_t q_hit[128], q_miss[128], out[256];
    size_t qlen_hit = make_query(q_hit, 0x1234, "foo.lan", 1, true);   // A hit -> answer
    size_t qlen_miss = make_query(q_miss, 1, "unknown.lan", 1, false); // A miss -> NXDOMAIN

    char nm[16];

    for (;;)
    {
        Serial.printf("DB ==== dns_server device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint32_t sink32 = 0;
        volatile bool sinkb = false;

        DBENCH_OP("pc_dns_server_build_response A", 20000,
                  sink += pc_dns_server_build_response(q_hit, qlen_hit, 60, resolve_foo, out, sizeof(out)));
        DBENCH_OP("pc_dns_server_build_response NXDOMAIN", 20000,
                  sink += pc_dns_server_build_response(q_miss, qlen_miss, 60, resolve_none, out, sizeof(out)));

        // Re-seed the built-in table (8 records, matching test_dns_add_and_lookup_guards) so the
        // lookup benches below always run against a full, known table before the add bench (which
        // clears it) runs last.
        pc_dns_server_clear();
        for (int i = 0; i < PC_DNS_SERVER_MAX_RECORDS; i++)
        {
            snprintf(nm, sizeof(nm), "h%d.lan", i);
            pc_dns_server_add(nm, 10, 0, 0, (uint8_t)i);
        }

        DBENCH_OP("pc_dns_server_lookup hit", 50000,
                  sink32 += pc_dns_server_lookup("H7.LAN")); // worst-case scan, case-insensitive
        DBENCH_OP("pc_dns_server_lookup miss", 50000, sink32 += pc_dns_server_lookup("absent.lan"));
        DBENCH_OP("pc_dns_server_add", 50000,
                  (pc_dns_server_clear(), sinkb = pc_dns_server_add("bench.lan", 10, 0, 0, 1)));

        (void)sink;
        (void)sink32;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: dns_server device microbench");
    xTaskCreatePinnedToCore(dns_server_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
