// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmarks for the request path (docs/FEATURE_PERFORMANCE.md section 3):
// the standalone HTTP/1.1 request parser and the zero-heap JSON writer/reader. A deterministic
// host ns/op + MB/s baseline that complements the on-device ESP32-S3 numbers; the host figure is
// a relative baseline (a fast RPi core), not the device cost. Build + run (same include roots as
// the native test env):
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENFORCE_HOST_HEADER=0
//   test/performance_benching/network_drivers/presentation/http_parser/host.c
//   src/network_drivers/presentation/http/http_parser/http_parser.c
//   src/network_drivers/presentation/codec/json/json.c src/shared/ip/ip.c
//   src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/br && /tmp/br

#include "network_drivers/presentation/codec/json/json.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

static uint8_t json_work[16]; // the borrow an entry takes; Json never reads it

// A realistic browser GET (request line + 6 headers, no body).
static const char *GET_REQ = "GET /api/v1/status?verbose=1 HTTP/1.1\r\n"
                             "Host: device.local\r\n"
                             "User-Agent: Mozilla/5.0 (X11; Linux x86_64)\r\n"
                             "Accept: application/json,text/html\r\n"
                             "Accept-Encoding: gzip, deflate\r\n"
                             "Connection: keep-alive\r\n"
                             "Cache-Control: no-cache\r\n"
                             "\r\n";

// A JSON POST (request line + 3 headers + a small body), the classic IoT command shape.
static const char *POST_REQ = "POST /api/v1/config HTTP/1.1\r\n"
                              "Host: device.local\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: 50\r\n"
                              "\r\n"
                              "{\"ssid\":\"lab-net\",\"port\":8080,\"tls\":true,\"chan\":6}";

// Feed a whole request string byte-by-byte through the parser (its real per-byte state machine).
static ParseState parse_all(HttpReq *req, const char *s, size_t n)
{
    HttpParser.reset_args.req = req;
    HttpParser.reset(protocore_http_parser_span());
    for (size_t i = 0; i < n; i++)
    {
        HttpParser.feed_args.req = req;
        HttpParser.feed_args.byte = (uint8_t)s[i];
        HttpParser.feed(protocore_http_parser_span());
    }
    return req->parse_state;
}

int main(void)
{
    hbench_header();

    static HttpReq req; // large struct - keep off the stack, mirrors the device's static pool

    // --- HTTP parse: GET (headers only) ---
    {
        const size_t n = strlen(GET_REQ);
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(200000, sink += (int)parse_all(&req, GET_REQ, n), ns);
        hbench_row("http_parse", "GET (6 headers)", ns, (double)n);
        (void)sink;
    }

    // --- HTTP parse: POST + JSON body ---
    {
        const size_t n = strlen(POST_REQ);
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(200000, sink += (int)parse_all(&req, POST_REQ, n), ns);
        hbench_row("http_parse", "POST + JSON body", ns, (double)n);
        (void)sink;
    }

    // --- JSON encode: a typical telemetry response object ---
    {
        char buf[256];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            500000,
            {
                protocore_json_writer w;
                Json.init_args.w = &w;
                Json.init_args.buf = buf;
                Json.init_args.cap = sizeof(buf);
                Json.init(json_work);
                Json.begin_object_args.w = &w;
                Json.begin_object(json_work);
                Json.kv_str_args.w = &w;
                Json.kv_str_args.k = "status";
                Json.kv_str_args.v = "ok";
                Json.kv_str(json_work);
                Json.kv_int_args.w = &w;
                Json.kv_int_args.k = "uptime";
                Json.kv_int_args.v = 123456;
                Json.kv_int(json_work);
                Json.kv_int_args.w = &w;
                Json.kv_int_args.k = "heap";
                Json.kv_int_args.v = 204800;
                Json.kv_int(json_work);
                Json.kv_bool_args.w = &w;
                Json.kv_bool_args.k = "wifi";
                Json.kv_bool_args.v = PROTO_TRUE;
                Json.kv_bool(json_work);
                Json.kv_str_args.w = &w;
                Json.kv_str_args.k = "ip";
                Json.kv_str_args.v = "192.168.1.42";
                Json.kv_str(json_work);
                Json.key_args.w = &w;
                Json.key_args.k = "temps";
                Json.key(json_work);
                Json.begin_array_args.w = &w;
                Json.begin_array(json_work);
                Json.put_int_args.w = &w;
                Json.put_int_args.v = 21;
                Json.put_int(json_work);
                Json.put_int_args.w = &w;
                Json.put_int_args.v = 22;
                Json.put_int(json_work);
                Json.put_int_args.w = &w;
                Json.put_int_args.v = 23;
                Json.put_int(json_work);
                Json.end_array_args.w = &w;
                Json.end_array(json_work);
                Json.end_object_args.w = &w;
                Json.end_object(json_work);
                sink += protocore_json_length(&w);
            },
            ns);
        hbench_row("json", "encode (8 fields)", ns, (double)sink / 500000.0);
        (void)sink;
    }

    // --- JSON decode: top-level field reads over a body ---
    {
        const char *body = "{\"ssid\":\"lab-net\",\"port\":8080,\"tls\":true,\"chan\":6}";
        const size_t n = strlen(body);
        char ssid[33];
        long port = 0;
        long chan = 0;
        proto_bool tls = PROTO_FALSE;
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            500000,
            {
                Json.get_str_args.json = body;
                Json.get_str_args.key = "ssid";
                Json.get_str_args.out = ssid;
                Json.get_str_args.out_cap = sizeof(ssid);
                Json.get_str(json_work);
                Json.get_int_args.json = body;
                Json.get_int_args.key = "port";
                Json.get_int_args.out = &port;
                Json.get_int(json_work);
                Json.get_bool_args.json = body;
                Json.get_bool_args.key = "tls";
                Json.get_bool_args.out = &tls;
                Json.get_bool(json_work);
                Json.get_int_args.json = body;
                Json.get_int_args.key = "chan";
                Json.get_int_args.out = &chan;
                Json.get_int(json_work);
                sink += (size_t)ssid[0] + (size_t)port + (size_t)tls + (size_t)chan;
            },
            ns);
        hbench_row("json", "decode (4 fields)", ns, (double)n);
        (void)sink;
    }

    return 0;
}
