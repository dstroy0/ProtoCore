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
    MtConnectV.streams.next_seq = 1;
    MtConnect.ring_init(w);
    MtConnectV.obs.cat = PROTOCORE_MTC_SAMPLE;
    MtConnectV.obs.type = "Position";
    MtConnectV.obs.data_id = "xpos";
    MtConnectV.obs.timestamp = "T1";
    MtConnectV.obs.value = "1.0";
    MtConnect.ring_add(w);
    MtConnectV.obs.timestamp = "T2";
    MtConnectV.obs.value = "2.0";
    MtConnect.ring_add(w);
    MtConnectV.obs.cat = PROTOCORE_MTC_EVENT;
    MtConnectV.obs.type = "Execution";
    MtConnectV.obs.data_id = "exec";
    MtConnectV.obs.timestamp = "T3";
    MtConnectV.obs.value = "ACTIVE";
    MtConnect.ring_add(w);

    // What every timed document carries. Staged once above the loop: none of it varies across
    // iterations, so hoisting it keeps the macro timing the framing and not the assignments.
    MtConnectV.doc.out = buf;
    MtConnectV.doc.cap = sizeof(buf);
    MtConnectV.doc.instance_id = 1500;
    MtConnectV.doc.sender = "agent-1";

    for (;;)
    {
        DBENCH_BANNER("mtconnect");
        volatile size_t sink = 0;

        // MTConnectStreams (`current`/`sample`): header + one Event, one Sample, one Condition.
        MtConnectV.streams.next_seq = 42;
        MtConnectV.streams.device_name = "cnc1";
        MtConnectV.streams.device_uuid = "uuid-abc";
        MtConnectV.streams.component = "Device";
        MtConnectV.streams.component_id = "dev1";
        MtConnectV.window.first_seq = 1;
        MtConnectV.window.last_seq = 41;
        MtConnectV.window.buffer_size = PROTOCORE_MTC_SAMPLE_BUFFER;
        DBENCH_OP(
            "MtConnect.streams build", 20000, MtConnect.streams_begin(w); MtConnectV.obs.cat = PROTOCORE_MTC_EVENT;
            MtConnectV.obs.type = "Availability"; MtConnectV.obs.data_id = "avail"; MtConnectV.obs.seq = 40;
            MtConnectV.obs.timestamp = "2026-07-06T00:00:00Z"; MtConnectV.obs.value = "AVAILABLE";
            MtConnect.streams_add(w); MtConnectV.obs.cat = PROTOCORE_MTC_SAMPLE; MtConnectV.obs.type = "Position";
            MtConnectV.obs.data_id = "xpos"; MtConnectV.obs.seq = 41; MtConnectV.obs.timestamp = "2026-07-06T00:00:01Z";
            MtConnectV.obs.value = "12.5"; MtConnect.streams_add(w); MtConnectV.obs.cat = PROTOCORE_MTC_CONDITION;
            MtConnectV.obs.type = "SystemCondition"; MtConnectV.obs.data_id = "sys"; MtConnectV.obs.seq = 42;
            MtConnectV.obs.timestamp = "2026-07-06T00:00:02Z"; MtConnectV.obs.value = "Fault"; MtConnect.streams_add(w);
            MtConnect.streams_end(w); sink += MtConnectV.n);

        // MTConnectDevices (`probe`): the device model with three DataItems.
        MtConnectV.device.device_id = "dev1";
        MtConnectV.device.device_name = "cnc1";
        MtConnectV.device.uuid = "uuid-abc";
        MtConnectV.assets.asset_count = 2;
        MtConnectV.assets.asset_buffer_size = 1024;
        DBENCH_OP("MtConnect.devices probe build", 20000, MtConnect.devices_begin(w);
                  MtConnectV.item.cat = PROTOCORE_MTC_EVENT; MtConnectV.item.id = "avail";
                  MtConnectV.item.type = "Availability"; MtConnectV.item.name = NULL; MtConnectV.item.units = NULL;
                  MtConnect.devices_add(w); MtConnectV.item.cat = PROTOCORE_MTC_SAMPLE; MtConnectV.item.id = "xpos";
                  MtConnectV.item.type = "Position"; MtConnectV.item.name = "Xabs";
                  MtConnectV.item.units = "MILLIMETER"; MtConnect.devices_add(w);
                  MtConnectV.item.cat = PROTOCORE_MTC_CONDITION; MtConnectV.item.id = "sys";
                  MtConnectV.item.type = "SystemCondition"; MtConnectV.item.name = NULL; MtConnectV.item.units = NULL;
                  MtConnect.devices_add(w); MtConnect.devices_end(w); sink += MtConnectV.n);

        // MTConnectAssets (`asset`): one CuttingTool with a ToolLife.
        MtConnectV.tool.asset_id = "tool-1";
        MtConnectV.tool.serial_number = "SN-42";
        MtConnectV.tool.tool_id = "T17";
        MtConnectV.tool.device_uuid = "uuid-abc";
        MtConnectV.tool.timestamp = "2026-07-09T00:00:00Z";
        MtConnectV.tool.cutter_status = "AVAILABLE";
        MtConnectV.life.type = "MINUTES";
        MtConnectV.life.count_direction = "DOWN";
        MtConnectV.life.initial = "100";
        MtConnectV.life.limit = "0";
        MtConnectV.life.value = "42";
        DBENCH_OP("MtConnect.assets build", 20000, MtConnect.assets_begin(w); MtConnect.tool_begin(w);
                  MtConnect.tool_life(w); MtConnect.tool_end(w); MtConnect.assets_end(w); sink += MtConnectV.n);

        // MTConnectError: header + one Error element.
        MtConnectV.err.error_code = "UNSUPPORTED";
        MtConnectV.err.message = "bad path";
        DBENCH_OP("MtConnect.error build", 50000, sink += (MtConnect.error(w), MtConnectV.n));

        // Long-poll `sample` cursor: replay the whole retained window as an MTConnectStreams document.
        MtConnectV.streams.device_name = "cnc1";
        MtConnectV.query.from = 1;
        MtConnectV.query.count = 10;
        DBENCH_OP("MtConnect.ring_query", 20000, sink += (MtConnect.ring_query(w), MtConnectV.n));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mtconnect")
