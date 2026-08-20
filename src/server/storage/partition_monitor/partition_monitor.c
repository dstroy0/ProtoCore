// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file partition_monitor.c
 * @brief Partition-map kind classifier, JSON serializer, and flash walk.
 *
 * The classifier and serializer are pure (host-tested); the walk uses
 * esp_partition / esp_ota_ops on ESP32 and is a no-op on host builds. No server
 * dependency lives here - the route is in partition_monitor_routes.cpp.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PARTITION_MONITOR

#include "server/storage/partition_monitor/partition_monitor.h"

#include "mmgr/protoframe/protoframe.h"
#include "mmgr/protostr/protostr.h"
#include "protocore.h" // on_http: the route table the begin entry installs on

PROTOCORE_BEGIN_DECLS

// The type/subtype codes the classifier reads are the partition table's own, mirrored here as
// plain numbers so this file stays pure and host-testable. No vendor header: the table walk is a
// platform seam (protocore_platform_partition_walk), and the vendor's headers live beside it in
// test/core_setup/hal/.
// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_partition_monitor_kind(uint8_t *restrict work);

void protocore_partition_monitor_kind(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = PartitionMonitorV.kind_args.type;
    uint8_t subtype = PartitionMonitorV.kind_args.subtype;

    if (type == 0) // ESP_PARTITION_TYPE_APP
    {
        if (subtype == 0x00)
        {
            PartitionMonitorV.text = "factory";
            return;
        }
        if (subtype >= 0x10 && subtype <= 0x1F)
        {
            PartitionMonitorV.text = "ota";
            return;
        }
        if (subtype == 0x20)
        {
            PartitionMonitorV.text = "test";
            return;
        }
        PartitionMonitorV.text = "app";
        return;
    }
    switch (subtype) // ESP_PARTITION_TYPE_DATA
    {
    case 0x00:
        PartitionMonitorV.text = "otadata";
        return;
    case 0x01:
        PartitionMonitorV.text = "phy";
        return;
    case 0x02:
        PartitionMonitorV.text = "nvs";
        return;
    case 0x03:
        PartitionMonitorV.text = "coredump";
        return;
    case 0x04:
        PartitionMonitorV.text = "nvs_keys";
        return;
    case 0x81:
        PartitionMonitorV.text = "fat";
        return;
    case 0x82:
        PartitionMonitorV.text = "spiffs";
        return;
    case 0x83:
        PartitionMonitorV.text = "littlefs";
        return;
    default:
        PartitionMonitorV.text = "data";
        return;
    }
}

// The item index selects it; !!i is 0 or 1, so the separator is a load rather than a branch.
static const char *const PROTOCORE_JSON_SEP[2] = {"", ","};

static const protocore_field PART_OPEN[] = {{PROTOCORE_FK_LIT, 0, 15, "{\"partitions\":["}, PROTOCORE_END};
static const protocore_field PART_ENTRY[] = {
    PROTOCORE_STR,                              // "," from the second entry on
    {PROTOCORE_FK_LIT, 0, 9, "{\"label\":"},    //
    PROTOCORE_JSON,                             // label
    {PROTOCORE_FK_LIT, 0, 8, ",\"kind\":"},     //
    PROTOCORE_JSON,                             // kind name
    {PROTOCORE_FK_LIT, 0, 8, ",\"type\":"},     //
    PROTOCORE_U32,                              //
    {PROTOCORE_FK_LIT, 0, 11, ",\"subtype\":"}, //
    PROTOCORE_U32,                              //
    {PROTOCORE_FK_LIT, 0, 8, ",\"addr\":"},     //
    PROTOCORE_U32,                              //
    {PROTOCORE_FK_LIT, 0, 8, ",\"size\":"},     //
    PROTOCORE_U32,                              //
    {PROTOCORE_FK_LIT, 0, 11, ",\"running\":"}, //
    PROTOCORE_STR,                              // "true" / "false" - a JSON keyword, not a string
    {PROTOCORE_FK_LIT, 0, 1, "}"},              //
    PROTOCORE_END,
};
static const protocore_field PART_CLOSE[] = {{PROTOCORE_FK_LIT, 0, 2, "]}"}, PROTOCORE_END};

void protocore_partition_monitor_json(uint8_t *restrict work)
{
    (void)work;
    const protocore_partition_info *parts = PartitionMonitorV.json_args.parts;
    uint8_t count = PartitionMonitorV.json_args.count;
    char *out = PartitionMonitorV.json_args.out;
    uint32_t cap = PartitionMonitorV.json_args.cap;

    if (!out || cap == 0)
    {
        PartitionMonitorV.n = 0;
        return;
    }
    out[0] = '\0';
    if (!parts)
    {
        PartitionMonitorV.n = 0;
        return;
    }
    // Each arm empties the buffer before reporting 0: a frame that did not fit leaves the document
    // open, and a caller measuring the buffer instead of reading the count would ship the fragment.
    if (frame.append(out, cap, PART_OPEN, NULL, 0) == 0)
    {
        out[0] = '\0';
        PartitionMonitorV.n = 0;
        return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        const protocore_partition_info *p = &parts[i];
        PartitionMonitorV.kind_args.type = p->type;
        PartitionMonitorV.kind_args.subtype = p->subtype;
        protocore_partition_monitor_kind(work);
        if (frame.append(out, cap, PART_ENTRY,
                         (const protocore_fval[]){
                             PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]), PROTOCORE_VJSON(p->label),
                             PROTOCORE_VJSON(PartitionMonitorV.text), PROTOCORE_VU32((uint32_t)p->type),
                             PROTOCORE_VU32((uint32_t)p->subtype), PROTOCORE_VU32((uint32_t)p->address),
                             PROTOCORE_VU32((uint32_t)p->size), PROTOCORE_VSTR(p->running ? "true" : "false")},
                         8) == 0)
        {
            out[0] = '\0';
            PartitionMonitorV.n = 0;
            return;
        }
    }
    size_t n = frame.append(out, cap, PART_CLOSE, NULL, 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    PartitionMonitorV.n = (int32_t)n;
}

// The table walk is the vendor's, so it is reached through the platform seam rather than through a
// vendor header: the vendor's headers live beside its implementation in test/core_setup/hal/.

#if PROTOCORE_HAS_VENDOR_OTA
// A part whose SDK owns a partition table: walk it through the seam and translate each entry into
// the shape this module publishes.
void protocore_partition_monitor_collect(uint8_t *restrict work)
{
    (void)work;
    protocore_partition_info *out = PartitionMonitorV.collect_args.out;
    const uint8_t max = PartitionMonitorV.collect_args.max;

    PartitionMonitorV.u8 = 0;
    if (!out || max == 0)
    {
        return;
    }
    protocore_platform_partition tbl[PROTOCORE_PARTITION_MAX];
    const uint8_t want = max < PROTOCORE_PARTITION_MAX ? max : PROTOCORE_PARTITION_MAX;
    const uint8_t n = protocore_platform_partition_walk(tbl, want);
    for (uint8_t i = 0; i < n; i++)
    {
        protocore_partition_info *d = &out[i];
        (void)str.copy(d->label, tbl[i].label, sizeof(d->label));
        d->label[sizeof(d->label) - 1] = '\0';
        d->type = tbl[i].type;
        d->subtype = tbl[i].subtype;
        d->address = tbl[i].address;
        d->size = tbl[i].size;
        d->running = tbl[i].running ? PROTO_TRUE : PROTO_FALSE;
    }
    PartitionMonitorV.u8 = n;
}
#endif

#if !PROTOCORE_HAS_VENDOR_OTA
// A part with no partition table reports none, rather than inventing entries a caller would then
// serve as though they described real storage.
void protocore_partition_monitor_collect(uint8_t *restrict work)
{
    (void)work;
    PartitionMonitorV.u8 = 0;
}
#endif

// The handler is partition_monitor_routes.c's - it is the arm with an HTTP surface to serve on -
// and the entry that installs it is here, with the rest of the namespace.
void partition_route_handler(uint8_t slot_id, HttpReq *req);

// Install the route the handler answers on. Under the module's own gate: a build without the
// monitor has neither this entry nor the handler.
void protocore_partition_monitor_begin(uint8_t *restrict work)
{
    (void)work;
    const char *path = PartitionMonitorV.begin_args.path;

    on_http((path && path[0]) ? path : "/partitions", HTTP_GET, partition_route_handler);
}

/** @brief The operands and the outcome. */
PartitionMonitorVars PartitionMonitorV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PARTITION_MONITOR
