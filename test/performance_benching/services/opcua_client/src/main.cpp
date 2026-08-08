// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OPC UA Binary client codec (services/fieldbus/opcua_client): the
// request builders a client sends (Hello, OpenSecureChannel, Read) and the response parsers it runs
// (ReadResponse -> Variants, BrowseResponse -> references). All of it is transport-agnostic - every
// function works on a caller-supplied byte buffer, so like services/fieldbus/modbus this is a pure protocol
// codec with no hardware involved: this rig has no socket, and none is needed. Every call here is the
// real production code path.
//
// The response buffers the parsers consume are prepared once, before the timing loop, by driving the
// matching services/fieldbus/opcua *server* builders (pc_opcua_build_read_response / _browse_response) with
// the same known-good, spec-conformant sample data as test/test_opcua_client - that is how the host
// test manufactures a wire-accurate reply in-process, and it keeps every parse a self-contained,
// deterministic measurement. Out of scope: pc_opcua_rx() (the ESP32 TCP data handler) and any real
// socket I/O - the transport is the application's job, not this codec's.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/opcua_client -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/opcua/opcua.h"
#include "services/fieldbus/opcua_client/opcua_client.h"
#include <Arduino.h>

// Browse resolver the server-side builder calls to manufacture a realistic BrowseResponse (two
// forward references off the Objects folder) - copied verbatim from test/test_opcua_client.
static int32_t srv_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    if (ns == 0 && id == 85 && max >= 2)
    {
        const char *names[2] = {"Uptime", "Temperature"};
        for (int i = 0; i < 2; i++)
        {
            out[i].ref_type_id = OPCUA_REFTYPE_ORGANIZES;
            out[i].is_forward = true;
            out[i].target_ns = 1;
            out[i].target_id = i + 1;
            out[i].browse_name_ns = 1;
            out[i].browse_name = names[i];
            out[i].display_name = names[i];
            out[i].node_class = OPCUA_NODECLASS_VARIABLE;
            out[i].type_def_id = OPCUA_TYPEDEF_BASE_DATA_VARIABLE;
        }
        return 2;
    }
    return -1;
}

static void opcua_client_bench_task(void *)
{
    OpcUaClient c;
    pc_opcua_client_init(&c);
    c.token_id = 88; // pretend a SecureChannel is open (the builders stamp it into each MSG)

    // Two nodes to read (ns=1 ids 1,2, the Value attribute) - the request the Read builder frames.
    static const OpcUaReadItem items[2] = {{1, 1, true, OPCUA_ATTR_VALUE}, {1, 2, true, OPCUA_ATTR_VALUE}};
    static uint8_t reqbuf[256]; // scratch the builders write into

    // ---- Prepare a wire-accurate ReadResponse for the on_read parser (server builder, once) ----
    static uint8_t read_resp[256];
    size_t read_resp_len = 0;
    {
        uint8_t rq[256];
        size_t rn = pc_opcua_client_read(&c, items, 2, rq, sizeof(rq));
        OpcUaReadRequest rr;
        if (rn > 0 && pc_opcua_parse_read(rq, rn, &rr))
        {
            OpcUaVariant sv[2];
            uint32_t ss[2];
            memset(sv, 0, sizeof(sv));
            sv[0].type = OpcUaVariantType::OPCUA_VAR_UINT32;
            sv[0].u32 = 4242;
            ss[0] = OPCUA_STATUS_GOOD;
            sv[1].type = OpcUaVariantType::OPCUA_VAR_DOUBLE;
            sv[1].f64 = 2.5;
            ss[1] = OPCUA_STATUS_GOOD;
            read_resp_len = pc_opcua_build_read_response(&rr, sv, ss, 3, 0, read_resp, sizeof(read_resp));
        }
    }

    // ---- Prepare a wire-accurate BrowseResponse for the on_browse parser (server builder, once) ----
    static uint8_t browse_resp[512];
    size_t browse_resp_len = 0;
    {
        uint8_t bq[256];
        size_t bn = pc_opcua_client_browse(&c, 0, 85, bq, sizeof(bq));
        OpcUaBrowseRequest br;
        if (bn > 0 && pc_opcua_parse_browse(bq, bn, &br))
        {
            browse_resp_len = pc_opcua_build_browse_response(&br, srv_browse, 4, 0, browse_resp, sizeof(browse_resp));
        }
    }

    for (;;)
    {
        Serial.printf("DB ==== opcua_client device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile int32_t isink = 0;

        // Request builders (little-endian encode into a caller buffer, then patch the frame size).
        DBENCH_OP("pc_opcua_client_hello", 20000,
                  sink += pc_opcua_client_hello("opc.tcp://host:4840", reqbuf, sizeof(reqbuf)));
        DBENCH_OP("pc_opcua_client_open", 20000, sink += pc_opcua_client_open(&c, reqbuf, sizeof(reqbuf)));
        DBENCH_OP("pc_opcua_client_read x2", 20000, sink += pc_opcua_client_read(&c, items, 2, reqbuf, sizeof(reqbuf)));

        // Response parsers (validate the UACP + MSG envelope, then decode the service body).
        OpcUaVariant cvals[2];
        uint32_t csts[2];
        DBENCH_OP("pc_opcua_client_on_read x2", 20000,
                  isink += pc_opcua_client_on_read(read_resp, read_resp_len, cvals, csts, 2));
        OpcUaClientRef refs[4];
        DBENCH_OP("pc_opcua_client_on_browse", 20000,
                  isink += pc_opcua_client_on_browse(browse_resp, browse_resp_len, refs, 4));

        (void)sink;
        (void)isink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: opcua_client device microbench");
    xTaskCreatePinnedToCore(opcua_client_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
