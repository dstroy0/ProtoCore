// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the NTP server (RFC 5905 server mode): protocore_ntp_server_build_response() parses a
// 48-octet client request and stamps the mode-4 server reply (echo VN, copy the client transmit stamp into
// origin, fill reference/receive/transmit timestamps). Pure (no clock, no socket), so it links standalone;
// the device UDP binding is compiled out on host. The device figure comes from the rig /bench op; this host
// ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_TIME_SOURCE=1 -DPROTOCORE_ENABLE_NTP_SERVER=1
//   test/performance_benching/services/ntp_server/host.c
//   src/network_drivers/application/ntp_server/ntp_server.c
//   src/services/timing_position/time_source/time_source.c src/network_drivers/transport/udp.c
//   src/network_drivers/transport/udp/udp_listener.c src/network_drivers/transport/udp/udp_client.c
//   src/network_drivers/transport/net_addr.c src/server/clock/clock.c src/mmgr/protomem.c
//   src/mmgr/protostr.c src/shared/ip/ip.c -o /tmp/bn && /tmp/bn

#define PROTOCORE_ENABLE_TIME_SOURCE 1
#define PROTOCORE_ENABLE_NTP_SERVER 1
#include "network_drivers/application/ntp_server/ntp_server.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    // A well-formed client request: LI 0, VN 4, mode 3 (client); a transmit timestamp in bytes 40..47.
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    memset(req, 0, sizeof(req));
    req[0] = 0x23; // 00 100 011
    for (int i = 40; i < 48; i++)
    {
        req[i] = (uint8_t)(i * 3 + 1);
    }
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];

    hbench_header();

    // build one mode-4 server reply per client request (the whole per-query server cost).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000,
                  sink += protocore_ntp_server_build_response(req, sizeof(req), 2, PROTOCORE_NTP_REFID_LOCL,
                                                              0xE9A1B2C3u, 0x80000000u, out, sizeof(out)),
                  ns);
        hbench_row("ntp", "build_response (48-octet)", ns,
                   (double)(PROTOCORE_NTP_PACKET_LEN * 2)); // request + reply moved
        (void)sink;
    }

    return 0;
}
