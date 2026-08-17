// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_io.h
 * @brief Schema-driven config export / restore (PROTOCORE_ENABLE_CONFIG_IO).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CONFIG_IO

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Type of a config field (selects the typed get/set used). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_CFG_STR = 0, ///< null-terminated string.
    PROTOCORE_CFG_U32 = 1, ///< unsigned 32-bit integer (serialized as decimal).
} protocore_cfg_type;

/** @brief One field in an export/restore schema. */
typedef struct
{
    const char *key;         ///< config-store key (<= 15 chars).
    protocore_cfg_type type; ///< the field's value type.
} protocore_cfg_field;

/** @brief What export takes: ns, fields, n, out, cap. */
typedef struct
{
    const char *ns;
    const protocore_cfg_field *fields;
    size_t n;
    char *out;
    size_t cap;
} ConfigIoExportArgs;

/** @brief What import takes: ns, fields, n, text, len. */
typedef struct
{
    const char *ns;
    const protocore_cfg_field *fields;
    size_t n;
    const char *text;
    size_t len;
} ConfigIoImportArgs;

/**
 * @brief Schema-driven config export / restore (PROTOCORE_ENABLE_CONFIG_IO). The app declares a fixed schema - an ...
 *
 * A caller sets the members a call takes, invokes it through ::ConfigIo with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   ConfigIo.export_args.ns = ...;
 *   ConfigIo.export_args.fields = ...;
 *   ConfigIo.export_args.n = ...;
 *   ConfigIo.export_args.out = ...;
 *   ConfigIo.export_args.cap = ...;
 *   ConfigIo.export(work);
 *   // ConfigIo.n is what the call reports
 *
 * @var ConfigIoNs::export_args  what export takes: ns, fields, n, out, cap
 * @var ConfigIoNs::import_args  what import takes: ns, fields, n, text, len
 * @var ConfigIoNs::ok  a call's true/false outcome
 * @var ConfigIoNs::n  characters written, or 0 on a too-small buffer / failure ...
 * @var ConfigIoNs::export  export the schema's current values from namespace ns as `key=value` ...
 * @var ConfigIoNs::import  import `key=value` lines from text into namespace ns, writing each ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ConfigIoExportArgs export_args;
    ConfigIoImportArgs import_args;

    proto_bool ok;
    int n;

    void (*const export)(uint8_t *restrict work);
    void (*const import)(uint8_t *restrict work);
} ConfigIoNs;

/** @brief The one symbol this module exports. */
extern ConfigIoNs ConfigIo;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CONFIG_IO

#endif // PROTOCORE_CONFIG_IO_H
