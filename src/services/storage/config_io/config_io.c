// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_io.c
 * @brief Schema-driven config export / restore over the config store.
 *
 * Delegates value storage to services/storage/config_store (which has a host in-memory
 * backend), so the whole serialize / parse round-trip is host-tested.
 */

#include "services/storage/config_io/config_io.h"
#include "mmgr/frame.h" // the one frame engine

#if PC_ENABLE_CONFIG_IO

#include "services/storage/config_store/config_store.h"
#include "shared_primitives/numparse.h"
#include <stdio.h>
#include <string.h>

// An exported u32 field is one number.
static const pc_field CFG_U32[] = {PC_U32, PC_END};

#define PC_VAL_MAX 128 // export/import value field cap
#define PC_KEY_MAX 16  // NVS key cap (15 + null)

// Look up a key in the schema; write its pc_cfg_type to *out and return true, or return false if absent.
static proto_bool field_type(const pc_cfg_field *fields, size_t n, const char *key, pc_cfg_type *out)
{
    for (size_t i = 0; i < n; i++)
    {
        if (fields[i].key && strcmp(fields[i].key, key) == 0)
        {
            *out = fields[i].type;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// Append "<key>=<val>\n" to out at *pos, overflow-safe. Returns false on overflow.
static proto_bool append_kv(char *out, size_t cap, size_t *pos, const char *key, const char *val)
{
    size_t kn = strnlen(key, cap + 1), vn = strnlen(val, cap + 1);
    size_t need = kn + 1 + vn + 1; // key '=' val '\n'
    if (*pos + need >= cap)        // keep room for the null terminator
    {
        return PROTO_FALSE;
    }
    memcpy(out + *pos, key, kn);
    *pos += kn;
    out[(*pos)++] = '=';
    memcpy(out + *pos, val, vn);
    *pos += vn;
    out[(*pos)++] = '\n';
    out[*pos] = '\0';
    return PROTO_TRUE;
}

int pc_config_export(const char *ns, const pc_cfg_field *fields, size_t n, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (!fields || !pc_config_begin(ns))
    {
        return 0; // host config_store backend's pc_config_begin always returns true
    }

    size_t pos = 0;
    for (size_t i = 0; i < n; i++)
    {
        char val[PC_VAL_MAX];
        if (fields[i].type == PC_CFG_U32)
        {
            // Fails closed to an empty string on its own, so there is no failure arm to write here.
            pc_frame_build(val, sizeof(val), CFG_U32, (uint32_t)pc_config_get_u32(fields[i].key, 0));
        }
        else
        {
            pc_config_get_str(fields[i].key, val, sizeof(val), "");
        }

        if (!append_kv(out, cap, &pos, fields[i].key, val))
        {
            out[0] = '\0';
            return 0; // fail closed on overflow
        }
    }
    return (int)pos;
}

// Set one key=val pair against the field table; returns true iff a matching field was found and its
// setter accepted the value. Extracted so the import loop stays flat (one dispatch, no nested type switch).
static proto_bool config_apply_field(const pc_cfg_field *fields, size_t n, const char *key, const char *val)
{
    pc_cfg_type t;
    if (!field_type(fields, n, key, &t))
    {
        return PROTO_FALSE;
    }
    if (t == PC_CFG_U32)
    {
        return pc_config_set_u32(key, (uint32_t)pc_strtoul(val, NULL));
    }
    if (t == PC_CFG_STR)
    {
        return pc_config_set_str(key, val);
    }
    return PROTO_FALSE;
}

int pc_config_import(const char *ns, const pc_cfg_field *fields, size_t n, const char *text, size_t len)
{
    if (!text || !fields || !pc_config_begin(ns))
    {
        return 0; // the host config_store backend's pc_config_begin always returns true
    }

    int count = 0;
    size_t i = 0;
    while (i < len)
    {
        // Find the end of this line.
        size_t eol = i;
        while (eol < len && text[eol] != '\n')
        {
            eol++;
        }

        // Split the line on the first '='.
        size_t eq = i;
        while (eq < eol && text[eq] != '=')
        {
            eq++;
        }

        if (eq >= eol) // no '=' on this line
        {
            i = eol + 1;
            continue;
        }
        size_t klen = eq - i;
        size_t vlen = eol - (eq + 1);
        if (klen > 0 && klen < PC_KEY_MAX && vlen < PC_VAL_MAX)
        {
            char key[PC_KEY_MAX];
            char val[PC_VAL_MAX];
            memcpy(key, text + i, klen);
            key[klen] = '\0';
            memcpy(val, text + eq + 1, vlen);
            val[vlen] = '\0';
            if (config_apply_field(fields, n, key, val))
            {
                count++;
            }
        }
        i = eol + 1; // skip the newline
    }
    return count;
}

#endif // PC_ENABLE_CONFIG_IO
