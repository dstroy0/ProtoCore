// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the MQTT 3.1.1 client codec: the pure packet build/parse hot ops the
// device runs as an MQTT client (pc_mqtt_build_connect / pc_mqtt_build_publish, and pc_mqtt_parse_publish - the
// inbound-message decode). All pure (no sockets, no heap); the transport (mqtt_connect etc.) is
// Arduino-only, so the codec links standalone. The device number comes from the rig /bench endpoint;
// this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_MQTT=1 performance_benching/services/mqtt/host.cpp
//   \
//       src/services/iot/mqtt/mqtt.cpp -o /tmp/bm && /tmp/bm    (utf8.h is header-only)

#define PC_ENABLE_MQTT 1
#include "services/iot/mqtt/mqtt.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
    {
        fn();
    }
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    MqttConnectOpts opts{};
    opts.client_id = "pc-s3-rig";
    opts.keepalive_s = 60;
    opts.clean_session = true;

    const char *topic = "factory/line1/sensor/temp";
    const uint8_t payload[] = "{\"v\":21.4,\"u\":\"C\",\"ts\":1720700000}";
    const size_t plen = sizeof(payload) - 1;

    uint8_t conn[256], pub[256];
    size_t clen = pc_mqtt_build_connect(conn, sizeof(conn), &opts);
    size_t publen = pc_mqtt_build_publish(pub, sizeof(pub), topic, payload, plen, 1, 0x1234, false, false);

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // build CONNECT: the packet the client sends on connect.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(1000000, [&] { sink += pc_mqtt_build_connect(conn, sizeof(conn), &opts); });
        row("mqtt", "build CONNECT", ns, (double)clen);
        (void)sink;
    }
    // build PUBLISH (QoS 1): the publish hot op.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(1000000, [&] {
            sink += pc_mqtt_build_publish(pub, sizeof(pub), topic, payload, plen, 1, 0x1234, false, false);
        });
        row("mqtt", "build PUBLISH (qos1)", ns, (double)publen);
        (void)sink;
    }
    // parse PUBLISH: the inbound-message decode (fixed header + variable header + payload).
    {
        char tbuf[128];
        volatile int sink = 0;
        double ns = bench_ns(1000000, [&] {
            uint8_t type = 0, flags = 0;
            uint32_t rl = 0;
            size_t hlen = 0;
            if (pc_mqtt_parse_fixed_header(pub, publen, &type, &flags, &rl, &hlen))
            {
                const uint8_t *pl = nullptr;
                size_t tl = 0, pll = 0;
                uint16_t pid = 0;
                sink += pc_mqtt_parse_publish(pub + hlen, rl, flags, tbuf, sizeof(tbuf), &tl, &pl, &pll, &pid) ? 1 : 0;
            }
        });
        row("mqtt", "parse PUBLISH", ns, (double)publen);
        (void)sink;
    }

    return 0;
}
