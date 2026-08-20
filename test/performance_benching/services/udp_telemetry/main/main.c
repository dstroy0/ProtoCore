// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the UDP telemetry line builder (services/iot/udp_telemetry): the
// InfluxDB line-protocol assembly (measurement + tags + int/uint/float fields) - the per-point hot op
// before the UDP send. Pure; no socket.
//
// Build/flash:  idf.py -C test/performance_benching/udp_telemetry -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/udp_telemetry/udp_telemetry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Bind @p buf as the line buffer and open the line with @p measurement. */
static void line_open(char *buf, size_t cap, const char *measurement)
{
    UdpTelemetry.line.buf = buf;
    UdpTelemetry.line.cap = cap;
    UdpTelemetry.line.measurement = measurement;
    UdpTelemetry.measurement(protocore_udp_telemetry_span());
}

/** @brief Append one tag set entry `,key=value`. */
static void line_tag(const char *key, const char *value)
{
    UdpTelemetry.tags.key = key;
    UdpTelemetry.tags.value = value;
    UdpTelemetry.tag(protocore_udp_telemetry_span());
}

/** @brief Append `key=<v>i`. */
static void line_int(const char *key, int64_t v)
{
    UdpTelemetry.fields.key = key;
    UdpTelemetry.fields.i64 = v;
    UdpTelemetry.field_int(protocore_udp_telemetry_span());
}

/** @brief Append `key=<v>u`. */
static void line_uint(const char *key, uint64_t v)
{
    UdpTelemetry.fields.key = key;
    UdpTelemetry.fields.u64 = v;
    UdpTelemetry.field_uint(protocore_udp_telemetry_span());
}

/** @brief Append `key=<v>` to @p decimals places. */
static void line_float(const char *key, float v, uint8_t decimals)
{
    UdpTelemetry.fields.key = key;
    UdpTelemetry.fields.f32 = v;
    UdpTelemetry.fields.decimals = decimals;
    UdpTelemetry.field_float(protocore_udp_telemetry_span());
}

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("udp_telemetry");
        volatile size_t sink = 0;
        static char buf[256];
        DBENCH_OP("UdpTelemetry line build (2 tags, 3 fields)", 200000, {
            line_open(buf, sizeof(buf), "env");
            line_tag("host", "rig-1");
            line_tag("room", "lab");
            line_float("temp", 21.5f, 1);
            line_int("rssi", -42);
            line_uint("uptime", 1234u);
            sink += UdpTelemetry.n;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("udp_telemetry")
