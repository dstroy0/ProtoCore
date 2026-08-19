// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the MQTT 3.1.1 client codec: the pure packet build/parse hot ops the
// device runs as an MQTT client (pc_mqtt_build_connect / pc_mqtt_build_publish, and pc_mqtt_parse_publish - the
// inbound-message decode). All pure (no sockets, no heap); the transport (mqtt_connect etc.) is
// device-only, so the codec links standalone. The device number comes from the rig /bench endpoint;
// this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_MQTT=1 test/performance_benching/services/mqtt/host.c
//   src/services/iot/mqtt/mqtt.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bm && /tmp/bm
//   (utf8.h is header-only)

#define PROTOCORE_ENABLE_MQTT 1
#include "services/iot/mqtt/mqtt/mqtt.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    MqttConnectOpts opts = {0};
    opts.client_id = "pc-s3-rig";
    opts.keepalive_s = 60;
    opts.clean_session = true;

    const char *topic = "factory/line1/sensor/temp";
    const uint8_t payload[] = "{\"v\":21.4,\"u\":\"C\",\"ts\":1720700000}";
    const size_t plen = sizeof(payload) - 1;

    uint8_t conn[256], pub[256];
    // The builders declare no storage: the caller lends the scratch the variable header and payload
    // are assembled in before the fixed header's length is known.
    static uint8_t scratch[256];
    size_t clen = pc_mqtt_build_connect(conn, sizeof(conn), &opts, scratch, sizeof(scratch));
    size_t publen = pc_mqtt_build_publish(pub, sizeof(pub), topic, payload, plen, 1, 0x1234, false, false, scratch,
                                          sizeof(scratch));

    hbench_header();

    // build CONNECT: the packet the client sends on connect.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(1000000, sink += pc_mqtt_build_connect(conn, sizeof(conn), &opts, scratch, sizeof(scratch)), ns);
        hbench_row("mqtt", "build CONNECT", ns, (double)clen);
        (void)sink;
    }
    // build PUBLISH (QoS 1): the publish hot op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(1000000,
                  sink += pc_mqtt_build_publish(pub, sizeof(pub), topic, payload, plen, 1, 0x1234, false, false,
                                                scratch, sizeof(scratch)),
                  ns);
        hbench_row("mqtt", "build PUBLISH (qos1)", ns, (double)publen);
        (void)sink;
    }
    // parse PUBLISH: the inbound-message decode (fixed header + variable header + payload).
    {
        char tbuf[128];
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            1000000,
            {
                uint8_t type = 0;
                uint8_t flags = 0;
                uint32_t rl = 0;
                size_t hlen = 0;
                if (pc_mqtt_parse_fixed_header(pub, publen, &type, &flags, &rl, &hlen))
                {
                    const uint8_t *pl = NULL;
                    size_t tl = 0;
                    size_t pll = 0;
                    uint16_t pid = 0;
                    sink +=
                        pc_mqtt_parse_publish(pub + hlen, rl, flags, tbuf, sizeof(tbuf), &tl, &pl, &pll, &pid) ? 1 : 0;
                }
            },
            ns);
        hbench_row("mqtt", "parse PUBLISH", ns, (double)publen);
        (void)sink;
    }

    return 0;
}
