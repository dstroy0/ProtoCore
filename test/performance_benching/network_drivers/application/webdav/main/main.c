// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the WebDAV multi-status builder (network_drivers/application/webdav): the
// PROPFIND 207 Multi-Status response assembly (protocore_webdav_ms_begin / _entry / _end) and the XML
// escaper. Pure string logic - the FS traversal is elsewhere; only the per-response codec is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/webdav -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/webdav/webdav.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const char *mtime = "Mon, 07 Jul 2026 12:00:00 GMT";

    for (;;)
    {
        DBENCH_BANNER("webdav");
        volatile size_t sink = 0;
        static char buf[4096];
        DBENCH_OP("protocore_webdav_ms_entry (1 file)", 100000,
                  sink +=
                  protocore_webdav_ms_entry(buf, sizeof(buf), 0, "/dav/report.txt", false, 4096, mtime, "text/plain"));
        DBENCH_OP("protocore_webdav propfind (dir+2)", 100000, {
            size_t len = protocore_webdav_ms_begin(buf, sizeof(buf), 0);
            len = protocore_webdav_ms_entry(buf, sizeof(buf), len, "/dav/", true, 0, mtime, "");
            len = protocore_webdav_ms_entry(buf, sizeof(buf), len, "/dav/sensor-log.csv", false, 12800, mtime,
                                            "text/csv");
            len = protocore_webdav_ms_end(buf, sizeof(buf), len);
            sink += len;
        });
        static char esc[256];
        DBENCH_OP("protocore_webdav_xml_escape", 200000,
                  sink += protocore_webdav_xml_escape(esc, sizeof(esc), "/dav/a&b<c>\"d'e.txt"));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("webdav")
