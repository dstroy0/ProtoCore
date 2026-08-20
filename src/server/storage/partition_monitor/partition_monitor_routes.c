// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file partition_monitor_routes.c
 * @brief Partition-map monitor route (GET endpoint serving the JSON).
 *
 * Separated from the host-testable core (partition_monitor.cpp) so the classifier
 * + serializer unit-test without pulling in the server.
 */

#include "server/storage/partition_monitor/partition_monitor.h"

static uint8_t partition_monitor_work[16]; // the borrow an entry takes; PartitionMonitor never reads it

#if PROTOCORE_ENABLE_PARTITION_MONITOR

#include "protocore.h"
#include "shared/mime/mime.h"

// All partition-monitor-routes state, owned by one instance (internal linkage): the server
// handle. (The route handler is a fixed-signature callback, so it reaches this owner directly.)
typedef struct
{
} PartitionRoutesCtx;
static PartitionRoutesCtx s_partr;

// External linkage: the entry that installs this route is in partition_monitor.c, with the rest of
// the namespace, so the whole surface is one initializer.
void partition_route_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    protocore_partition_info parts[PROTOCORE_PARTITION_MAX];
    PartitionMonitorV.collect_args.out = parts;
    PartitionMonitorV.collect_args.max = PROTOCORE_PARTITION_MAX;
    PartitionMonitor.collect(partition_monitor_work);
    uint8_t n = PartitionMonitorV.u8;
    char buf[PROTOCORE_PARTITION_JSON_BUF];
    PartitionMonitorV.json_args.parts = parts;
    PartitionMonitorV.json_args.count = n;
    PartitionMonitorV.json_args.out = buf;
    PartitionMonitorV.json_args.cap = sizeof(buf);
    PartitionMonitor.json(partition_monitor_work);
    // No instance test: a handler only runs because begin() registered its route.
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

#endif // PROTOCORE_ENABLE_PARTITION_MONITOR
