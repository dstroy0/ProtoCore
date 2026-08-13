// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the RFC 5424 syslog client formatter: protocore_syslog_format() builds one
// `<PRI>1 - HOST APP - - - MSG` line into a caller buffer - the per-log-line hot op the device runs before
// each UDP send. Pure (no socket, no heap). syslog.c also holds protocore_syslog_log(), which reaches the
// transport UDP service, so udp.c and its listener/client/net_addr are linked in for the host no-op
// stubs (protocore_syslog_log() is not exercised here - only the pure protocore_syslog_format() is benched).
// The device number comes from the rig /bench protocore_syslog_format op; this host ns/op is a relative baseline:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_SYSLOG=1 test/performance_benching/services/syslog/host.c
//   src/services/net/syslog/syslog.c src/network_drivers/transport/udp.c
//   src/network_drivers/transport/udp/udp_listener.c src/network_drivers/transport/udp/udp_client.c
//   src/network_drivers/transport/net_addr.c src/mmgr/protomem.c src/mmgr/protostr.c
//   src/shared_primitives/ip.c -o /tmp/bsl && /tmp/bsl

#define PROTOCORE_ENABLE_SYSLOG 1
#include "services/net/syslog/syslog.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    char out[256];
    const char *msg = "sensor=21.4C rh=48% link=up heap=131072";
    size_t len = protocore_syslog_format(out, sizeof(out), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "pc-rig", "rig-app", msg);

    hbench_header();

    // format one RFC 5424 line (the per-log-line cost before the UDP send).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000,
                  sink += protocore_syslog_format(out, sizeof(out), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "pc-rig", "rig-app", msg),
                  ns);
        hbench_row("syslog", "protocore_syslog_format (RFC 5424)", ns, (double)len);
        (void)sink;
    }

    return 0;
}
