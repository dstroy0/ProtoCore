// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the WAMP codec (services/iot/wamp): the JSON message builders
// (HELLO / SUBSCRIBE / GOODBYE) and the array-element parser used to decode inbound messages. Pure;
// the WebSocket transport is elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/wamp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/wamp/wamp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Build `[HELLO, Realm|uri, Details|dict]` into @p buf; the octets written. */
static size_t wamp_hello(char *buf, size_t cap)
{
    Wamp.out.buf = buf;
    Wamp.out.cap = cap;
    Wamp.uri.realm = "realm1";
    Wamp.payload.details = "{\"roles\":{\"subscriber\":{}}}";
    Wamp.build_hello(Wamp.internal);
    return Wamp.n;
}

/** @brief Build `[SUBSCRIBE, Request|id, Options|dict, Topic|uri]` into @p buf; the octets written. */
static size_t wamp_subscribe(char *buf, size_t cap)
{
    Wamp.out.buf = buf;
    Wamp.out.cap = cap;
    Wamp.id.request = 713845233ull;
    Wamp.uri.topic = "com.pc.telemetry";
    Wamp.payload.options = NULL;
    Wamp.build_subscribe(Wamp.internal);
    return Wamp.n;
}

/** @brief Build `[GOODBYE, Details|dict, Reason|uri]` into @p buf; the octets written. */
static size_t wamp_goodbye(char *buf, size_t cap)
{
    Wamp.out.buf = buf;
    Wamp.out.cap = cap;
    Wamp.uri.reason = "wamp.close.normal";
    Wamp.payload.details = NULL;
    Wamp.build_goodbye(Wamp.internal);
    return Wamp.n;
}

/** @brief Read the message type code of @p msg into @p type; whether element 0 held one. */
static proto_bool wamp_type(const char *msg, int32_t *type)
{
    Wamp.parse.msg = msg;
    Wamp.get_type(Wamp.internal);
    *type = Wamp.i32;
    return Wamp.ok;
}

void dbench_run(void)
{
    static const char welcome[] = "[2,9129137332,{\"roles\":{\"broker\":{}}}]";

    for (;;)
    {
        DBENCH_BANNER("wamp");
        volatile size_t sink = 0;
        static char buf[256];
        DBENCH_OP("Wamp.build_hello", 200000, sink += wamp_hello(buf, sizeof(buf)));
        DBENCH_OP("Wamp.build_subscribe", 200000, sink += wamp_subscribe(buf, sizeof(buf)));
        DBENCH_OP("Wamp.build_goodbye", 200000, sink += wamp_goodbye(buf, sizeof(buf)));
        int32_t type = 0;
        DBENCH_OP("Wamp.get_type (parse)", 200000, sink += wamp_type(welcome, &type) ? 1u : 0u);
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("wamp")
