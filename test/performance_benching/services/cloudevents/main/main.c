// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CloudEvents v1.0 envelope (services/iot/cloudevents):
// CloudEvents.build_structured() (the structured-JSON builder, over the JSON writer) and
// CloudEvents.read_binary() (the binary-mode ce-* header reader). Both are pure - no heap,
// no sockets - so, like performance_benching/device/modbus, every call here exercises the real production code
// path. The binary-mode reader operates on an HttpReq already parsed by the (equally pure)
// standalone HTTP parser; feeding the request bytes happens once outside the timed loop so the
// benched call is CloudEvents.read_binary() itself, not the unrelated byte-at-a-time parser.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/cloudevents -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "services/iot/cloudevents/cloudevents.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void feed_request(uint8_t slot, const char *raw)
{
    http_parser_reset(&http_pool[slot]);
    for (const char *p = raw; *p; p++)
    {
        http_parser_feed(&http_pool[slot], (uint8_t)*p);
    }
}

void dbench_run(void)
{
    // Binary-mode inbound event: parse the ce-* headers once (the byte-at-a-time HTTP parser is
    // out of scope for this bench), then repeatedly read them back off the parsed request.
    feed_request(0, "POST /events HTTP/1.1\r\nHost: x\r\n"
                    "ce-id: abc-1\r\nce-source: /producer\r\nce-type: com.example.test\r\n"
                    "ce-subject: s1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");

    static char buf[256];

    CloudEvents.envelope.out = buf;
    CloudEvents.envelope.cap = sizeof(buf);

    for (;;)
    {
        DBENCH_BANNER("cloudevents");
        volatile size_t sink = 0;
        volatile bool sinkb = false;

        // Minimal event: only the three required context attributes (CloudEvents 1.0).
        CloudEvents.attr.id = "1001";
        CloudEvents.attr.source = "/devices/esp32-1";
        CloudEvents.attr.type = "com.example.sensor.reading";
        CloudEvents.attr.subject = NULL;
        CloudEvents.attr.datacontenttype = NULL;
        CloudEvents.data.json = NULL;
        CloudEvents.data.str = NULL;
        DBENCH_OP("CloudEvents.build_structured min", 50000, CloudEvents.build_structured(CloudEvents.internal);
                  sink += CloudEvents.n);

        // Event carrying a pre-formatted JSON value as data (emitted verbatim, not escaped).
        CloudEvents.attr.id = "7";
        CloudEvents.attr.subject = "temp";
        CloudEvents.data.json = "{\"celsius\":23.5}";
        DBENCH_OP("CloudEvents.build_structured json-data", 50000, CloudEvents.build_structured(CloudEvents.internal);
                  sink += CloudEvents.n);

        // Event carrying a plain string as data (JSON-escaped).
        CloudEvents.attr.id = "8";
        CloudEvents.attr.subject = NULL;
        CloudEvents.attr.datacontenttype = "text/plain";
        CloudEvents.data.json = NULL;
        CloudEvents.data.str = "hi \"there\"";
        DBENCH_OP("CloudEvents.build_structured str-data", 50000, CloudEvents.build_structured(CloudEvents.internal);
                  sink += CloudEvents.n);

        CloudEvents.msg.req = &http_pool[0];
        DBENCH_OP("CloudEvents.read_binary", 50000, CloudEvents.read_binary(CloudEvents.internal);
                  sinkb = CloudEvents.ok);

        (void)sink;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("cloudevents")
