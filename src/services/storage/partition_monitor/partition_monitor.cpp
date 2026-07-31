// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file partition_monitor.cpp
 * @brief Partition-map kind classifier, JSON serializer, and flash walk.
 *
 * The classifier and serializer are pure (host-tested); the walk uses
 * esp_partition / esp_ota_ops on ESP32 and is a no-op on host builds. No server
 * dependency lives here - the route is in partition_monitor_routes.cpp.
 */

#include "services/storage/partition_monitor/partition_monitor.h"

#if PC_ENABLE_PARTITION_MONITOR

#include "shared_primitives/frame.h"
#include <string.h>

// esp_partition type/subtype constants (mirrors esp_partition_type_t/subtype_t so
// the classifier stays pure and host-testable without the IDF headers).
#if defined(ARDUINO)
#include <esp_ota_ops.h>
#include <esp_partition.h>
#endif
const char *pc_partition_kind(uint8_t type, uint8_t subtype)
{
    if (type == 0) // ESP_PARTITION_TYPE_APP
    {
        if (subtype == 0x00)
        {
            return "factory";
        }
        if (subtype >= 0x10 && subtype <= 0x1F)
        {
            return "ota";
        }
        if (subtype == 0x20)
        {
            return "test";
        }
        return "app";
    }
    switch (subtype) // ESP_PARTITION_TYPE_DATA
    {
    case 0x00:
        return "otadata";
    case 0x01:
        return "phy";
    case 0x02:
        return "nvs";
    case 0x03:
        return "coredump";
    case 0x04:
        return "nvs_keys";
    case 0x81:
        return "fat";
    case 0x82:
        return "spiffs";
    case 0x83:
        return "littlefs";
    default:
        return "data";
    }
}

static const pc_field PART_OPEN[] = {{PC_FK_LIT, 0, 15, "{\"partitions\":["}, PC_END};
static const pc_field PART_ENTRY[] = {
    PC_STR,                              // "," from the second entry on
    {PC_FK_LIT, 0, 9, "{\"label\":"},    //
    PC_JSON,                             // label
    {PC_FK_LIT, 0, 8, ",\"kind\":"},     //
    PC_JSON,                             // kind name
    {PC_FK_LIT, 0, 8, ",\"type\":"},     //
    PC_U32,                              //
    {PC_FK_LIT, 0, 11, ",\"subtype\":"}, //
    PC_U32,                              //
    {PC_FK_LIT, 0, 8, ",\"addr\":"},     //
    PC_U32,                              //
    {PC_FK_LIT, 0, 8, ",\"size\":"},     //
    PC_U32,                              //
    {PC_FK_LIT, 0, 11, ",\"running\":"}, //
    PC_STR,                              // "true" / "false" - a JSON keyword, not a string
    {PC_FK_LIT, 0, 1, "}"},              //
    PC_END,
};
static const pc_field PART_CLOSE[] = {{PC_FK_LIT, 0, 2, "]}"}, PC_END};

int32_t pc_partition_json(const pc_partition_info *parts, uint8_t count, char *out, uint32_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (!parts)
    {
        return 0;
    }
    if (pc_frame_append(out, cap, PART_OPEN) == 0)
    {
        return 0;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        const pc_partition_info *p = &parts[i];
        if (pc_frame_append(out, cap, PART_ENTRY, i ? "," : "", p->label, pc_partition_kind(p->type, p->subtype),
                            (uint32_t)p->type, (uint32_t)p->subtype, (uint32_t)p->address, (uint32_t)p->size,
                            p->running ? "true" : "false") == 0)
        {
            return 0;
        }
    }
    return (int32_t)pc_frame_append(out, cap, PART_CLOSE);
}

#ifdef ARDUINO

uint8_t pc_partition_collect(pc_partition_info *out, uint8_t max)
{
    if (!out || max == 0)
    {
        return 0;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    uint8_t n = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL && n < max; it = esp_partition_next(it))
    {
        const esp_partition_t *p = esp_partition_get(it);
        pc_partition_info *d = &out[n++];
        strncpy(d->label, p->label, sizeof(d->label) - 1);
        d->label[sizeof(d->label) - 1] = '\0';
        d->type = (uint8_t)p->type;
        d->subtype = (uint8_t)p->subtype;
        d->address = p->address;
        d->size = p->size;
        d->running = (running != NULL && p->address == running->address);
    }
    esp_partition_iterator_release(it);
    return n;
}

#else // host build - no flash

uint8_t pc_partition_collect(pc_partition_info *, uint8_t)
{
    return 0;
}

#endif // ARDUINO

#endif // PC_ENABLE_PARTITION_MONITOR
