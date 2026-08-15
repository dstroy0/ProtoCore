// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the NATS client codec: pc_nats_build_pub (the device publishes) and pc_nats_parse
// (decode one inbound server frame - the untrusted-input hot op). Both pure (no sockets, no heap), so they
// link standalone. The device figure comes from the rig /bench pc_nats_parse op; this host ns/op + MB/s is a
// relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_NATS=1 test/performance_benching/services/nats/host.c
//   src/services/iot/nats/nats.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bnats && /tmp/bnats

#define PROTOCORE_ENABLE_NATS 1
#include "services/iot/nats/nats.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    const uint8_t payload[] = "{\"v\":21.4,\"u\":\"C\",\"ts\":1720700000}";
    const size_t plen = sizeof(payload) - 1;
    char pub[128];
    size_t publen = pc_nats_build_pub(pub, sizeof(pub), "factory.line1.temp", NULL, payload, plen);

    // A representative inbound MSG frame (what a subscriber receives).
    const char msg[] = "MSG factory.line1.temp 1 34\r\n{\"v\":21.4,\"u\":\"C\",\"ts\":1720700000}\r\n";
    const size_t mlen = sizeof(msg) - 1;

    hbench_header();

    // build a PUB frame.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(3000000, sink += pc_nats_build_pub(pub, sizeof(pub), "factory.line1.temp", NULL, payload, plen), ns);
        hbench_row("nats", "build PUB", ns, (double)publen);
        (void)sink;
    }
    // parse an inbound MSG frame (control line + payload).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            3000000,
            {
                NatsMsg m;
                size_t used = 0;
                sink += pc_nats_parse(msg, mlen, &m, &used) ? (int)used : 0;
            },
            ns);
        hbench_row("nats", "parse MSG", ns, (double)mlen);
        (void)sink;
    }

    return 0;
}
