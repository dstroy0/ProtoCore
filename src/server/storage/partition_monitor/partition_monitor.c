// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file partition_monitor.c
 * @brief Partition-map kind classifier, JSON serializer, and flash walk.
 *
 * The classifier and serializer are pure (host-tested); the walk uses
 * esp_partition / esp_ota_ops on ESP32 and is a no-op on host builds. No server
 * dependency lives here - the route is in partition_monitor_routes.cpp.
 */

#include "server/storage/partition_monitor/partition_monitor.h"

#if PROTOCORE_ENABLE_PARTITION_MONITOR

#include "mmgr/protoframe.h"

// esp_partition type/subtype constants (mirrors esp_partition_type_t/subtype_t so
// the classifier stays pure and host-testable without the IDF headers).
#if PROTOCORE_HAS_VENDOR_OTA
#include <esp_ota_ops.h>
#include <esp_partition.h>
#endif
const char *protocore_partition_kind(uint8_t type, uint8_t subtype)
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

int32_t protocore_partition_json(const protocore_partition_info *parts, uint8_t count, char *out, uint32_t cap)
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
    // Each arm empties the buffer before reporting 0: a frame that did not fit leaves the document
    // open, and a caller measuring the buffer instead of reading the count would ship the fragment.
    if (frame.append(out, cap, PART_OPEN, NULL, 0) == 0)
    {
        out[0] = '\0';
        return 0;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        const protocore_partition_info *p = &parts[i];
        if (frame.append(
                out, cap, PART_ENTRY,
                (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]), PROTOCORE_VJSON(p->label),
                                         PROTOCORE_VJSON(protocore_partition_kind(p->type, p->subtype)),
                                         PROTOCORE_VU32((uint32_t)p->type), PROTOCORE_VU32((uint32_t)p->subtype),
                                         PROTOCORE_VU32((uint32_t)p->address), PROTOCORE_VU32((uint32_t)p->size),
                                         PROTOCORE_VSTR(p->running ? "true" : "false")},
                8) == 0)
        {
            out[0] = '\0';
            return 0;
        }
    }
    size_t n = frame.append(out, cap, PART_CLOSE, NULL, 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    return (int32_t)n;
}

#if PROTOCORE_HAS_VENDOR_OTA

uint8_t protocore_partition_collect(protocore_partition_info *out, uint8_t max)
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
        protocore_partition_info *d = &out[n++];
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

uint8_t protocore_partition_collect(protocore_partition_info *out, uint8_t max)
{
    (void)out;
    (void)max;
    return 0;
}

#endif // PROTOCORE_HAS_VENDOR_OTA

#endif // PROTOCORE_ENABLE_PARTITION_MONITOR
