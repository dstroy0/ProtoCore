// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the STOMP 1.2 frame codec: pc_stomp_build_frame (the device emits a SEND) and
// pc_stomp_parse_frame (decode one inbound broker frame - command + headers + content-length body, the
// untrusted-input hot op). Both pure (no sockets, no heap), so they link standalone. The device figure comes
// from the rig /bench pc_stomp_parse_frame op; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPC_ENABLE_STOMP=1 test/performance_benching/services/stomp/host.c
//   src/services/iot/stomp/stomp.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bstomp && /tmp/bstomp

#define PC_ENABLE_STOMP 1
#include "services/iot/stomp/stomp.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    const char *body = "hello-from-pc-rig";
    const size_t blen = strlen(body);
    char frame[384];
    const char *bk[] = {"destination", "content-length"};
    const char *bv[] = {"/topic/pc", "20"};
    size_t flen = pc_stomp_build_frame(frame, sizeof(frame), "SEND", bk, bv, 2, body, blen);

    // A representative inbound MESSAGE frame (what a subscriber receives). Ends at the NUL.
    const char msg[] = "MESSAGE\ndestination:/topic/pc\nmessage-id:007\nsubscription:0\n"
                       "content-length:20\n\nhello-from-pc-rig";
    const size_t mlen = sizeof(msg); // include the terminating NUL that ends the STOMP body

    hbench_header();

    // build a SEND frame (command + 2 escaped headers + body + NUL).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000, sink += pc_stomp_build_frame(frame, sizeof(frame), "SEND", bk, bv, 2, body, blen), ns);
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
                size_t used = 0;
                sink += pc_stomp_parse_frame(msg, mlen, &f, &used) ? (int)used : 0;
            },
            ns);
        hbench_row("stomp", "parse MESSAGE frame", ns, (double)mlen);
        (void)sink;
    }

    return 0;
}
