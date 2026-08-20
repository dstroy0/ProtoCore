// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t webdav_work[16]; // the borrow an entry takes; Webdav never reads it

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
            WebdavV.ms_begin_args.buf = buf;
            WebdavV.ms_begin_args.cap = sizeof(buf);
            WebdavV.ms_begin_args.len = 0;
            Webdav.ms_begin(webdav_work);
            size_t len = WebdavV.n;
            WebdavV.ms_entry_args.buf = buf;
            WebdavV.ms_entry_args.cap = sizeof(buf);
            WebdavV.ms_entry_args.len = len;
            WebdavV.ms_entry_args.href = "/dav/";
            WebdavV.ms_entry_args.is_collection = true;
            WebdavV.ms_entry_args.size = 0;
            WebdavV.ms_entry_args.rfc1123_mtime = mtime;
            WebdavV.ms_entry_args.content_type = "";
            Webdav.ms_entry(webdav_work);
            len = WebdavV.n;
            WebdavV.ms_entry_args.buf = buf;
            WebdavV.ms_entry_args.cap = sizeof(buf);
            WebdavV.ms_entry_args.len = len;
            WebdavV.ms_entry_args.href = "/dav/sensor-log.csv";
            WebdavV.ms_entry_args.is_collection = false;
            WebdavV.ms_entry_args.size = 12800;
            WebdavV.ms_entry_args.rfc1123_mtime = mtime;
            WebdavV.ms_entry_args.content_type = "text/csv";
            Webdav.ms_entry(webdav_work);
            len = WebdavV.n;
            WebdavV.ms_end_args.buf = buf;
            WebdavV.ms_end_args.cap = sizeof(buf);
            WebdavV.ms_end_args.len = len;
            Webdav.ms_end(webdav_work);
            len = WebdavV.n;
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
