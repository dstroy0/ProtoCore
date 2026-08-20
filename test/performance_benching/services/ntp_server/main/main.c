// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NTP/SNTP server codec (network_drivers/application/ntp_server):
// protocore_ntp_server_build_response() takes a received 48-octet client request plus the current
// NTP-epoch time and writes the RFC 5905 mode-4 server reply - echoing the version, copying the
// client's transmit timestamp into the origin field, and stamping reference/receive/transmit
// times big-endian. It is pure (no clock, no sockets, zero heap), so every call here exercises
// the real production code path. Worked-example class: a pure protocol codec with no hardware
// involved (contrast with performance_benching/device/ads1115, a peripheral driver where the bus is stubbed).
//
// The UDP-binding half (protocore_ntp_server_begin -> pc_udp_listen on port 123) is deliberately out of
// scope: this rig has no network attached and the codec is what determines per-request CPU cost.
// We never call it, so no transport transaction is ever issued.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ntp_server -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/application/ntp_server/ntp_server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // A plausible, spec-conformant client request (byte layout straight from
    // test/test_ntp_server: LI=0, VN=4, Mode=3 client, poll=6, transmit stamp 0xDEADBEEF.12345678).
    static const uint8_t req[PROTOCORE_NTP_PACKET_LEN] = {
        0x23, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    // Arbitrary NTP time / half-second fraction (same literals the host test uses).
    static const uint32_t secs = 0xE6C50000u;
    static const uint32_t frac = 0x80000000u;
    static uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    uint8_t *work = protocore_ntp_server_span();

    for (;;)
    {
        DBENCH_BANNER("ntp_server");
        volatile size_t sink = 0;
        // The operands do not vary across iterations, so they are staged once above each macro;
        // only the call itself is inside the timed loop.
        NtpServer.build_response_args.req = req;
        NtpServer.build_response_args.req_len = sizeof(req);
        NtpServer.build_response_args.protocore_ntp_secs = secs;
        NtpServer.build_response_args.protocore_ntp_frac = frac;
        NtpServer.build_response_args.out = out;
        NtpServer.build_response_args.out_cap = sizeof(out);

        // Full server reply build: stratum 3 relay advertising the local clock (LOCL).
        NtpServer.build_response_args.stratum = 3;
        NtpServer.build_response_args.refid = PROTOCORE_NTP_REFID_LOCL;
        DBENCH_OP("NtpServer.build_response", 100000, sink += (NtpServer.build_response(work), NtpServer.n));
        // Same codec advertising a stratum-1 GPS reference clock (different stratum/refid inputs).
        NtpServer.build_response_args.stratum = 1;
        NtpServer.build_response_args.refid = PROTOCORE_NTP_REFID_GPS;
        DBENCH_OP("build_response gps stratum1", 100000, sink += (NtpServer.build_response(work), NtpServer.n));
        // Throughput view: the reply is a fixed 48-octet packet, so report ns/byte + MB/s.
        NtpServer.build_response_args.stratum = 3;
        NtpServer.build_response_args.refid = PROTOCORE_NTP_REFID_LOCL;
        DBENCH_BULK("build_response throughput", 100000, PROTOCORE_NTP_PACKET_LEN,
                    sink += (NtpServer.build_response(work), NtpServer.n));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ntp_server")
