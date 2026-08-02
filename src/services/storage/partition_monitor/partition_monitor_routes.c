// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file partition_monitor_routes.c
 * @brief Partition-map monitor route (GET endpoint serving the JSON).
 *
 * Separated from the host-testable core (partition_monitor.cpp) so the classifier
 * + serializer unit-test without pulling in the server.
 */

#include "services/storage/partition_monitor/partition_monitor.h"

#if PC_ENABLE_PARTITION_MONITOR

#include "protocore.h"
#include "shared_primitives/mime.h"

// All partition-monitor-routes state, owned by one instance (internal linkage): the server
// handle. (The route handler is a fixed-signature callback, so it reaches this owner directly.)
typedef struct
{
} PartitionRoutesCtx;
static PartitionRoutesCtx s_partr;

static void partition_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    pc_partition_info parts[PC_PARTITION_MAX];
    uint8_t n = pc_partition_collect(parts, PC_PARTITION_MAX);
    char buf[PC_PARTITION_JSON_BUF];
    pc_partition_json(parts, n, buf, sizeof(buf));
    // No instance test: a handler only runs because begin() registered its route.
    send_text(slot_id, 200, PC_MIME_JSON, buf);
}

void pc_partition_monitor_begin(const char *path)
{
    on_http((path && path[0]) ? path : "/partitions", HttpMethod::HTTP_GET, partition_handler);
}

#endif // PC_ENABLE_PARTITION_MONITOR
