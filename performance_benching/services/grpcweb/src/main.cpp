// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the gRPC-Web message framing codec (services/iot/grpcweb):
// the 5-octet length-prefixed message frame builder (pc_grpcweb_frame_message), the 0x80
// trailers frame builder (pc_grpcweb_frame_trailer -> grpc-status/grpc-message), the frame
// parser (pc_grpcweb_parse) and the trailers-body grpc-status extractor
// (pc_grpcweb_trailer_status). All pure (no sockets, no heap, no HTTP transport): gRPC-Web
// rides the already-shipped HTTP/1.1 server, but that transport half is out of scope here - only
// the deterministic CPU-side framing/parsing is benched, exactly like performance_benching/device/modbus (a pure
// protocol codec, contrast performance_benching/device/ads1115 where the bus transaction is stubbed). Sample
// bytes are copied straight from test/test_grpcweb/test_grpcweb.cpp (already known-good).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/grpcweb -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/grpcweb/grpcweb.h"
#include <Arduino.h>

static void grpcweb_bench_task(void *)
{
    // A small Protobuf message body (field 1 = 150), straight from test_grpcweb.cpp.
    static const uint8_t msg[] = {0x08, 0x96, 0x01};
    static uint8_t framebuf[64];
    static uint8_t trailerbuf[64];
    // A larger message body (256 B) so the frame builder gets a throughput (MB/s) reading, since
    // framing is a 5-octet prefix write + a memcpy of the body.
    static uint8_t bigbody[256];
    static uint8_t bigframe[512];
    for (size_t i = 0; i < sizeof(bigbody); i++)
        bigbody[i] = (uint8_t)(i * 31u + 7u);

    // Pre-build one message frame and one trailers frame to feed the parser / status extractor.
    const size_t msg_frame_len = pc_grpcweb_frame_message(framebuf, sizeof(framebuf), msg, sizeof(msg), false);
    const size_t trailer_len = pc_grpcweb_frame_trailer(trailerbuf, sizeof(trailerbuf), 0, "OK");

    // Parse the trailers frame once so the status extractor benches against its real body slice
    // (body points INTO trailerbuf, which is static and lives for the task's lifetime).
    GrpcWebFrame tf;
    size_t tcons = 0;
    pc_grpcweb_parse(trailerbuf, trailer_len, &tf, &tcons);
    const uint8_t *trailer_body = tf.body;
    const size_t trailer_body_len = tf.body_len;

    for (;;)
    {
        Serial.printf("DB ==== grpcweb device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sinksz = 0;
        volatile int sinki = 0;
        GrpcWebFrame f;
        size_t consumed = 0;
        int status = 0;

        DBENCH_OP("pc_grpcweb_frame_message", 200000,
                  sinksz += pc_grpcweb_frame_message(framebuf, sizeof(framebuf), msg, sizeof(msg), false));
        DBENCH_BULK("pc_grpcweb_frame_message 256B", 50000, sizeof(bigbody),
                    sinksz += pc_grpcweb_frame_message(bigframe, sizeof(bigframe), bigbody, sizeof(bigbody), false));
        DBENCH_OP("pc_grpcweb_frame_trailer", 100000,
                  sinksz += pc_grpcweb_frame_trailer(trailerbuf, sizeof(trailerbuf), 0, "OK"));
        DBENCH_OP("pc_grpcweb_parse", 200000,
                  sinksz += pc_grpcweb_parse(framebuf, msg_frame_len, &f, &consumed) ? 1u : 0u);
        DBENCH_OP("pc_grpcweb_trailer_status", 100000,
                  sinki += pc_grpcweb_trailer_status(trailer_body, trailer_body_len, &status) ? 1 : 0);

        (void)sinksz;
        (void)sinki;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: grpcweb device microbench");
    xTaskCreatePinnedToCore(grpcweb_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
