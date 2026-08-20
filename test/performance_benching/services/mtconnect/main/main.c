// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the MTConnect agent response codec (services/machine_tool/mtconnect,
// ANSI/MTC1.4): the four XML document builders an agent answers HTTP requests with, all pure
// text-framing over a caller buffer (zero heap, no stdlib, values XML-escaped) - so like
// performance_benching/device/modbus this is a pure protocol codec and every call here runs the real production
// path, no hardware to stub out. Benched:
//   - MTConnectStreams (the `current`/`sample` response): begin header + Samples/Events/Condition
//     observations + end.
//   - MTConnectDevices (the `probe` response): the device model - a <Device> with its <DataItems>.
//   - MTConnectAssets (the `asset` response): a <CuttingTool> with its <CuttingToolLifeCycle>.
//   - MTConnectError (a request error): header + <Errors><Error errorCode=..>.
//   - MtConnect.ring_query: replay a from/count sub-window out of the rolling observation ring
//     into an MTConnectStreams document (the long-poll `sample` cursor).
// Out of scope: the HTTP transport (sockets/AsyncWebServer) that carries these documents - only the
// deterministic CPU-side document framing is timed. Sample data is copied verbatim from the
// known-good, spec-conformant vectors in test/test_mtconnect/test_mtconnect.cpp.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/mtconnect -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/mtconnect/mtconnect.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static char buf[1024];
    uint8_t *const w = protocore_mtconnect_span();

    // A populated rolling observation ring for the `sample` long-poll query (built once).
    MtConnect.streams.next_seq = 1;
    MtConnect.ring_init(w);
    MtConnect.obs.cat = PROTOCORE_MTC_SAMPLE;
    MtConnect.obs.type = "Position";
    MtConnect.obs.data_id = "xpos";
    MtConnect.obs.timestamp = "T1";
    MtConnect.obs.value = "1.0";
    MtConnect.ring_add(w);
    MtConnect.obs.timestamp = "T2";
    MtConnect.obs.value = "2.0";
    MtConnect.ring_add(w);
    MtConnect.obs.cat = PROTOCORE_MTC_EVENT;
    MtConnect.obs.type = "Execution";
    MtConnect.obs.data_id = "exec";
    MtConnect.obs.timestamp = "T3";
    MtConnect.obs.value = "ACTIVE";
    MtConnect.ring_add(w);

    // What every timed document carries. Staged once above the loop: none of it varies across
    // iterations, so hoisting it keeps the macro timing the framing and not the assignments.
    MtConnect.doc.out = buf;
    MtConnect.doc.cap = sizeof(buf);
    MtConnect.doc.instance_id = 1500;
    MtConnect.doc.sender = "agent-1";

    for (;;)
    {
        DBENCH_BANNER("mtconnect");
        volatile size_t sink = 0;

        // MTConnectStreams (`current`/`sample`): header + one Event, one Sample, one Condition.
        MtConnect.streams.next_seq = 42;
        MtConnect.streams.device_name = "cnc1";
        MtConnect.streams.device_uuid = "uuid-abc";
        MtConnect.streams.component = "Device";
        MtConnect.streams.component_id = "dev1";
        MtConnect.window.first_seq = 1;
        MtConnect.window.last_seq = 41;
        MtConnect.window.buffer_size = PROTOCORE_MTC_SAMPLE_BUFFER;
        DBENCH_OP(
            "MtConnect.streams build", 20000, MtConnect.streams_begin(w); MtConnect.obs.cat = PROTOCORE_MTC_EVENT;
            MtConnect.obs.type = "Availability"; MtConnect.obs.data_id = "avail"; MtConnect.obs.seq = 40;
            MtConnect.obs.timestamp = "2026-07-06T00:00:00Z"; MtConnect.obs.value = "AVAILABLE";
            MtConnect.streams_add(w); MtConnect.obs.cat = PROTOCORE_MTC_SAMPLE; MtConnect.obs.type = "Position";
            MtConnect.obs.data_id = "xpos"; MtConnect.obs.seq = 41;
            MtConnect.obs.timestamp = "2026-07-06T00:00:01Z"; MtConnect.obs.value = "12.5";
            MtConnect.streams_add(w); MtConnect.obs.cat = PROTOCORE_MTC_CONDITION;
            MtConnect.obs.type = "SystemCondition"; MtConnect.obs.data_id = "sys"; MtConnect.obs.seq = 42;
            MtConnect.obs.timestamp = "2026-07-06T00:00:02Z"; MtConnect.obs.value = "Fault";
            MtConnect.streams_add(w); MtConnect.streams_end(w); sink += MtConnect.n);

        // MTConnectDevices (`probe`): the device model with three DataItems.
        MtConnect.device.device_id = "dev1";
        MtConnect.device.device_name = "cnc1";
        MtConnect.device.uuid = "uuid-abc";
        MtConnect.assets.asset_count = 2;
        MtConnect.assets.asset_buffer_size = 1024;
        DBENCH_OP("MtConnect.devices probe build", 20000, MtConnect.devices_begin(w);
                  MtConnect.item.cat = PROTOCORE_MTC_EVENT; MtConnect.item.id = "avail";
                  MtConnect.item.type = "Availability"; MtConnect.item.name = NULL; MtConnect.item.units = NULL;
                  MtConnect.devices_add(w); MtConnect.item.cat = PROTOCORE_MTC_SAMPLE; MtConnect.item.id = "xpos";
                  MtConnect.item.type = "Position"; MtConnect.item.name = "Xabs";
                  MtConnect.item.units = "MILLIMETER"; MtConnect.devices_add(w);
                  MtConnect.item.cat = PROTOCORE_MTC_CONDITION; MtConnect.item.id = "sys";
                  MtConnect.item.type = "SystemCondition"; MtConnect.item.name = NULL; MtConnect.item.units = NULL;
                  MtConnect.devices_add(w); MtConnect.devices_end(w); sink += MtConnect.n);

        // MTConnectAssets (`asset`): one CuttingTool with a ToolLife.
        MtConnect.tool.asset_id = "tool-1";
        MtConnect.tool.serial_number = "SN-42";
        MtConnect.tool.tool_id = "T17";
        MtConnect.tool.device_uuid = "uuid-abc";
        MtConnect.tool.timestamp = "2026-07-09T00:00:00Z";
        MtConnect.tool.cutter_status = "AVAILABLE";
        MtConnect.life.type = "MINUTES";
        MtConnect.life.count_direction = "DOWN";
        MtConnect.life.initial = "100";
        MtConnect.life.limit = "0";
        MtConnect.life.value = "42";
        DBENCH_OP("MtConnect.assets build", 20000, MtConnect.assets_begin(w); MtConnect.tool_begin(w);
                  MtConnect.tool_life(w); MtConnect.tool_end(w); MtConnect.assets_end(w); sink += MtConnect.n);

        // MTConnectError: header + one Error element.
        MtConnect.err.error_code = "UNSUPPORTED";
        MtConnect.err.message = "bad path";
        DBENCH_OP("MtConnect.error build", 50000, sink += (MtConnect.error(w), MtConnect.n));

        // Long-poll `sample` cursor: replay the whole retained window as an MTConnectStreams document.
        MtConnect.streams.device_name = "cnc1";
        MtConnect.query.from = 1;
        MtConnect.query.count = 10;
        DBENCH_OP("MtConnect.ring_query", 20000, sink += (MtConnect.ring_query(w), MtConnect.n));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mtconnect")
