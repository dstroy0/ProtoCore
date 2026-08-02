// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_io.h
 * @brief Schema-driven config export / restore (PC_ENABLE_CONFIG_IO).
 *
 * The app declares a fixed schema - an array of {key, type} fields - and this
 * service serializes their current values from the config store to a portable
 * `key=value` text blob (one field per line) for backup / migration, and parses
 * such a blob back into the store for restore / bulk provisioning. Schema-driven
 * (rather than enumerating NVS) keeps it deterministic and zero-heap; the
 * serialize / parse round-trip is host-tested against the in-memory config store.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CONFIG_IO_H
#define PROTOCORE_CONFIG_IO_H

#include "protocore_config.h"

#if PC_ENABLE_CONFIG_IO

/** @brief Type of a config field (selects the typed get/set used). */
typedef enum PROTO_ENUM_PACKED
{
    PC_CFG_STR = 0, ///< null-terminated string.
    PC_CFG_U32 = 1, ///< unsigned 32-bit integer (serialized as decimal).
} pc_cfg_type;

/** @brief One field in an export/restore schema. */
typedef struct
{
    const char *key;  ///< config-store key (<= 15 chars).
    pc_cfg_type type; ///< the field's value type.
} pc_cfg_field;

/**
 * @brief Export the schema's current values from namespace @p ns as `key=value`
 *        lines into @p out.
 * @return characters written, or 0 on a too-small buffer / failure (fail-closed).
 */
int pc_config_export(const char *ns, const pc_cfg_field *fields, size_t n, char *out, size_t cap);

/**
 * @brief Import `key=value` lines from @p text into namespace @p ns, writing each
 *        line whose key is in the schema with the schema's type. Unknown keys are
 *        skipped.
 * @return number of fields written.
 */
int pc_config_import(const char *ns, const pc_cfg_field *fields, size_t n, const char *text, size_t len);

#endif // PC_ENABLE_CONFIG_IO
#endif // PROTOCORE_CONFIG_IO_H
