// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t xmpp_work[16]; // the borrow an entry takes; Xmpp never reads it

/** @brief Write the nine characters of the escape fixture through the predefined entities. */
static size_t xmpp_escape(char *out, size_t cap)
{
    XmppV.out.buf = out;
    XmppV.out.cap = cap;
    XmppV.text.in = "a<b>&\"c'd";
    XmppV.text.len = 9;
    Xmpp.escape(xmpp_work);
    return XmppV.n;
}

/** @brief Build the initial stream header into @p out; the octets written. */
static size_t xmpp_stream_open(char *out, size_t cap)
{
    XmppV.out.buf = out;
    XmppV.out.cap = cap;
    XmppV.stream.from = "rig@pc";
    XmppV.stream.to = "pc.example";
    Xmpp.stream_open(xmpp_work);
    return XmppV.n;
}

/** @brief Build a chat `<message/>` with a body into @p out; the octets written. */
static size_t xmpp_message(char *out, size_t cap)
{
    XmppV.out.buf = out;
    XmppV.out.cap = cap;
    XmppV.common.to = "ops@pc";
    XmppV.common.from = "rig@pc";
    XmppV.common.type = "chat";
    XmppV.child.body = "temp 84C over threshold";
    Xmpp.message(xmpp_work);
    return XmppV.n;
}

/** @brief Build a get `<iq/>` carrying a ping extension into @p out; the octets written. */
static size_t xmpp_iq(char *out, size_t cap)
{
    XmppV.out.buf = out;
    XmppV.out.cap = cap;
    XmppV.common.to = NULL;
    XmppV.common.from = NULL;
    XmppV.common.type = "get";
    XmppV.common.id = "q1";
    XmppV.child.extension = "<ping xmlns='urn:xmpp:ping'/>";
    Xmpp.iq(xmpp_work);
    return XmppV.n;
}

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("xmpp");
        volatile size_t sink = 0;
        static char out[512];
        DBENCH_OP("Xmpp.escape", 200000, sink += xmpp_escape(out, sizeof(out)));
        DBENCH_OP("Xmpp.stream_open", 200000, sink += xmpp_stream_open(out, sizeof(out)));
        DBENCH_OP("Xmpp.message", 200000, sink += xmpp_message(out, sizeof(out)));
        DBENCH_OP("Xmpp.iq", 200000, sink += xmpp_iq(out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("xmpp")
