// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the StatsD metrics client: protocore_statsd_format() builds one `name:value|type
// [|@rate][|#tags]` line - the per-metric hot op the device runs before each protocore_udp_sendto(). Pure (no
// socket, no heap). statsd.c also holds the emit helpers which reference protocore_udp_sendto(), so link udp.c
// for the host no-op UDP stubs. The device number comes from the rig /bench protocore_statsd_format op; this
// host ns/op + MB/s is a relative baseline:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_STATSD=1 test/performance_benching/services/statsd/host.c
//   src/services/iot/statsd/statsd.c src/network_drivers/transport/udp.c
//   src/network_drivers/transport/udp/udp_listener.c src/network_drivers/transport/udp/udp_client.c
//   src/network_drivers/transport/net_addr.c src/mmgr/protomem.c src/shared/ip/ip.c
//   -o /tmp/bst && /tmp/bst

#define PROTOCORE_ENABLE_STATSD 1
#include "services/iot/statsd/statsd.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

/** @brief Format one sampled, tagged counter line into @p out; the octets written. */
static size_t statsd_counter_line(char *out, size_t cap)
{
    Statsd.line.out = out;
    Statsd.line.cap = cap;
    Statsd.metric.name = "api.requests";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.1f;
    Statsd.value.text = "1";
    Statsd.tags.metric = "env:prod,host:pc-rig";
    Statsd.format(Statsd.internal);
    return Statsd.n;
}

int main(void)
{
    char out[256];
    size_t len = statsd_counter_line(out, sizeof(out));

    hbench_header();

    // format one sampled counter line with tags (the per-metric cost before the UDP send).
    volatile size_t sink = 0;
    double ns = 0.0;
    HBENCH_NS(3000000, sink += statsd_counter_line(out, sizeof(out)), ns);
    hbench_row("statsd", "Statsd.format (counter)", ns, (double)len);
    (void)sink;

    return 0;
}
