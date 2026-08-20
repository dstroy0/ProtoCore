// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_io.c
 * @brief Schema-driven config export / restore over the config store.
 *
 * Delegates value storage to server/storage/config_store (which has a host in-memory
 * backend), so the whole serialize / parse round-trip is host-tested.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CONFIG_IO

#include "mmgr/protoframe/protoframe.h" // the one frame engine
#include "mmgr/protomem/protomem.h"
#include "server/storage/config_io/config_io.h"

#include "mmgr/protostr/protostr.h"
#include "server/storage/config_store/config_store.h"

PROTOCORE_BEGIN_DECLS

// An exported u32 field is one number.
static const protocore_field CFG_U32[] = {PROTOCORE_U32, PROTOCORE_END};

#define PROTOCORE_VAL_MAX 128 // export/import value field cap
#define PROTOCORE_KEY_MAX 16  // NVS key cap (15 + null)

// Look up a key in the schema; write its protocore_cfg_type to *out and return true, or return false if absent.
static proto_bool field_type(const protocore_cfg_field *fields, size_t n, const char *key, protocore_cfg_type *out)
{
    for (size_t i = 0; i < n; i++)
    {
        if (fields[i].key &&
            str.eq(fields[i].key, key, str.len(fields[i].key, PROTOCORE_KEY_MAX - 1u) + 1u, PROTO_FALSE))
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
    size_t kn = str.len(key, cap + 1), vn = str.len(val, cap + 1);
    size_t need = kn + 1 + vn + 1; // key '=' val '\n'
    if (*pos + need >= cap)        // keep room for the null terminator
    {
        return PROTO_FALSE;
    }
    mem.cpy(out + *pos, key, kn);
    *pos += kn;
    out[(*pos)++] = '=';
    mem.cpy(out + *pos, val, vn);
    *pos += vn;
    out[(*pos)++] = '\n';
    out[*pos] = '\0';
    return PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_config_io_export(uint8_t *restrict work)
{
    (void)work;
    const char *ns = ConfigIoV.export_args.ns;
    const protocore_cfg_field *fields = ConfigIoV.export_args.fields;
    size_t n = ConfigIoV.export_args.n;
    char *out = ConfigIoV.export_args.out;
    size_t cap = ConfigIoV.export_args.cap;

    if (!out || cap == 0)
    {
        ConfigIoV.n = 0;
        return;
    }
    out[0] = '\0';
    ConfigStoreV.begin_args.ns = ns;
    ConfigStore.begin(protocore_config_store_span());
    if (!fields || !ConfigStoreV.ok)
    {
        ConfigIoV.n = 0;
        return; // host config_store backend's protocore_config_begin always returns true
    }

    size_t pos = 0;
    for (size_t i = 0; i < n; i++)
    {
        char val[PROTOCORE_VAL_MAX];
        if (fields[i].type == PROTOCORE_CFG_U32)
        {
            ConfigStoreV.get_u32_args.key = fields[i].key;
            ConfigStoreV.get_u32_args.def = 0;
            ConfigStore.get_u32(protocore_config_store_span());
            // Fails closed to an empty string on its own, so there is no failure arm to write here.
            frame.build(val, sizeof(val), CFG_U32, (const protocore_fval[]){PROTOCORE_VU32((uint32_t)ConfigStoreV.ms)},
                        1);
        }
        else
        {
            ConfigStoreV.get_str_args.key = fields[i].key;
            ConfigStoreV.get_str_args.out = val;
            ConfigStoreV.get_str_args.out_cap = sizeof(val);
            ConfigStoreV.get_str_args.def = "";
            ConfigStore.get_str(protocore_config_store_span());
        }

        if (!append_kv(out, cap, &pos, fields[i].key, val))
        {
            out[0] = '\0';
            ConfigIoV.n = 0;
            return; // fail closed on overflow
        }
    }
    ConfigIoV.n = (int)pos;
}

// Set one key=val pair against the field table; returns true iff a matching field was found and its
// setter accepted the value. Extracted so the import loop stays flat (one dispatch, no nested type switch).
static proto_bool config_apply_field(const protocore_cfg_field *fields, size_t n, const char *key, const char *val)
{
    protocore_cfg_type t;
    if (!field_type(fields, n, key, &t))
    {
        return PROTO_FALSE;
    }
    if (t == PROTOCORE_CFG_U32)
    {
        ConfigStoreV.set_u32_args.key = key;
        ConfigStoreV.set_u32_args.val = (uint32_t)str.to_ulong(val, NULL);
        ConfigStore.set_u32(protocore_config_store_span());
        return ConfigStoreV.ok;
    }
    if (t == PROTOCORE_CFG_STR)
    {
        ConfigStoreV.set_str_args.key = key;
        ConfigStoreV.set_str_args.val = val;
        ConfigStore.set_str(protocore_config_store_span());
        return ConfigStoreV.ok;
    }
    return PROTO_FALSE;
}

void protocore_config_io_import(uint8_t *restrict work)
{
    (void)work;
    const char *ns = ConfigIoV.import_args.ns;
    const protocore_cfg_field *fields = ConfigIoV.import_args.fields;
    size_t n = ConfigIoV.import_args.n;
    const char *text = ConfigIoV.import_args.text;
    size_t len = ConfigIoV.import_args.len;

    ConfigStoreV.begin_args.ns = ns;
    ConfigStore.begin(protocore_config_store_span());
    if (!text || !fields || !ConfigStoreV.ok)
    {
        ConfigIoV.n = 0;
        return; // the host config_store backend's protocore_config_begin always returns true
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
        if (klen > 0 && klen < PROTOCORE_KEY_MAX && vlen < PROTOCORE_VAL_MAX)
        {
            char key[PROTOCORE_KEY_MAX];
            char val[PROTOCORE_VAL_MAX];
            mem.cpy(key, text + i, klen);
            key[klen] = '\0';
            mem.cpy(val, text + eq + 1, vlen);
            val[vlen] = '\0';
            if (config_apply_field(fields, n, key, val))
            {
                count++;
            }
        }
        i = eol + 1; // skip the newline
    }
    ConfigIoV.n = count;
}

/** @brief The operands and the outcome. */
ConfigIoVars ConfigIoV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CONFIG_IO
