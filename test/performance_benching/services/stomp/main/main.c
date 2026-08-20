// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the STOMP 1.2 frame codec (services/iot/stomp): the zero-heap
// frame builder (command + escaped headers + NUL-terminated body) and the non-mutating parser.
// Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/stomp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/stomp/stomp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t stomp_work[16]; // the borrow an entry takes; Stomp never reads it

/** @brief Write a SEND frame carrying @p body and the two headers into @p out; the octets written. */
static size_t stomp_send_frame(char *out, size_t cap, const char *const *hk, const char *const *hv, const char *body,
                               size_t blen)
{
    StompV.buf.out = out;
    StompV.buf.cap = cap;
    StompV.build_args.command = "SEND";
    StompV.build_args.header_names = hk;
    StompV.build_args.header_values = hv;
    StompV.build_args.header_count = 2;
    StompV.build_args.body = body;
    StompV.build_args.body_len = blen;
    Stomp.build(stomp_work);
    return StompV.n;
}

/** @brief Take one frame from the head of @p in into @p f; the octets it occupied, 0 on a refusal. */
static size_t stomp_take_frame(StompFrame *f, const char *in, size_t len)
{
    StompV.frame = f;
    StompV.buf.in = in;
    StompV.buf.len = len;
    Stomp.parse(stomp_work);
    return StompV.ok ? StompV.consumed : 0;
}

void dbench_run(void)
{
    static const char body[] = "hello-from-pc-rig";
    const size_t blen = sizeof(body) - 1;
    static const char *const bk[] = {"destination", "content-length"};
    static const char *const bv[] = {"/topic/pc", "20"};
    // A representative inbound MESSAGE frame (what a subscriber receives). Ends at the NUL.
    static const char msg[] = "MESSAGE\ndestination:/topic/pc\nmessage-id:007\nsubscription:0\n"
                              "content-length:18\n\nhello-from-pc-rig";
    const size_t mlen = sizeof(msg);

    for (;;)
    {
        DBENCH_BANNER("stomp");
        volatile size_t sink = 0;
        static char frame[384];
        DBENCH_OP("Stomp.build (SEND)", 200000, sink += stomp_send_frame(frame, sizeof(frame), bk, bv, body, blen));
        DBENCH_OP("Stomp.parse (MESSAGE)", 200000, {
            StompFrame f;
            sink += stomp_take_frame(&f, msg, mlen);
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("stomp")
