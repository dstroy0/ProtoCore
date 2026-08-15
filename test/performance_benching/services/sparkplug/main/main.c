// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Sparkplug B codec (services/iot/sparkplug): the topic string
// builder, a single protobuf metric encode, and a full NDATA payload encode. Pure; the MQTT
// transport is elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/sparkplug -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/sparkplug/sparkplug.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Join `spBv1.0/Group1/NDATA/edge1/dev1` into @p out; the octets written. */
static size_t spb_topic(char *out, size_t cap)
{
    Sparkplug.topic_out.out = out;
    Sparkplug.topic_out.cap = cap;
    Sparkplug.topic.group_id = "Group1";
    Sparkplug.topic.message_type = "NDATA";
    Sparkplug.topic.edge_node_id = "edge1";
    Sparkplug.topic.device_id = "dev1";
    Sparkplug.build_topic(Sparkplug.internal);
    return Sparkplug.n;
}

/** @brief Serialize @p m as one Metric message into @p out; the octets written. */
static size_t spb_metric(uint8_t *out, size_t cap, const SpbMetric *m)
{
    Sparkplug.out.buf = out;
    Sparkplug.out.cap = cap;
    Sparkplug.metrics.list = m;
    Sparkplug.build_metric(Sparkplug.internal);
    return Sparkplug.n;
}

/** @brief Serialize a Payload header plus @p n Metrics into @p out; the octets written. */
static size_t spb_payload(uint8_t *out, size_t cap, const SpbMetric *m, size_t n, uint64_t ts, uint64_t seq)
{
    Sparkplug.out.buf = out;
    Sparkplug.out.cap = cap;
    Sparkplug.payload.timestamp = ts;
    Sparkplug.payload.seq = seq;
    Sparkplug.metrics.list = m;
    Sparkplug.metrics.count = n;
    Sparkplug.build_payload(Sparkplug.internal);
    return Sparkplug.n;
}

void dbench_run(void)
{
    static SpbMetric metrics[3];
    for (int i = 0; i < 3; i++)
    {
        metrics[i] = (SpbMetric){0};
        metrics[i].name = "line1/temp";
        metrics[i].datatype = SPB_DT_DOUBLE;
        metrics[i].kind = SPB_M_DOUBLE;
        metrics[i].double_value = 21.4 + i;
    }

    for (;;)
    {
        DBENCH_BANNER("sparkplug");
        volatile size_t sink = 0;
        static char topic[128];
        DBENCH_OP("Sparkplug.build_topic", 200000, sink += spb_topic(topic, sizeof(topic)));
        static uint8_t buf[256];
        DBENCH_OP("Sparkplug.build_metric", 200000, sink += spb_metric(buf, sizeof(buf), &metrics[0]));
        DBENCH_OP("Sparkplug.build_payload (3 metrics)", 200000,
                  sink += spb_payload(buf, sizeof(buf), metrics, 3, 1720700000000ull, 1));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sparkplug")
