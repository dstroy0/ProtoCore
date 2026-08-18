// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the WebDAV 207 Multi-Status builder (RFC 4918) - the pure,
// transport/filesystem-free hot op that runs on every PROPFIND response (one protocore_webdav_ms_entry per
// directory child) plus the XML escaping on each href/property. The device number comes from the rig
// /bench endpoint; this host ns/op + MB/s is a relative baseline (a fast RPi core), not the device
// cost. The 207 builder is pure, so it links standalone. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_FILE_SERVING=1 -DPROTOCORE_ENABLE_WEBDAV=1
//   test/performance_benching/network_drivers/application/webdav/host.c
//   src/network_drivers/application/webdav/webdav.c -o /tmp/bw && /tmp/bw

#define PROTOCORE_ENABLE_FILE_SERVING 1
#define PROTOCORE_ENABLE_WEBDAV 1
#include "network_drivers/application/webdav/webdav.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t webdav_work[16]; // the borrow an entry takes; Webdav never reads it

int main(void)
{
    hbench_header();

    static char buf[8192];
    const char *mtime = "Mon, 07 Jul 2026 12:00:00 GMT";

    // One <response> entry for a file (the per-child PROPFIND hot op).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            1000000,
            {
                Webdav.ms_entry_args.buf = buf;
                Webdav.ms_entry_args.cap = sizeof(buf);
                Webdav.ms_entry_args.len = 0;
                Webdav.ms_entry_args.href = "/dav/report.txt";
                Webdav.ms_entry_args.is_collection = false;
                Webdav.ms_entry_args.size = 4096;
                Webdav.ms_entry_args.rfc1123_mtime = mtime;
                Webdav.ms_entry_args.content_type = "text/plain";
                Webdav.ms_entry(webdav_work);
                size_t n = Webdav.n;
                sink += n;
            },
            ns);
        Webdav.ms_entry_args.buf = buf;
        Webdav.ms_entry_args.cap = sizeof(buf);
        Webdav.ms_entry_args.len = 0;
        Webdav.ms_entry_args.href = "/dav/report.txt";
        Webdav.ms_entry_args.is_collection = false;
        Webdav.ms_entry_args.size = 4096;
        Webdav.ms_entry_args.rfc1123_mtime = mtime;
        Webdav.ms_entry_args.content_type = "text/plain";
        Webdav.ms_entry(webdav_work);
        size_t bytes = Webdav.n;
        hbench_row("webdav", "ms_entry file", ns, (double)bytes);
        (void)sink;
    }

    // A whole Depth-1 directory listing: prolog + 8 children + epilog (a realistic PROPFIND body).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            200000,
            {
                Webdav.ms_begin_args.buf = buf;
                Webdav.ms_begin_args.cap = sizeof(buf);
                Webdav.ms_begin_args.len = 0;
                Webdav.ms_begin(webdav_work);
                size_t len = Webdav.n;
                Webdav.ms_entry_args.buf = buf;
                Webdav.ms_entry_args.cap = sizeof(buf);
                Webdav.ms_entry_args.len = len;
                Webdav.ms_entry_args.href = "/dav/";
                Webdav.ms_entry_args.is_collection = true;
                Webdav.ms_entry_args.size = 0;
                Webdav.ms_entry_args.rfc1123_mtime = mtime;
                Webdav.ms_entry_args.content_type = "";
                Webdav.ms_entry(webdav_work);
                len = Webdav.n;
                for (int k = 0; k < 8; k++)
                {
                    Webdav.ms_entry_args.buf = buf;
                    Webdav.ms_entry_args.cap = sizeof(buf);
                    Webdav.ms_entry_args.len = len;
                    Webdav.ms_entry_args.href = "/dav/sensor-log.csv";
                    Webdav.ms_entry_args.is_collection = false;
                    Webdav.ms_entry_args.size = 12800;
                    Webdav.ms_entry_args.rfc1123_mtime = mtime;
                    Webdav.ms_entry_args.content_type = "text/csv";
                    Webdav.ms_entry(webdav_work);
                    len = Webdav.n;
                }
                Webdav.ms_end_args.buf = buf;
                Webdav.ms_end_args.cap = sizeof(buf);
                Webdav.ms_end_args.len = len;
                Webdav.ms_end(webdav_work);
                len = Webdav.n;
                sink += len;
            },
            ns);
        Webdav.ms_begin_args.buf = buf;
        Webdav.ms_begin_args.cap = sizeof(buf);
        Webdav.ms_begin_args.len = 0;
        Webdav.ms_begin(webdav_work);
        size_t len = Webdav.n;
        Webdav.ms_entry_args.buf = buf;
        Webdav.ms_entry_args.cap = sizeof(buf);
        Webdav.ms_entry_args.len = len;
        Webdav.ms_entry_args.href = "/dav/";
        Webdav.ms_entry_args.is_collection = true;
        Webdav.ms_entry_args.size = 0;
        Webdav.ms_entry_args.rfc1123_mtime = mtime;
        Webdav.ms_entry_args.content_type = "";
        Webdav.ms_entry(webdav_work);
        len = Webdav.n;
        for (int k = 0; k < 8; k++)
        {
            Webdav.ms_entry_args.buf = buf;
            Webdav.ms_entry_args.cap = sizeof(buf);
            Webdav.ms_entry_args.len = len;
            Webdav.ms_entry_args.href = "/dav/sensor-log.csv";
            Webdav.ms_entry_args.is_collection = false;
            Webdav.ms_entry_args.size = 12800;
            Webdav.ms_entry_args.rfc1123_mtime = mtime;
            Webdav.ms_entry_args.content_type = "text/csv";
            Webdav.ms_entry(webdav_work);
            len = Webdav.n;
        }
        Webdav.ms_end_args.buf = buf;
        Webdav.ms_end_args.cap = sizeof(buf);
        Webdav.ms_end_args.len = len;
        Webdav.ms_end(webdav_work);
        len = Webdav.n;
        hbench_row("webdav", "PROPFIND depth-1 (8)", ns, (double)len);
        (void)sink;
    }

    // XML-escape an href containing the five escapable characters (per href / property tag).
    {
        char esc[256];
        volatile size_t sink = 0;
        double ns = 0.0;
        Webdav.xml_escape_args.dst = esc;
        Webdav.xml_escape_args.cap = sizeof(esc);
        Webdav.xml_escape_args.src = "/dav/a&b<c>\"d'e.txt";
        Webdav.xml_escape(webdav_work);
        HBENCH_NS(2000000, sink += Webdav.n, ns);
        Webdav.xml_escape_args.dst = esc;
        Webdav.xml_escape_args.cap = sizeof(esc);
        Webdav.xml_escape_args.src = "/dav/a&b<c>\"d'e.txt";
        Webdav.xml_escape(webdav_work);
        size_t bytes = Webdav.n;
        hbench_row("webdav", "xml_escape", ns, (double)bytes);
        (void)sink;
    }

    return 0;
}
