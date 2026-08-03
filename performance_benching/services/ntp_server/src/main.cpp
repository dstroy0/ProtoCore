// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NTP/SNTP server codec (network_drivers/application/ntp_server):
// pc_ntp_server_build_response() takes a received 48-octet client request plus the current
// NTP-epoch time and writes the RFC 5905 mode-4 server reply - echoing the version, copying the
// client's transmit timestamp into the origin field, and stamping reference/receive/transmit
// times big-endian. It is pure (no clock, no sockets, zero heap), so every call here exercises
// the real production code path. Worked-example class: a pure protocol codec with no hardware
// involved (contrast with performance_benching/device/ads1115, a peripheral driver where the bus is stubbed).
//
// The UDP-binding half (pc_ntp_server_begin -> pc_udp_listen on port 123) is deliberately out of
// scope: this rig has no network attached and the codec is what determines per-request CPU cost.
// We never call it, so no transport transaction is ever issued.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ntp_server -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/application/ntp_server/ntp_server.h"
#include <Arduino.h>

static void ntp_server_bench_task(void *)
{
    // A plausible, spec-conformant client request (byte layout straight from
    // test/test_ntp_server: LI=0, VN=4, Mode=3 client, poll=6, transmit stamp 0xDEADBEEF.12345678).
    static const uint8_t req[NTP_PACKET_LEN] = {0x23, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    // Arbitrary NTP time / half-second fraction (same literals the host test uses).
    static const uint32_t secs = 0xE6C50000u;
    static const uint32_t frac = 0x80000000u;
    static uint8_t out[NTP_PACKET_LEN];

    for (;;)
    {
        Serial.printf("DB ==== ntp_server device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        // Full server reply build: stratum 3 relay advertising the local clock (LOCL).
        DBENCH_OP("pc_ntp_server_build_response", 100000,
                  sink +=
                  pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, secs, frac, out, sizeof(out)));
        // Same codec advertising a stratum-1 GPS reference clock (different stratum/refid inputs).
        DBENCH_OP("build_response gps stratum1", 100000,
                  sink +=
                  pc_ntp_server_build_response(req, sizeof(req), 1, NTP_REFID_GPS, secs, frac, out, sizeof(out)));
        // Throughput view: the reply is a fixed 48-octet packet, so report ns/byte + MB/s.
        DBENCH_BULK("build_response throughput", 100000, NTP_PACKET_LEN,
                    sink +=
                    pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, secs, frac, out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ntp_server device microbench");
    xTaskCreatePinnedToCore(ntp_server_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
