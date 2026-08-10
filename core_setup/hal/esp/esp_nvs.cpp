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

#include "core_setup/hal/nvs.h"

#if PC_VENDOR_ESP

#include <Preferences.h>

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

// Preferences::begin refuses while another call is between begin() and end(), and that refusal is
// indistinguishable from an absent key: a second task would read a credential as missing. One mutex
// makes the seam one caller at a time. Its control block is caller-owned, so nothing is allocated;
// the function-local static is initialised once, which C++11 guarantees against a concurrent second
// caller, and this is the only path that reaches it.
static pc_platform_mutex nvs_lock(void)
{
    static pc_platform_mutex_ctrl store;
    static pc_platform_mutex m = pc_platform_mutex_create(&store);
    return m;
}

// Held from a successful open to the matching close. A refused open holds nothing.
static proto_bool nvs_open(const char *ns, proto_bool read_only)
{
    if (!name_ok(ns))
    {
        return PROTO_FALSE;
    }
    pc_platform_mutex_take(nvs_lock(), portMAX_DELAY);
    if (!s_nvs.prefs.begin(ns, read_only))
    {
        pc_platform_mutex_give(nvs_lock());
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static void nvs_close(void)
{
    s_nvs.prefs.end();
    pc_platform_mutex_give(nvs_lock());
}

proto_bool pc_nvs_has(const char *ns, const char *key)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return PROTO_FALSE;
    }
    proto_bool found = s_nvs.prefs.isKey(key);
    nvs_close();
    return found;
}

size_t pc_nvs_get_blob(const char *ns, const char *key, void *out, size_t cap)
{
    if (!out || cap == 0 || !name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return 0;
    }
    size_t n = s_nvs.prefs.isKey(key) ? s_nvs.prefs.getBytes(key, out, cap) : 0;
    nvs_close();
    return n;
}

proto_bool pc_nvs_put_blob(const char *ns, const char *key, const void *in, size_t len)
{
    if (!in || len == 0 || len > PC_CONFIG_VAL_MAX || !name_ok(key) || !nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE; // putBytes bails on len 0 and reports 0, which would read as success
    }
    proto_bool ok = s_nvs.prefs.putBytes(key, in, len) == len;
    nvs_close();
    return ok;
}

size_t pc_nvs_get_str(const char *ns, const char *key, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0'; // nvs.h promises a terminated buffer on every path, including the ones that fail
    if (!name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return 0;
    }
    // getString returns nvs_get_str's length, which COUNTS the terminator, and returns 0 without
    // writing when the value does not fit. An absent key would give the Arduino String overload,
    // which allocates, so the key is checked first.
    size_t n = s_nvs.prefs.isKey(key) ? s_nvs.prefs.getString(key, out, cap) : 0;
    nvs_close();
    if (n == 0)
    {
        return 0;
    }
    out[n - 1 < cap ? n - 1 : cap - 1] = '\0';
    return n - 1; // the contract is the character count, terminator excluded
}

proto_bool pc_nvs_put_str(const char *ns, const char *key, const char *val)
{
    if (!val || !name_ok(key))
    {
        return PROTO_FALSE;
    }
    // putString writes uncapped and returns the true length, so an over-long value would COMMIT and
    // then be reported as a failure. Measure one past the cap and refuse before opening.
    size_t len = strnlen(val, PC_CONFIG_VAL_MAX + 1);
    if (len > PC_CONFIG_VAL_MAX)
    {
        return PROTO_FALSE;
    }
    if (!nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.putString(key, val) == len;
    nvs_close();
    return ok;
}

uint32_t pc_nvs_get_u32(const char *ns, const char *key, uint32_t def)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_TRUE))
    {
        return def;
    }
    uint32_t v = s_nvs.prefs.getUInt(key, def);
    nvs_close();
    return v;
}

proto_bool pc_nvs_put_u32(const char *ns, const char *key, uint32_t val)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.putUInt(key, val) == sizeof(uint32_t);
    nvs_close();
    return ok;
}

proto_bool pc_nvs_erase(const char *ns, const char *key)
{
    if (!name_ok(key) || !nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.remove(key);
    nvs_close();
    return ok;
}

proto_bool pc_nvs_clear(const char *ns)
{
    if (!nvs_open(ns, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    proto_bool ok = s_nvs.prefs.clear();
    nvs_close();
    return ok;
}

#endif // PC_VENDOR_ESP
