// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

void dbench_run(void)
{
    static const char welcome[] = "[2,9129137332,{\"roles\":{\"broker\":{}}}]";

    for (;;)
    {
        DBENCH_BANNER("wamp");
        volatile size_t sink = 0;
        static char buf[256];
        DBENCH_OP("protocore_wamp_build_hello", 200000,
                  sink += protocore_wamp_build_hello(buf, sizeof(buf), "realm1", "{\"roles\":{\"subscriber\":{}}}"));
        DBENCH_OP("protocore_wamp_build_subscribe", 200000,
                  sink += protocore_wamp_build_subscribe(buf, sizeof(buf), 713845233ull, "com.pc.telemetry", NULL));
        DBENCH_OP("protocore_wamp_build_goodbye", 200000,
                  sink += protocore_wamp_build_goodbye(buf, sizeof(buf), "wamp.close.normal", NULL));
        int type = 0;
        DBENCH_OP("protocore_wamp_get_type (parse)", 200000, sink += protocore_wamp_get_type(welcome, &type));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("wamp")
