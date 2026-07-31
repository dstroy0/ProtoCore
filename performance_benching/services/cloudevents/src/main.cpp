// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CloudEvents v1.0 envelope (services/iot/cloudevents):
// pc_cloudevents_build_json() (the structured-JSON builder, over the JSON writer) and
// pc_cloudevents_from_headers() (the binary-mode ce-* header reader). Both are pure - no heap,
// no sockets - so, like performance_benching/device/modbus, every call here exercises the real production code
// path. The binary-mode reader operates on an HttpReq already parsed by the (equally pure)
// standalone HTTP parser; feeding the request bytes happens once outside the timed loop so the
// benched call is pc_cloudevents_from_headers() itself, not the unrelated byte-at-a-time parser.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/cloudevents -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "services/iot/cloudevents/cloudevents.h"
#include <Arduino.h>

static void feed_request(uint8_t slot, const char *raw)
{
    http_parser_reset(&http_pool[slot]);
    for (const char *p = raw; *p; p++)
        http_parser_feed(&http_pool[slot], (uint8_t)*p);
}

static void cloudevents_bench_task(void *)
{
    // Minimal event: only the three required context attributes (CloudEvents 1.0).
    CloudEvent ce_minimal = {};
    ce_minimal.id = "1001";
    ce_minimal.source = "/devices/esp32-1";
    ce_minimal.type = "com.example.sensor.reading";

    // Event carrying a pre-formatted JSON value as data (emitted verbatim, not escaped).
    CloudEvent ce_json_data = {};
    ce_json_data.id = "7";
    ce_json_data.source = "/devices/esp32-1";
    ce_json_data.type = "com.example.sensor.reading";
    ce_json_data.subject = "temp";
    ce_json_data.data_json = "{\"celsius\":23.5}";

    // Event carrying a plain string as data (JSON-escaped).
    CloudEvent ce_str_data = {};
    ce_str_data.id = "8";
    ce_str_data.source = "/devices/esp32-1";
    ce_str_data.type = "com.example.sensor.reading";
    ce_str_data.datacontenttype = "text/plain";
    ce_str_data.data_str = "hi \"there\"";

    // Binary-mode inbound event: parse the ce-* headers once (the byte-at-a-time HTTP parser is
    // out of scope for this bench), then repeatedly read them back off the parsed request.
    feed_request(0, "POST /events HTTP/1.1\r\nHost: x\r\n"
                    "ce-id: abc-1\r\nce-source: /producer\r\nce-type: com.example.test\r\n"
                    "ce-subject: s1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");

    static char buf[256];
    CloudEvent ce_out;

    for (;;)
    {
        Serial.printf("DB ==== cloudevents device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool sinkb = false;
        DBENCH_OP("pc_cloudevents_build_json min", 50000,
                  sink += pc_cloudevents_build_json(buf, sizeof(buf), &ce_minimal));
        DBENCH_OP("pc_cloudevents_build_json json-data", 50000,
                  sink += pc_cloudevents_build_json(buf, sizeof(buf), &ce_json_data));
        DBENCH_OP("pc_cloudevents_build_json str-data", 50000,
                  sink += pc_cloudevents_build_json(buf, sizeof(buf), &ce_str_data));
        DBENCH_OP("pc_cloudevents_from_headers", 50000, sinkb = pc_cloudevents_from_headers(&http_pool[0], &ce_out));
        (void)sink;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: cloudevents device microbench");
    xTaskCreatePinnedToCore(cloudevents_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
