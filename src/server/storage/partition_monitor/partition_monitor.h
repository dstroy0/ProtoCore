// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file partition_monitor.h
 * @brief Flash partition-map monitor (PROTOCORE_ENABLE_PARTITION_MONITOR).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PARTITION_MONITOR

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief One flash partition entry. */
typedef struct
{
    char label[17];     ///< partition label (null-terminated).
    uint8_t type;       ///< esp_partition type (0 = app, 1 = data).
    uint8_t subtype;    ///< esp_partition subtype.
    uint32_t address;   ///< flash offset (bytes).
    uint32_t size;      ///< partition size (bytes).
    proto_bool running; ///< true for the currently-running app partition.
} protocore_partition_info;
/** @brief What kind takes: type, subtype. */
typedef struct
{
    uint8_t type;
    uint8_t subtype;
} PartitionMonitorKindArgs;
/** @brief What json takes: parts, count, out, cap. */
typedef struct
{
    const protocore_partition_info *parts;
    uint8_t count;
    char *out;
    uint32_t cap;
} PartitionMonitorJsonArgs;
/** @brief What collect takes: out, max. */
typedef struct
{
    protocore_partition_info *out;
    uint8_t max;
} PartitionMonitorCollectArgs;
/** @brief What begin takes: path. */
typedef struct
{
    const char *path;
} PartitionMonitorBeginArgs;
/**
 * @brief Flash partition-map monitor (PROTOCORE_ENABLE_PARTITION_MONITOR). Reports the device's flash partition table
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::PartitionMonitor with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   PartitionMonitor.kind_args.type = ...;
 *   PartitionMonitor.kind_args.subtype = ...;
 *   PartitionMonitor.kind(work);
 *   // PartitionMonitor.text is what the call reports
 *
 * @var PartitionMonitorNs::kind_args  what kind takes: type, subtype
 * @var PartitionMonitorNs::json_args  what json takes: parts, count, out, cap
 * @var PartitionMonitorNs::collect_args  what collect takes: out, max
 * @var PartitionMonitorNs::begin_args  what begin takes: path
 * @var PartitionMonitorNs::ok  a call's true/false outcome
 * @var PartitionMonitorNs::text  the string a call reports
 * @var PartitionMonitorNs::n  characters written, or 0 if cap is too small
 * @var PartitionMonitorNs::u8  number of partitions written (<= max)
 * @var PartitionMonitorNs::kind  human name for a partition type/subtype (e.g. "factory", "ota", ...
 * @var PartitionMonitorNs::json  serialize a partition array as JSON `{"partitions":[...]}` into out
 * @var PartitionMonitorNs::collect  walk the flash partition table into out (ESP32; 0 on host builds)
 * @var PartitionMonitorNs::begin  serve the partition map as JSON at path (GET). Default "/partitions"
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    PartitionMonitorKindArgs kind_args;
    PartitionMonitorJsonArgs json_args;
    PartitionMonitorCollectArgs collect_args;
    PartitionMonitorBeginArgs begin_args;
    proto_bool ok;
    const char *text;
    int32_t n;
    uint8_t u8;
} PartitionMonitorVars;

/** @brief The operands and the outcome. */
extern PartitionMonitorVars PartitionMonitorV;

/** @brief The entries. */
typedef struct
{
    void (*const kind)(uint8_t *restrict work);
    void (*const json)(uint8_t *restrict work);
    void (*const collect)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
} PartitionMonitorNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in PartitionMonitorV or a region of the borrow at a fixed offset.
void protocore_partition_monitor_kind(uint8_t *restrict work);
void protocore_partition_monitor_json(uint8_t *restrict work);
void protocore_partition_monitor_collect(uint8_t *restrict work);
void protocore_partition_monitor_begin(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `PartitionMonitor.kind(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const PartitionMonitorNs PartitionMonitor __attribute__((unused)) = {
    .kind = protocore_partition_monitor_kind,
    .json = protocore_partition_monitor_json,
    .collect = protocore_partition_monitor_collect,
    .begin = protocore_partition_monitor_begin,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PARTITION_MONITOR

#endif // PROTOCORE_PARTITION_MONITOR_H
