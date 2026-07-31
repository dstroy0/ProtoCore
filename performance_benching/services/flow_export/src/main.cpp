// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the flow-record export codec (services/net/flow_export):
// NetFlow v5 fixed header/record builders, plus the NetFlow v9 / IPFIX template-then-data
// cursor (flow_*_begin -> flow_export_template -> flow_export_data_begin ->
// flow_export_data_record -> flow_export_finish, which patches the message length/count and,
// for v9, pads each FlowSet to a 4-octet boundary). Worked example for performance_benching/device/<service>/:
// a pure protocol codec with no hardware involved (same category as performance_benching/device/modbus) - every
// call here exercises the real production code path against wire-byte-verified sample data
// lifted straight from test/test_flow_export/test_flow_export.cpp. Out of scope: the flow
// cache (5-tuple + counters) and the UDP send itself (pc_udp_sendto), which are the app's
// concern, not this codec's.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/flow_export -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/flow_export/flow_export.h"
#include <Arduino.h>

// Wraps the v9/IPFIX cursor's begin->template->data_begin->data_record->finish sequence into a
// single call so it can be dropped straight into DBENCH_OP as one "build a message" operation
// (mirrors test_ipfix_message_bytes / test_v9_count_and_padding in test_flow_export.cpp).

static size_t build_ipfix_message(uint8_t *buf, size_t cap)
{
    FlowWriter w;
    if (!flow_ipfix_begin(&w, buf, cap, 0x11223344, 1, 0x2A))
        return 0;
    static const FlowField fields[] = {{8, 4}, {12, 4}}; // sourceIPv4Address, destinationIPv4Address
    if (!flow_export_template(&w, 256, fields, 2))
        return 0;
    if (!flow_export_data_begin(&w, 256))
        return 0;
    static const uint8_t rec[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    if (!flow_export_data_record(&w, rec, sizeof(rec)))
        return 0;
    return flow_export_finish(&w); // 44 octets
}

static size_t build_v9_message(uint8_t *buf, size_t cap)
{
    FlowWriter w;
    if (!flow_v9_begin(&w, buf, cap, 1000, 0x5F000000, 7, 1))
        return 0;
    static const FlowField fields[] = {{1, 4}, {10, 2}}; // a 6-octet record layout
    if (!flow_export_template(&w, 256, fields, 2))
        return 0;
    if (!flow_export_data_begin(&w, 256))
        return 0;
    static const uint8_t rec[] = {0x00, 0x00, 0x00, 0x64, 0x00, 0x07}; // 6 octets (not 4-aligned)
    if (!flow_export_data_record(&w, rec, sizeof(rec)))
        return 0;
    return flow_export_finish(&w); // 48 octets (incl. 2 pad octets)
}

static void flow_export_bench_task(void *)
{
    static FlowV5Header h = {};
    h.count = 1;
    h.sys_uptime = 1000;
    h.unix_secs = 0x5F5E1100;
    h.unix_nsecs = 0;
    h.flow_sequence = 1;
    h.engine_type = 0;
    h.engine_id = 0;
    h.sampling_interval = 0;

    static FlowV5Record r = {};
    r.src_addr = 0x0A000001;
    r.dst_addr = 0x0A000002;
    r.next_hop = 0x0A0000FE;
    r.input = 1;
    r.output = 2;
    r.d_pkts = 10;
    r.d_octets = 1500;
    r.first = 100;
    r.last = 200;
    r.src_port = 12345;
    r.dst_port = 80;
    r.tcp_flags = 0x18;
    r.prot = 6;
    r.tos = 0;
    r.src_as = 0;
    r.dst_as = 0;
    r.src_mask = 24;
    r.dst_mask = 24;

    static uint8_t hdr_buf[FLOW_V5_HEADER_SIZE];
    static uint8_t rec_buf[FLOW_V5_RECORD_SIZE];
    static uint8_t ipfix_buf[64];
    static uint8_t v9_buf[64];

    for (;;)
    {
        Serial.printf("DB ==== flow_export device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_BULK("flow_v5_write_header", 20000, FLOW_V5_HEADER_SIZE,
                    sink += flow_v5_write_header(hdr_buf, sizeof(hdr_buf), &h));
        DBENCH_BULK("flow_v5_write_record", 20000, FLOW_V5_RECORD_SIZE,
                    sink += flow_v5_write_record(rec_buf, sizeof(rec_buf), &r));
        DBENCH_OP("flow_ipfix_build_message", 20000, sink += build_ipfix_message(ipfix_buf, sizeof(ipfix_buf)));
        DBENCH_OP("flow_v9_build_message", 20000, sink += build_v9_message(v9_buf, sizeof(v9_buf)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: flow_export device microbench");
    xTaskCreatePinnedToCore(flow_export_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
