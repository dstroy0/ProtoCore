// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the XMPP stanza codec (services/iot/xmpp): the XML escaper and
// the stanza builders (stream open, message, presence, iq). Pure string logic; no TCP/TLS.
//
// Build/flash:  idf.py -C test/performance_benching/xmpp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/xmpp/xmpp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("xmpp");
        volatile size_t sink = 0;
        static char out[512];
        DBENCH_OP("pc_xmpp_escape", 200000, sink += pc_xmpp_escape("a<b>&\"c'd", 9, out, sizeof(out)));
        DBENCH_OP("pc_xmpp_stream_open", 200000, sink += pc_xmpp_stream_open("rig@pc", "pc.example", out, sizeof(out)));
        DBENCH_OP("pc_xmpp_message", 200000,
                  sink += pc_xmpp_message("ops@pc", "rig@pc", "chat", "temp 84C over threshold", out, sizeof(out)));
        DBENCH_OP("pc_xmpp_iq", 200000,
                  sink += pc_xmpp_iq("get", "q1", "<ping xmlns='urn:xmpp:ping'/>", out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("xmpp")
