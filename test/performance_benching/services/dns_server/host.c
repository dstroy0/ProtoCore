// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the authoritative DNS server (RFC 1035): DnsServer.build_response() parses
// an A/IN query and appends the compressed A answer (or NXDOMAIN on a miss). Pure (no clock, no socket), so
// it links standalone; the device UDP binding is compiled out on host. The device figure comes from the rig
// /bench op; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPC_ENABLE_DNS_SERVER=1 test/performance_benching/services/dns_server/host.c
//   src/network_drivers/network/dns/dns_server.c src/network_drivers/network/dns/dns_wire.c
//   src/network_drivers/transport/udp.c src/network_drivers/transport/udp/udp_listener.c
//   src/network_drivers/transport/udp/udp_client.c src/network_drivers/transport/net_addr.c
//   src/mmgr/protomem.c src/mmgr/protostr.c src/shared_primitives/ip.c -o /tmp/bd && /tmp/bd

#define PC_ENABLE_DNS_SERVER 1
#include "network_drivers/network/dns/dns_server.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

// Always resolves, so build_response takes the A-answer path (the heaviest).
static uint32_t resolve_hit(const char *name)
{
    (void)name;
    return 0xC0A80105u; // 192.168.1.5
}

static uint32_t resolve_miss(const char *name)
{
    (void)name;
    return 0; // -> NXDOMAIN
}

int main(void)
{
    // "test.lan" A IN: 12-byte header + question (04 test 03 lan 00 + qtype/qclass).
    const uint8_t query[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
                             't',  'e',  's',  't',  0x03, 'l',  'a',  'n',  0x00, 0x00, 0x01, 0x00, 0x01};
    const size_t qlen = sizeof(query);
    uint8_t out[192]; // >= PC_DNS_NAME_MAX (128) + one A answer

    hbench_header();

    // A hit: parse question + append the compressed A answer.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += DnsServer.build_response(query, qlen, 60, resolve_hit, out, sizeof(out)), ns);
        hbench_row("dns", "build_response (A hit)", ns, (double)qlen);
        (void)sink;
    }
    // A miss -> NXDOMAIN (parse only, no answer RR).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += DnsServer.build_response(query, qlen, 60, resolve_miss, out, sizeof(out)), ns);
        hbench_row("dns", "build_response (NXDOMAIN)", ns, (double)qlen);
        (void)sink;
    }

    return 0;
}
