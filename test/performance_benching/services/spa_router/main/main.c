// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SPA router core (server/web/spa_router): the path
// extension check (deep-link vs asset) and the conditional UI-fragment streamer that assembles a
// single-page-app shell one fragment at a time. Pure string logic; no server.
//
// Build/flash:  idf.py -C test/performance_benching/spa_router -t upload --upload-port COM7
#include "device_bench.h"
#include "server/web/spa_router/spa_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t spa_router_work[16]; // the borrow an entry takes; SpaRouter never reads it

void dbench_run(void)
{
    static const protocore_ui_fragment frags[3] = {
        {"head", "<head><title>PC</title></head>", NULL},
        {"nav", "<nav>menu</nav>", NULL},
        {"body", "<main>dashboard</main>", NULL},
    };

    for (;;)
    {
        DBENCH_BANNER("spa_router");
        volatile size_t sink = 0;
        DBENCH_OP("protocore_spa_has_extension", 200000, sink += protocore_spa_has_extension("/app/users/42"));
        static char out[128];
        DBENCH_OP("protocore_ui_stream (3 fragments)", 100000, {
            protocore_ui_stream s;
            SpaRouterV.ui_stream_begin_args.s = &s;
            SpaRouterV.ui_stream_begin_args.frags = frags;
            SpaRouterV.ui_stream_begin_args.count = 3;
            SpaRouterV.ui_stream_begin_args.ctx = NULL;
            SpaRouter.ui_stream_begin(spa_router_work);
            while (!protocore_ui_stream_done(&s))
            {
                SpaRouterV.ui_stream_next_args.s = &s;
                SpaRouterV.ui_stream_next_args.out = out;
                SpaRouterV.ui_stream_next_args.cap = sizeof(out);
                SpaRouter.ui_stream_next(spa_router_work);
                sink += SpaRouterV.n;
            }
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("spa_router")
