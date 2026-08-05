// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the outbound HTTP client's pure core (services/net/http_client):
//   - http_client_parse_url()      : split "http[s]://host[:port][/path]" into scheme/host/port/path
//   - http_client_build_request()  : format the HTTP/1.1 request line + headers (+ optional body)
//   - http_client_parse_response() : locate the status code and body, bounded by Content-Length /
//                                    connection-close, or decoded from chunked transfer-encoding.
// These are pure string functions (no sockets, no heap) - the same code the host suite unit-tests in
// test/test_http_client. The lwIP/mbedTLS transport (http_get/http_post, pc_client_*, the TLS BIO)
// is ESP32-only and deliberately OUT OF SCOPE: this rig has no network up, so no real DNS/TCP/TLS
// transaction is ever issued - only the CPU-side codec is timed. (Enabling PC_ENABLE_HTTP_CLIENT
// still compiles + links that transport TU via the Library Dependency Finder; we simply never call
// into it, exactly as bench_modbus/bench_coap keep the wire half stubbed/untouched.)
//
// The chunked-decode parse rewrites its input buffer in place, so that block refreshes a scratch
// copy from a const template each iteration (the memcpy is part of the measured op and noted below);
// the Content-Length parse does not mutate its input, so it reuses one buffer directly.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/http_client -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/http_client/http_client.h"
#include <Arduino.h>

// Known-good, spec-conformant sample data lifted straight out of test/test_http_client.cpp.
static const char *const URL_HTTP = "http://example.com/api/v1"; // scheme + host + path
static const char *const URL_HTTPS = "https://10.0.0.5:8443";    // scheme + host + non-default port
static const char POST_BODY[] = "{\"k\":1}";
// Content-Length framed response (parse does not mutate the buffer).
static const char RESP_CL[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
// Chunked response: two chunks "Wiki"(4) + "pedia"(5) -> "Wikipedia" (in-place decode, needs a reset).
static const char RESP_CHUNKED[] =
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";

static void http_client_bench_task(void *)
{
    // Scratch buffers.
    static char reqbuf[768];    // request-builder output
    static uint8_t clbuf[128];  // Content-Length response (parsed in place, but unmutated)
    static uint8_t chkbuf[128]; // chunked response working copy (rewritten in place each parse)
    const size_t cl_len = sizeof(RESP_CL) - 1;
    const size_t chk_len = sizeof(RESP_CHUNKED) - 1;
    memcpy(clbuf, RESP_CL, cl_len);

    for (;;)
    {
        Serial.printf("DB ==== http_client device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        volatile int sinkb = 0;    // parse_url bool
        volatile size_t sinkn = 0; // build_request byte count
        volatile int sinks = 0;    // parse_response status

        bool is_https;
        char host[80], path[160];
        uint16_t port;
        size_t body_off, body_len;

        // 1) URL parse: http:// with an explicit path.
        DBENCH_OP("parse_url http+path", 100000,
                  sinkb += http_client_parse_url(URL_HTTP, &is_https, host, sizeof(host), &port, path, sizeof(path)));
        // 2) URL parse: https:// with a non-default port, default path.
        DBENCH_OP("parse_url https+port", 100000,
                  sinkb += http_client_parse_url(URL_HTTPS, &is_https, host, sizeof(host), &port, path, sizeof(path)));
        // 3) Request build: GET, no body.
        DBENCH_OP("build_request GET", 50000,
                  sinkn += http_client_build_request("GET", "example.com", 80, "/path", nullptr, nullptr, 0, reqbuf,
                                                     sizeof(reqbuf)));
        // 4) Request build: POST with a JSON body + non-default port (Host carries the port).
        DBENCH_OP("build_request POST+body", 50000,
                  sinkn +=
                  http_client_build_request("POST", "host", 8080, "/ingest", "application/json",
                                            (const uint8_t *)POST_BODY, sizeof(POST_BODY) - 1, reqbuf, sizeof(reqbuf)));
        // 5) Response parse: Content-Length framed (buffer is not mutated -> reuse directly).
        DBENCH_OP("parse_response ContentLength", 100000,
                  sinks += http_client_parse_response(clbuf, cl_len, &body_off, &body_len));
        // 6) Response parse: chunked transfer-decode. The decode rewrites the body in place, so a
        //    fresh copy is staged each iteration; the small memcpy is part of the measured op.
        DBENCH_OP("parse_response chunked(+copy)", 50000,
                  (memcpy(chkbuf, RESP_CHUNKED, chk_len),
                   sinks += http_client_parse_response(chkbuf, chk_len, &body_off, &body_len)));

        (void)sinkb;
        (void)sinkn;
        (void)sinks;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: http_client device microbench");
    xTaskCreatePinnedToCore(http_client_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
