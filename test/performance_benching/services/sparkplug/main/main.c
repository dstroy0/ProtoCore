// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
        DBENCH_OP("protocore_spb_build_topic", 200000,
                  sink += protocore_spb_build_topic(topic, sizeof(topic), "Group1", "NDATA", "edge1", "dev1"));
        static uint8_t buf[256];
        DBENCH_OP("protocore_spb_build_metric", 200000,
                  sink += protocore_spb_build_metric(buf, sizeof(buf), &metrics[0]));
        DBENCH_OP("protocore_spb_build_payload (3 metrics)", 200000,
                  sink += protocore_spb_build_payload(buf, sizeof(buf), 1720700000000ull, 1, metrics, 3));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sparkplug")
