// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Q7: is the per-listener event queue live at PROTOCORE_WORKER_COUNT > 1?
//
// listener_add() creates lst->queue unconditionally. listener_enqueue() reads it only in the
// N == 1 branch; at N > 1 it sends to lq.wq[owner] instead. This reports what a listener row
// spends on a queue it may never be sent to. Built once per worker count by run.sh, which passes
// -DPROTOCORE_WORKER_COUNT.

#include <stdio.h>

#include "network_drivers/transport/tcp/common.h" // Listener, listener_pool
#include "network_drivers/transport/tcp/server/server.h"
#include "protocore_config.h"

int main(void)
{
    // What the queue would cost, from the header constants.
    const unsigned long would_cost = (unsigned long)EVT_QUEUE_DEPTH * sizeof(TcpEvt) * (unsigned long)MAX_LISTENERS;
    const unsigned long workers =
        (unsigned long)EVT_QUEUE_DEPTH * sizeof(TcpEvt) * (unsigned long)PROTOCORE_WORKER_COUNT;

    // What it actually costs, measured off the struct the guard edits.
    const unsigned long row = (unsigned long)sizeof(Listener);
    const unsigned long pool = (unsigned long)sizeof(listener_pool);

    printf("PROTOCORE_WORKER_COUNT   %d\n", (int)PROTOCORE_WORKER_COUNT);
    printf("MAX_LISTENERS            %d\n", (int)MAX_LISTENERS);
    printf("EVT_QUEUE_DEPTH          %d\n", (int)EVT_QUEUE_DEPTH);
    printf("sizeof(TcpEvt)           %d\n", (int)sizeof(TcpEvt));
    printf("sizeof(EvtType)          %d\n", (int)sizeof(EvtType));
    printf("sizeof(ConnState)        %d\n", (int)sizeof(ConnState));
    printf("sizeof(Listener)         %lu bytes   <- measured\n", row);
    printf("sizeof(listener_pool)    %lu bytes   <- measured\n", pool);
    printf("queue would cost         %lu bytes\n", would_cost);
    printf("per-worker queues        %lu bytes\n", workers);
#if PROTOCORE_WORKER_COUNT > 1
    printf("verdict                  listener queues GUARDED OUT: pool is %lu, not %lu\n", pool, pool + would_cost);
#else
    printf("verdict                  listener queues PRESENT: the only path an event takes\n");
#endif
    return 0;
}
