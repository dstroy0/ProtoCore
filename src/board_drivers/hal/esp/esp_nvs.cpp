// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_nvs.cpp
 * @brief ESP backend for hal/nvs.h, over the Arduino `Preferences` NVS wrapper.
 *
 * Each entry point opens the namespace, does its one operation, and closes it, which is what lets
 * the contract above be stateless. A read opens read-only so it cannot create the namespace.
 *
 * C++, because `Preferences` is an Arduino class whose methods cannot be named from C
 * (docs/SYMBOLS.md section 4). nvs.h declares everything this file defines between
 * PROTO_BEGIN_DECLS and PROTO_END_DECLS, so the names it exports are C names.
 */

#include "board_drivers/hal/nvs.h"

#if PC_VENDOR_ESP

#include <Preferences.h>
#include <string.h>

// The one Preferences object every entry point below borrows, opened and closed within the call
// (internal linkage). One named owner; nothing outside this file reaches it.
typedef struct
{
    Preferences prefs;
} EspNvsCtx;
static EspNvsCtx s_nvs;

// A key longer than NVS accepts is refused here rather than silently truncated into a different
// key. `ns` gets the same check: NVS names both with the same limit.
static proto_bool name_ok(const char *name)
{
    return name && name[0] && strnlen(name, PC_CONFIG_KEY_MAX) < PC_CONFIG_KEY_MAX;
}

static proto_bool nvs_open(const char *ns, proto_bool read_only)
{
    return name_ok(ns) && s_nvs.prefs.begin(ns, read_only);
}

proto_bool pc_nvs_has(const char *ns, const char *key)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return PROTO_FALSE;
    }
    proto_bool found = s_nvs.prefs.isKey(key);
    s_nvs.prefs.end();
    return found;
}

size_t pc_nvs_get_blob(const char *ns, const char *key, void *out, size_t cap)
{
    if (!out || cap == 0 || !name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return 0;
    }
    size_t n = s_nvs.prefs.isKey(key) ? s_nvs.prefs.getBytes(key, out, cap) : 0;
    s_nvs.prefs.end();
    return n;
}

proto_bool pc_nvs_put_blob(const char *ns, const char *key, const void *in, size_t len)
{
    if (!in || !name_ok(key) || !nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.putBytes(key, in, len) == len;
    s_nvs.prefs.end();
    return ok;
}

size_t pc_nvs_get_str(const char *ns, const char *key, char *out, size_t cap)
{
    if (!out || cap == 0 || !name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return 0;
    }
    // getString writes the terminator and returns the character count without it; an absent key
    // would give the Arduino String overload, which allocates, so the key is checked first.
    size_t n = s_nvs.prefs.isKey(key) ? s_nvs.prefs.getString(key, out, cap) : 0;
    s_nvs.prefs.end();
    out[n < cap ? n : cap - 1] = '\0';
    return n;
}

proto_bool pc_nvs_put_str(const char *ns, const char *key, const char *val)
{
    if (!val || !name_ok(key) || !nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    size_t len = strnlen(val, PC_CONFIG_VAL_MAX);
    proto_bool ok = s_nvs.prefs.putString(key, val) == len;
    s_nvs.prefs.end();
    return ok;
}

uint32_t pc_nvs_get_u32(const char *ns, const char *key, uint32_t def)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return def;
    }
    uint32_t v = s_nvs.prefs.getUInt(key, def);
    s_nvs.prefs.end();
    return v;
}

proto_bool pc_nvs_put_u32(const char *ns, const char *key, uint32_t val)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.putUInt(key, val) == sizeof(uint32_t);
    s_nvs.prefs.end();
    return ok;
}

proto_bool pc_nvs_erase(const char *ns, const char *key)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.remove(key);
    s_nvs.prefs.end();
    return ok;
}

proto_bool pc_nvs_clear(const char *ns)
{
    if (!nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.clear();
    s_nvs.prefs.end();
    return ok;
}

#endif // PC_VENDOR_ESP
