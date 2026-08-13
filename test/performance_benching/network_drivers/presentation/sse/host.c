// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the Server-Sent Events framing hot op (sse_format), the pure
// presentation-layer record builder that runs on every sse_send()/sse_broadcast(). The device
// number comes from the rig /bench endpoint; this host ns/op + MB/s is a relative baseline (a fast
// RPi core), not the device cost. protocore_sse_format() is pure (no transport), so it links standalone.
// Build + run (same include roots as the native test env):
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_SSE=1 test/performance_benching/network_drivers/presentation/sse/host.c
//   src/network_drivers/presentation/http/sse/sse.c src/mmgr/protomem.c src/mmgr/protostr.c
//   -o /tmp/bs && /tmp/bs

#define PROTOCORE_ENABLE_SSE 1
#include "network_drivers/presentation/http/sse/sse.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// sse.c's protocore_sse_write() (not exercised here) references the transport layer; the bench only calls
// the pure protocore_sse_format(), so satisfy the linker with stubs rather than pulling in transport + lwIP.
TcpConn conn_pool[CONN_POOL_SLOTS];
const TcpNs Tcp = {0};

bool protocore_conn_send(uint8_t slot, const void *data, uint16_t len)
{
    (void)slot;
    (void)data;
    (void)len;
    return true;
}

int main(void)
{
    hbench_header();

    char buf[SSE_BUF_SIZE];

    // data-only: the common broadcast shape (`data: <payload>\n\n`).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000, sink += protocore_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", NULL, NULL), ns);
        int bytes = protocore_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", NULL, NULL);
        hbench_row("sse", "format data-only", ns, (double)bytes);
        (void)sink;
    }

    // event + id + data: the fully-addressed record (named event, resumable id).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000, sink += protocore_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", "telemetry", "12345"), ns);
        int bytes = protocore_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", "telemetry", "12345");
        hbench_row("sse", "format event+id+data", ns, (double)bytes);
        (void)sink;
    }

    return 0;
}
