// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the STOMP 1.2 frame codec: Stomp.build (the device emits a SEND) and
// Stomp.parse (decode one inbound broker frame - command + headers + content-length body, the
// untrusted-input hot op). Both pure (no sockets, no heap), so they link standalone. The device figure comes
// from the rig /bench Stomp.parse op; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_STOMP=1 test/performance_benching/services/stomp/host.c
//   src/services/iot/stomp/stomp.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bstomp && /tmp/bstomp

#define PROTOCORE_ENABLE_STOMP 1
#include "services/iot/stomp/stomp.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

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

int main(void)
{
    const char *body = "hello-from-pc-rig";
    const size_t blen = strlen(body);
    char frame[384];
    const char *const bk[] = {"destination", "content-length"};
    const char *const bv[] = {"/topic/pc", "20"};
    size_t flen = stomp_send_frame(frame, sizeof(frame), bk, bv, body, blen);

    // A representative inbound MESSAGE frame (what a subscriber receives). Ends at the NUL.
    const char msg[] = "MESSAGE\ndestination:/topic/pc\nmessage-id:007\nsubscription:0\n"
                       "content-length:20\n\nhello-from-pc-rig";
    const size_t mlen = sizeof(msg); // include the terminating NUL that ends the STOMP body

    hbench_header();

    // build a SEND frame (command + 2 escaped headers + body + NUL).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000, sink += stomp_send_frame(frame, sizeof(frame), bk, bv, body, blen), ns);
        hbench_row("stomp", "build SEND frame", ns, (double)flen);
        (void)sink;
    }
    // parse an inbound MESSAGE frame (command + 4 headers + content-length body).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            2000000,
            {
                StompFrame f;
                sink += (int)stomp_take_frame(&f, msg, mlen);
            },
            ns);
        hbench_row("stomp", "parse MESSAGE frame", ns, (double)mlen);
        (void)sink;
    }

    return 0;
}
