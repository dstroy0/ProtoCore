// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file partition_monitor.h
 * @brief Flash partition-map monitor (PC_ENABLE_PARTITION_MONITOR).
 *
 * Reports the device's flash partition table as JSON for diagnostics / OTA
 * dashboards: each entry's label, a human "kind" (factory / ota / nvs / spiffs /
 * littlefs / coredump / ...), the raw type/subtype, flash offset, size, and which
 * app slot is currently running. The partition walk uses esp_partition /
 * esp_ota_ops (ESP32-only); the kind classifier and the JSON serializer are pure
 * and host-tested.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PARTITION_MONITOR_H
#define PROTOCORE_PARTITION_MONITOR_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_PARTITION_MONITOR

/** @brief One flash partition entry. */
struct pc_partition_info
{
    char label[17];   ///< partition label (null-terminated).
    uint8_t type;     ///< esp_partition type (0 = app, 1 = data).
    uint8_t subtype;  ///< esp_partition subtype.
    uint32_t address; ///< flash offset (bytes).
    uint32_t size;    ///< partition size (bytes).
    bool running;     ///< true for the currently-running app partition.
};

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/** @brief Human name for a partition type/subtype (e.g. "factory", "ota", "nvs", "littlefs"). */
const char *pc_partition_kind(uint8_t type, uint8_t subtype);

/**
 * @brief Serialize a partition array as JSON `{"partitions":[...]}` into @p out.
 * @return characters written, or 0 if @p cap is too small.
 */
int32_t pc_partition_json(const pc_partition_info *parts, uint8_t count, char *out, uint32_t cap);

/**
 * @brief Walk the flash partition table into @p out (ESP32; 0 on host builds).
 * @return number of partitions written (<= @p max).
 */
uint8_t pc_partition_collect(pc_partition_info *out, uint8_t max);

// ---------------------------------------------------------------------------
// Server integration
// ---------------------------------------------------------------------------

/** @brief Serve the partition map as JSON at @p path (GET). Default "/partitions". */
void pc_partition_monitor_begin(const char *path);

#endif // PC_ENABLE_PARTITION_MONITOR
#endif // PROTOCORE_PARTITION_MONITOR_H
