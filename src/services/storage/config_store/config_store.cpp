// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_store.cpp
 * @brief Typed NVS configuration store - implementation.
 *
 * ESP32: the Arduino `Preferences` NVS wrapper. Host: a fixed in-memory table so
 * the typed contract is unit-testable. See config_store.h.
 */

#include "config_store.h"

#if PC_ENABLE_CONFIG_STORE

#include <string.h>

#ifdef ARDUINO

#include <Preferences.h>

namespace
{
// All config-store state, owned by one instance (internal linkage): the NVS Preferences
// handle and its open flag, grouped so it is one named owner, unreachable cross-TU.
struct ConfigStoreCtx
{
    Preferences prefs;
    bool open = false;
};
ConfigStoreCtx s_cfg;
} // namespace

bool pc_config_begin(const char *ns)
{
    if (s_cfg.open)
    {
        s_cfg.prefs.end();
    }
    s_cfg.open = s_cfg.prefs.begin(ns, false); // read-write
    return s_cfg.open;
}

bool pc_config_set_str(const char *key, const char *val)
{
    if (!val)
    {
        return false;
    }
    return s_cfg.prefs.putString(key, val) == strnlen(val, PC_CONFIG_VAL_MAX + 1);
}

size_t pc_config_get_str(const char *key, char *out, size_t out_cap, const char *def)
{
    if (!out || out_cap == 0)
    {
        return 0;
    }
    if (!s_cfg.prefs.isKey(key))
    {
        size_t n = def ? strnlen(def, out_cap) : 0;
        if (n > out_cap - 1)
        {
            n = out_cap - 1;
        }
        if (n)
        {
            memcpy(out, def, n);
        }
        out[n] = '\0';
        return n;
    }
    return s_cfg.prefs.getString(key, out, out_cap);
}

bool pc_config_set_u32(const char *key, uint32_t val)
{
    return s_cfg.prefs.putUInt(key, val) == sizeof(uint32_t);
}

uint32_t pc_config_get_u32(const char *key, uint32_t def)
{
    return s_cfg.prefs.getUInt(key, def);
}

bool pc_config_set_blob(const char *key, const void *data, size_t len)
{
    if (!data)
    {
        return false;
    }
    return s_cfg.prefs.putBytes(key, data, len) == len;
}

size_t pc_config_get_blob(const char *key, void *out, size_t out_cap)
{
    if (!out || !s_cfg.prefs.isKey(key))
    {
        return 0;
    }
    return s_cfg.prefs.getBytes(key, out, out_cap);
}

bool pc_config_erase(const char *key)
{
    return s_cfg.prefs.remove(key);
}

bool pc_config_clear(void)
{
    return s_cfg.prefs.clear();
}

#else // host: in-memory backend (test double for NVS)

namespace
{
struct Entry
{
    char key[PC_CONFIG_KEY_MAX];
    uint8_t val[PC_CONFIG_VAL_MAX];
    size_t len;
    bool used;
};
// All host config-store state, owned by one instance (internal linkage): the in-memory
// entry table (the NVS test double), so it is one named owner, unreachable cross-TU.
struct ConfigStoreCtx
{
    Entry tbl[PC_CONFIG_MAX_ENTRIES];
};
ConfigStoreCtx s_cfg;

bool key_ok(const char *key)
{
    return key && key[0] && strnlen(key, PC_CONFIG_KEY_MAX + 1) < PC_CONFIG_KEY_MAX;
}

// Returns a mutable entry (callers mutate it), so it takes the owner by non-const reference.
Entry *find(ConfigStoreCtx &c, const char *key)
{
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        if (c.tbl[i].used && strcmp(c.tbl[i].key, key) == 0)
        {
            return &c.tbl[i];
        }
    }
    return nullptr;
}

Entry *find_or_alloc(ConfigStoreCtx &c, const char *key)
{
    Entry *e = find(c, key);
    if (e)
    {
        return e;
    }
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        if (!c.tbl[i].used)
        {
            c.tbl[i].used = true;
            strncpy(c.tbl[i].key, key, PC_CONFIG_KEY_MAX - 1);
            c.tbl[i].key[PC_CONFIG_KEY_MAX - 1] = '\0';
            c.tbl[i].len = 0;
            return &c.tbl[i];
        }
    }
    return nullptr; // table full
}

bool store(ConfigStoreCtx &c, const char *key, const void *data, size_t len)
{
    if (!key_ok(key) || len > PC_CONFIG_VAL_MAX)
    {
        return false;
    }
    Entry *e = find_or_alloc(c, key);
    if (!e)
    {
        return false;
    }
    memcpy(e->val, data, len);
    e->len = len;
    return true;
}
} // namespace

bool pc_config_begin(const char *ns)
{
    (void)ns; // single in-memory namespace on host
    return true;
}

bool pc_config_set_str(const char *key, const char *val)
{
    if (!val)
    {
        return false;
    }
    return store(s_cfg, key, val, strnlen(val, PC_CONFIG_VAL_MAX) + 1); // include the null terminator
}

size_t pc_config_get_str(const char *key, char *out, size_t out_cap, const char *def)
{
    if (!out || out_cap == 0)
    {
        return 0;
    }
    Entry *e = key_ok(key) ? find(s_cfg, key) : nullptr;
    const char *src = e ? (const char *)e->val : (def ? def : "");
    size_t n = strnlen(src, out_cap);
    if (n > out_cap - 1)
    {
        n = out_cap - 1;
    }
    memcpy(out, src, n);
    out[n] = '\0';
    return n;
}

bool pc_config_set_u32(const char *key, uint32_t val)
{
    uint8_t b[4] = {(uint8_t)val, (uint8_t)(val >> 8), (uint8_t)(val >> 16), (uint8_t)(val >> 24)};
    return store(s_cfg, key, b, sizeof(b));
}

uint32_t pc_config_get_u32(const char *key, uint32_t def)
{
    Entry *e = key_ok(key) ? find(s_cfg, key) : nullptr;
    if (!e || e->len < 4)
    {
        return def;
    }
    return (uint32_t)e->val[0] | ((uint32_t)e->val[1] << 8) | ((uint32_t)e->val[2] << 16) | ((uint32_t)e->val[3] << 24);
}

bool pc_config_set_blob(const char *key, const void *data, size_t len)
{
    if (!data)
    {
        return false;
    }
    return store(s_cfg, key, data, len);
}

size_t pc_config_get_blob(const char *key, void *out, size_t out_cap)
{
    Entry *e = key_ok(key) ? find(s_cfg, key) : nullptr;
    if (!e || !out)
    {
        return 0;
    }
    size_t n = e->len < out_cap ? e->len : out_cap;
    memcpy(out, e->val, n);
    return n;
}

bool pc_config_erase(const char *key)
{
    Entry *e = key_ok(key) ? find(s_cfg, key) : nullptr;
    if (!e)
    {
        return false;
    }
    e->used = false;
    e->len = 0;
    return true;
}

bool pc_config_clear(void)
{
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        s_cfg.tbl[i] = Entry{};
    }
    return true;
}

#endif // ARDUINO
#endif // PC_ENABLE_CONFIG_STORE
