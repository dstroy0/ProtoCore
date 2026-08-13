// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
//   src/network_drivers/presentation/codec/json/json.c src/shared_primitives/ip.c
//   src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/br && /tmp/br

#include "network_drivers/presentation/codec/json/json.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

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
    http_parser_reset(req);
    for (size_t i = 0; i < n; i++)
    {
        http_parser_feed(req, (uint8_t)s[i]);
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
                Json.init(&w, buf, sizeof(buf));
                Json.begin_object(&w);
                Json.kv_str(&w, "status", "ok");
                Json.kv_int(&w, "uptime", 123456);
                Json.kv_int(&w, "heap", 204800);
                Json.kv_bool(&w, "wifi", PROTO_TRUE);
                Json.kv_str(&w, "ip", "192.168.1.42");
                Json.key(&w, "temps");
                Json.begin_array(&w);
                Json.put_int(&w, 21);
                Json.put_int(&w, 22);
                Json.put_int(&w, 23);
                Json.end_array(&w);
                Json.end_object(&w);
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
                Json.get_str(body, "ssid", ssid, sizeof(ssid));
                Json.get_int(body, "port", &port);
                Json.get_bool(body, "tls", &tls);
                Json.get_int(body, "chan", &chan);
                sink += (size_t)ssid[0] + (size_t)port + (size_t)tls + (size_t)chan;
            },
            ns);
        hbench_row("json", "decode (4 fields)", ns, (double)n);
        (void)sink;
    }

    return 0;
}
