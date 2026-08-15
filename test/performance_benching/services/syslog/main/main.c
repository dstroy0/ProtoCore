// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the RFC 5424 syslog formatter (services/net/syslog):
// Syslog.format builds one `<PRI>1 - HOST APP - - - MSG` line into a caller buffer - the
// per-log-line hot op before each UDP send. Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/syslog -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/syslog/syslog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Format one local0/info SYSLOG-MSG carrying @p msg into @p out; the octets written. */
static size_t syslog_line(char *out, size_t cap, const char *msg)
{
    Syslog.line.out = out;
    Syslog.line.cap = cap;
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.header.hostname = "pc-rig";
    Syslog.header.app_name = "rig-app";
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.record.msg = msg;
    Syslog.format(Syslog.internal);
    return Syslog.n;
}

void dbench_run(void)
{
    static const char *msg = "sensor=21.4C rh=48% link=up heap=131072";

    for (;;)
    {
        DBENCH_BANNER("syslog");
        volatile size_t sink = 0;
        static char out[256];
        DBENCH_OP("Syslog.format (RFC 5424)", 200000, sink += syslog_line(out, sizeof(out), msg));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("syslog")
