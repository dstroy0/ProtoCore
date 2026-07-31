// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmarks for the request path (docs/FEATURE_PERFORMANCE.md section 3):
// the standalone HTTP/1.1 request parser and the zero-heap JSON writer/reader. A deterministic
// host ns/op + MB/s baseline that complements the on-device ESP32-S3 numbers; the host figure is
// a RELATIVE baseline (a fast RPi core), not the device cost. Build + run (same include roots as
// the native test env: test/mocks for Arduino.h, test/support, src):
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENFORCE_HOST_HEADER=0 \
//       performance_benching/network_drivers/presentation/http_parser/host.cpp \
//       src/network_drivers/presentation/http/http_parser/http_parser.cpp \
//       src/network_drivers/network/ip.cpp \
//       src/network_drivers/presentation/codec/json/json.cpp -o /tmp/br && /tmp/br

#include "network_drivers/presentation/codec/json/json.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

// Time `iters` calls of fn(), return ns per op.
template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
        fn();
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    if (bytes_per_op > 0)
        printf("| %-12s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
    else
        printf("| %-12s | %-24s | %10.1f | %9s |\n", feature, op, ns_per_op, "-");
}

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
        http_parser_feed(req, (uint8_t)s[i]);
    return req->parse_state;
}

int main()
{
    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    static HttpReq req; // large struct - keep off the stack, mirrors the device's static pool

    // --- HTTP parse: GET (headers only) ---
    {
        const size_t n = strlen(GET_REQ);
        volatile int sink = 0;
        double ns = bench_ns(200000, [&] { sink += (int)parse_all(&req, GET_REQ, n); });
        row("http_parse", "GET (6 headers)", ns, (double)n);
        (void)sink;
    }

    // --- HTTP parse: POST + JSON body ---
    {
        const size_t n = strlen(POST_REQ);
        volatile int sink = 0;
        double ns = bench_ns(200000, [&] { sink += (int)parse_all(&req, POST_REQ, n); });
        row("http_parse", "POST + JSON body", ns, (double)n);
        (void)sink;
    }

    // --- JSON encode: a typical telemetry response object ---
    {
        char buf[256];
        volatile size_t sink = 0;
        double ns = bench_ns(500000, [&] {
            JsonWriter w(buf, sizeof(buf));
            w.begin_object();
            w.kv_str("status", "ok");
            w.kv_int("uptime", 123456);
            w.kv_int("heap", 204800);
            w.kv_bool("wifi", true);
            w.kv_str("ip", "192.168.1.42");
            w.key("temps");
            w.begin_array();
            w.integer(21);
            w.integer(22);
            w.integer(23);
            w.end_array();
            w.end_object();
            sink += w.length();
        });
        row("json", "encode (8 fields)", ns, (double)sink / 500000.0);
        (void)sink;
    }

    // --- JSON decode: top-level field reads over a body ---
    {
        const char *body = "{\"ssid\":\"lab-net\",\"port\":8080,\"tls\":true,\"chan\":6}";
        const size_t n = strlen(body);
        char ssid[33];
        long port = 0, chan = 0;
        bool tls = false;
        volatile size_t sink = 0;
        double ns = bench_ns(500000, [&] {
            json_get_str(body, "ssid", ssid, sizeof(ssid));
            json_get_int(body, "port", &port);
            json_get_bool(body, "tls", &tls);
            json_get_int(body, "chan", &chan);
            sink += (size_t)ssid[0] + (size_t)port + (size_t)tls + (size_t)chan;
        });
        row("json", "decode (4 fields)", ns, (double)n);
        (void)sink;
    }

    return 0;
}
