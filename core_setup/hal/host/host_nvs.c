// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_nvs.c
 * @brief Host backend for hal/nvs.h: a fixed table, so every caller above the seam is testable.
 *
 * PC_CONFIG_MAX_ENTRIES rows of PC_CONFIG_KEY_MAX name and PC_CONFIG_VAL_MAX bytes, in BSS. An
 * entry is addressed by namespace and key together, so two namespaces holding the same key name
 * hold two values, exactly as NVS does. Values do not survive the process, which is the one way
 * this differs from the device: a test that wants a reboot calls pc_nvs_clear().
 */

#include "board_drivers/hal/nvs.h"

#if !PC_VENDOR_ESP

typedef struct
{
    char ns[PC_CONFIG_KEY_MAX];
    char key[PC_CONFIG_KEY_MAX];
    uint8_t val[PC_CONFIG_VAL_MAX];
    size_t len;
    proto_bool used;
} HostNvsEntry;

// The whole store, owned by one instance (internal linkage): one named owner, unreachable
// from any other translation unit.
typedef struct
{
    HostNvsEntry tbl[PC_CONFIG_MAX_ENTRIES];
} HostNvsCtx;
static HostNvsCtx s_nvs;

// NVS caps a name at PC_CONFIG_KEY_MAX including the terminator; a longer one is refused rather
// than truncated into a different name.
static proto_bool name_ok(const char *name)
{
    return name && name[0] && strnlen(name, PC_CONFIG_KEY_MAX) < PC_CONFIG_KEY_MAX;
}

// Returns a mutable entry (callers write through it), so it takes the owner by non-const pointer.
static HostNvsEntry *find(HostNvsCtx *c, const char *ns, const char *key)
{
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        if (c->tbl[i].used && strcmp(c->tbl[i].ns, ns) == 0 && strcmp(c->tbl[i].key, key) == 0)
        {
            return &c->tbl[i];
        }
    }
    return NULL;
}

static HostNvsEntry *find_or_alloc(HostNvsCtx *c, const char *ns, const char *key)
{
    HostNvsEntry *e = find(c, ns, key);
    if (e)
    {
        return e;
    }
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        if (!c->tbl[i].used)
        {
            c->tbl[i].used = PROTO_TRUE;
            strncpy(c->tbl[i].ns, ns, PC_CONFIG_KEY_MAX - 1);
            c->tbl[i].ns[PC_CONFIG_KEY_MAX - 1] = '\0';
            strncpy(c->tbl[i].key, key, PC_CONFIG_KEY_MAX - 1);
            c->tbl[i].key[PC_CONFIG_KEY_MAX - 1] = '\0';
            c->tbl[i].len = 0;
            return &c->tbl[i];
        }
    }
    return NULL; // table full
}

static proto_bool store(const char *ns, const char *key, const void *data, size_t len)
{
    if (!name_ok(ns) || !name_ok(key) || len > PC_CONFIG_VAL_MAX)
    {
        return PROTO_FALSE;
    }
    HostNvsEntry *e = find_or_alloc(&s_nvs, ns, key);
    if (!e)
    {
        return PROTO_FALSE;
    }
    memcpy(e->val, data, len);
    e->len = len;
    return PROTO_TRUE;
}

static HostNvsEntry *lookup(const char *ns, const char *key)
{
    if (!name_ok(ns) || !name_ok(key))
    {
        return NULL;
    }
    return find(&s_nvs, ns, key);
}

proto_bool pc_nvs_has(const char *ns, const char *key)
{
    return lookup(ns, key) != NULL;
}

size_t pc_nvs_get_blob(const char *ns, const char *key, void *out, size_t cap)
{
    HostNvsEntry *e = lookup(ns, key);
    if (!e || !out || cap == 0)
    {
        return 0;
    }
    size_t n = e->len < cap ? e->len : cap;
    memcpy(out, e->val, n);
    return n;
}

proto_bool pc_nvs_put_blob(const char *ns, const char *key, const void *in, size_t len)
{
    if (!in)
    {
        return PROTO_FALSE;
    }
    return store(ns, key, in, len);
}

size_t pc_nvs_get_str(const char *ns, const char *key, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    HostNvsEntry *e = lookup(ns, key);
    if (!e)
    {
        out[0] = '\0';
        return 0;
    }
    // The terminator is stored with the value, so the run is measured rather than assumed: a blob
    // written through put_blob and read back as a string stops at the first zero byte.
    size_t n = strnlen((const char *)e->val, e->len);
    if (n > cap - 1)
    {
        n = cap - 1;
    }
    memcpy(out, e->val, n);
    out[n] = '\0';
    return n;
}

proto_bool pc_nvs_put_str(const char *ns, const char *key, const char *val)
{
    if (!val)
    {
        return PROTO_FALSE;
    }
    return store(ns, key, val, strnlen(val, PC_CONFIG_VAL_MAX - 1) + 1); // the terminator is stored
}

// The object, not a byte layout: this table never leaves the process, so the two halves below only
// have to agree with each other. The device backend stores through NVS's own u32 and shares no
// representation with this one either.
uint32_t pc_nvs_get_u32(const char *ns, const char *key, uint32_t def)
{
    HostNvsEntry *e = lookup(ns, key);
    if (!e || e->len != sizeof(uint32_t))
    {
        return def;
    }
    uint32_t v = 0;
    memcpy(&v, e->val, sizeof(v));
    return v;
}

proto_bool pc_nvs_put_u32(const char *ns, const char *key, uint32_t val)
{
    return store(ns, key, &val, sizeof(val));
}

proto_bool pc_nvs_erase(const char *ns, const char *key)
{
    HostNvsEntry *e = lookup(ns, key);
    if (!e)
    {
        return PROTO_FALSE;
    }
    e->used = PROTO_FALSE;
    e->len = 0;
    return PROTO_TRUE;
}

proto_bool pc_nvs_clear(const char *ns)
{
    if (!name_ok(ns))
    {
        return PROTO_FALSE;
    }
    for (int i = 0; i < PC_CONFIG_MAX_ENTRIES; i++)
    {
        if (s_nvs.tbl[i].used && strcmp(s_nvs.tbl[i].ns, ns) == 0)
        {
            s_nvs.tbl[i].used = PROTO_FALSE;
            s_nvs.tbl[i].len = 0;
        }
    }
    return PROTO_TRUE;
}

#endif // !PC_VENDOR_ESP
