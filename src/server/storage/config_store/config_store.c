// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_store.c
 * @brief Typed NVS configuration store - implementation.
 *
 * The typing and the defaults; the storage is hal/nvs.h, which picks the device NVS or the host
 * table. One body over both, so the contract a test proves is the contract the device runs.
 * See config_store.h.
 */

#include "config_store.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_CONFIG_STORE

#include "core_setup/hal/nvs.h"

// The namespace every call below addresses, owned by one instance (internal linkage): the seam
// names a namespace per operation, so the one this store was opened on is what it holds.
typedef struct
{
    char ns[PROTOCORE_CONFIG_KEY_MAX];
} ConfigStoreCtx;
static ConfigStoreCtx s_cfg;

proto_bool protocore_config_begin(const char *ns)
{
    if (!ns || !ns[0] || strnlen(ns, PROTOCORE_CONFIG_KEY_MAX) >= PROTOCORE_CONFIG_KEY_MAX)
    {
        s_cfg.ns[0] = '\0';
        return PROTO_FALSE;
    }
    strncpy(s_cfg.ns, ns, PROTOCORE_CONFIG_KEY_MAX - 1);
    s_cfg.ns[PROTOCORE_CONFIG_KEY_MAX - 1] = '\0';
    return PROTO_TRUE;
}

proto_bool protocore_config_set_str(const char *key, const char *val)
{
    return protocore_nvs_put_str(s_cfg.ns, key, val);
}

size_t protocore_config_get_str(const char *key, char *out, size_t out_cap, const char *def)
{
    if (!out || out_cap == 0)
    {
        return 0;
    }
    size_t n = protocore_nvs_get_str(s_cfg.ns, key, out, out_cap);
    if (n > 0)
    {
        return n;
    }
    // Absent: the default is this layer's, so the seam never has to know there is one.
    n = def ? strnlen(def, out_cap) : 0;
    if (n > out_cap - 1)
    {
        n = out_cap - 1;
    }
    if (n)
    {
        mem.cpy(out, def, n);
    }
    out[n] = '\0';
    return n;
}

proto_bool protocore_config_set_u32(const char *key, uint32_t val)
{
    return protocore_nvs_put_u32(s_cfg.ns, key, val);
}

uint32_t protocore_config_get_u32(const char *key, uint32_t def)
{
    return protocore_nvs_get_u32(s_cfg.ns, key, def);
}

proto_bool protocore_config_set_blob(const char *key, const void *data, size_t len)
{
    return protocore_nvs_put_blob(s_cfg.ns, key, data, len);
}

size_t protocore_config_get_blob(const char *key, void *out, size_t out_cap)
{
    return protocore_nvs_get_blob(s_cfg.ns, key, out, out_cap);
}

proto_bool protocore_config_erase(const char *key)
{
    return protocore_nvs_erase(s_cfg.ns, key);
}

proto_bool protocore_config_clear(void)
{
    return protocore_nvs_clear(s_cfg.ns);
}

#endif // PROTOCORE_ENABLE_CONFIG_STORE
