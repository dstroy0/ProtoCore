// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HAProxy PROXY protocol codec (services/net/proxy_protocol):
// the v1 (text) and v2 (binary) TCP/IPv4 header builders and the unified parser a server uses to
// recover the real client IPv4 when it sits behind a load balancer. Every operation here is pure
// (no sockets, no heap, no transport) - the header bytes are prepended once before the proxied
// stream, so parsing/building them is the entire CPU cost, and it is exactly what this rig times.
// The transport that would carry these bytes is out of scope (this codec never touches a socket).
//
// Benched (realistic sample data lifted verbatim from test/test_proxy_protocol/test_proxy_protocol.cpp:
// 203.0.113.50 -> 203.0.113.10, host-order uint32):
//   - proxy_v1_build  : build the "PROXY TCP4 ..." text header (snprintf dotted-quad formatting)
//   - proxy_v2_build  : build the 28-octet v2 binary TCP/IPv4 header (memcpy signature + big-endian pack)
//   - proxy_parse v1  : detect + tokenize + parse a complete v1 text header back to a ProxyInfo
//   - proxy_parse v2  : detect the 12-octet signature + unpack the v2 binary address block
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/proxy_protocol -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/proxy_protocol/proxy_protocol.h"
#include <Arduino.h>

// 203.0.113.50 / 203.0.113.10 in host-order uint32 (straight from the host test).
static const uint32_t SRC = 0xCB007132u;
static const uint32_t DST = 0xCB00710Au;

static void proxy_protocol_bench_task(void *)
{
    static char v1buf[64];
    static uint8_t v2buf[32];
    static uint8_t parsebuf[64];
    ProxyInfo info;
    size_t consumed = 0;

    // Pre-build a canonical v1 text header and a v2 binary header once, so the parse benches
    // time only the parser (not the builder that produced their input).
    static uint8_t v1hdr[64];
    static uint8_t v2hdr[32];
    size_t v1len = proxy_v1_build((char *)v1hdr, sizeof(v1hdr), SRC, DST, 12345, 80);
    size_t v2len = proxy_v2_build(v2hdr, sizeof(v2hdr), SRC, DST, 12345, 80);

    for (;;)
    {
        Serial.printf("DB ==== proxy_protocol device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        // v1 (text) build: dotted-quad snprintf formatting - the more expensive builder.
        DBENCH_OP("proxy_v1_build (TCP4 text)", 50000,
                  sink += proxy_v1_build(v1buf, sizeof(v1buf), SRC, DST, 12345, 80));
        // v2 (binary) build: signature memcpy + big-endian pack - cheap, so a large N.
        DBENCH_OP("proxy_v2_build (TCP4 binary)", 200000,
                  sink += proxy_v2_build(v2buf, sizeof(v2buf), SRC, DST, 12345, 80));

        (void)parsebuf;
        // v1 parse: detect "PROXY ", find CRLF, tokenize, parse_ipv4 x2 + parse_u16 x2.
        DBENCH_OP("proxy_parse v1 (TCP4 text)", 50000, sink += proxy_parse(v1hdr, v1len, &info, &consumed));
        // v2 parse: match the 12-octet signature, unpack the fixed binary address block.
        DBENCH_OP("proxy_parse v2 (TCP4 binary)", 200000, sink += proxy_parse(v2hdr, v2len, &info, &consumed));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: proxy_protocol device microbench");
    xTaskCreatePinnedToCore(proxy_protocol_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
