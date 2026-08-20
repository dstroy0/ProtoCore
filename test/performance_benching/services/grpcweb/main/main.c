// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the gRPC-Web message framing codec (services/iot/grpcweb):
// the 5-octet length-prefixed message frame builder (GrpcWeb.frame_message), the 0x80
// trailers frame builder (GrpcWeb.frame_trailers -> grpc-status/grpc-message), the frame
// parser (GrpcWeb.parse) and the trailers-body grpc-status extractor
// (GrpcWeb.trailers_status). All pure (no sockets, no heap, no HTTP transport): gRPC-Web
// rides the already-shipped HTTP/1.1 server, but that transport half is out of scope here - only
// the deterministic CPU-side framing/parsing is benched, exactly like performance_benching/device/modbus (a pure
// protocol codec, contrast performance_benching/device/ads1115 where the bus transaction is stubbed). Sample
// bytes are copied straight from test/test_grpcweb/test_grpcweb.cpp (already known-good).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/grpcweb -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/grpcweb/grpcweb.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t grpcweb_work[16]; // the borrow an entry takes; GrpcWeb never reads it

/** @brief Frame @p body as a Length-Prefixed-Message into @p out; the octets written. */
static size_t gw_frame_message(uint8_t *out, size_t cap, const uint8_t *body, size_t len, proto_bool compressed)
{
    GrpcWeb.out.buf = out;
    GrpcWeb.out.cap = cap;
    GrpcWeb.msg.body = body;
    GrpcWeb.msg.body_len = len;
    GrpcWeb.msg.compressed = compressed;
    GrpcWeb.frame_message(grpcweb_work);
    return GrpcWeb.n;
}

/** @brief Frame a trailer-section carrying @p status and @p message into @p out; the octets written. */
static size_t gw_frame_trailers(uint8_t *out, size_t cap, int32_t status, const char *message)
{
    GrpcWeb.out.buf = out;
    GrpcWeb.out.cap = cap;
    GrpcWeb.trailers.status = status;
    GrpcWeb.trailers.message = message;
    GrpcWeb.frame_trailers(grpcweb_work);
    return GrpcWeb.n;
}

/** @brief Decode the frame at the head of @p data into @p f; whether one was there. */
static proto_bool gw_parse(GrpcWebFrame *f, const uint8_t *data, size_t len)
{
    GrpcWeb.in.data = data;
    GrpcWeb.in.len = len;
    GrpcWeb.parse(grpcweb_work);
    *f = GrpcWeb.parsed;
    return GrpcWeb.ok;
}

/** @brief Read "grpc-status" out of the trailer-section at @p data into @p status. */
static proto_bool gw_trailers_status(const uint8_t *data, size_t len, int32_t *status)
{
    GrpcWeb.in.data = data;
    GrpcWeb.in.len = len;
    GrpcWeb.trailers_status(grpcweb_work);
    *status = GrpcWeb.i32;
    return GrpcWeb.ok;
}

void dbench_run(void)
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
    {
        bigbody[i] = (uint8_t)(i * 31u + 7u);
    }

    // Pre-build one message frame and one trailers frame to feed the parser / status extractor.
    const size_t msg_frame_len = gw_frame_message(framebuf, sizeof(framebuf), msg, sizeof(msg), PROTO_FALSE);
    const size_t trailer_len = gw_frame_trailers(trailerbuf, sizeof(trailerbuf), 0, "OK");

    // Parse the trailers frame once so the status extractor benches against its real body slice
    // (body points INTO trailerbuf, which is static and lives for the task's lifetime).
    GrpcWebFrame tf;
    gw_parse(&tf, trailerbuf, trailer_len);
    const uint8_t *trailer_body = tf.body;
    const size_t trailer_body_len = tf.body_len;

    for (;;)
    {
        DBENCH_BANNER("grpcweb");
        volatile size_t sinksz = 0;
        volatile int sinki = 0;
        GrpcWebFrame f;
        int32_t status = 0;

        DBENCH_OP("GrpcWeb.frame_message", 200000,
                  sinksz += gw_frame_message(framebuf, sizeof(framebuf), msg, sizeof(msg), PROTO_FALSE));
        DBENCH_BULK("GrpcWeb.frame_message 256B", 50000, sizeof(bigbody),
                    sinksz += gw_frame_message(bigframe, sizeof(bigframe), bigbody, sizeof(bigbody), PROTO_FALSE));
        DBENCH_OP("GrpcWeb.frame_trailers", 100000,
                  sinksz += gw_frame_trailers(trailerbuf, sizeof(trailerbuf), 0, "OK"));
        DBENCH_OP("GrpcWeb.parse", 200000, sinksz += gw_parse(&f, framebuf, msg_frame_len) ? 1u : 0u);
        DBENCH_OP("GrpcWeb.trailers_status", 100000,
                  sinki += gw_trailers_status(trailer_body, trailer_body_len, &status) ? 1 : 0);

        (void)sinksz;
        (void)sinki;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("grpcweb")
