// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/protostr.h"
#include "mmgr/secure.h" // the persistent end this module's state is taken from

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CONFIG_STORE

#include "core_setup/hal/nvs.h"

PROTOCORE_BEGIN_DECLS

// The namespace every call below addresses, owned by one instance (internal linkage): the seam
// names a namespace per operation, so the one this store was opened on is what it holds.
typedef struct
{
    char ns[PROTOCORE_CONFIG_KEY_MAX];
} ConfigStoreCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define CONFIG_STORE_OFF_CTX 0u
static_assert(CONFIG_STORE_OFF_CTX + sizeof(ConfigStoreCtx) <= PROTOCORE_CONFIG_STORE_BORROW,
              "PROTOCORE_CONFIG_STORE_BORROW is short of the module context - raise it in protocore_config.h, which\n"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define CONFIG_STORE_CTX(w) ((ConfigStoreCtx *)(void *)((w) + CONFIG_STORE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_CONFIG_STORE_BORROW persistent bytes, or null while the pool was short
} ConfigStoreOwnCtx;
static ConfigStoreOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_config_store_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_CONFIG_STORE_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void config_store_begin(uint8_t *restrict work)
{
    (void)work;
    const char *ns = ConfigStore.begin_args.ns;

    if (!ns || !ns[0] || str.len(ns, PROTOCORE_CONFIG_KEY_MAX) >= PROTOCORE_CONFIG_KEY_MAX)
    {
        CONFIG_STORE_CTX(work)->ns[0] = '\0';
        ConfigStore.ok = PROTO_FALSE;
        return;
    }
    str.copy(CONFIG_STORE_CTX(work)->ns, ns, sizeof(CONFIG_STORE_CTX(work)->ns));
    CONFIG_STORE_CTX(work)->ns[PROTOCORE_CONFIG_KEY_MAX - 1] = '\0';
    ConfigStore.ok = PROTO_TRUE;
    return;
}

static void config_store_set_str(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStore.set_str_args.key;
    const char *val = ConfigStore.set_str_args.val;

    ConfigStore.ok = protocore_nvs_put_str(CONFIG_STORE_CTX(work)->ns, key, val);
    return;
}

static void config_store_get_str(uint8_t *restrict work)
{
    (void)work;
    ConfigStore.n = 0;
    const char *key = ConfigStore.get_str_args.key;
    char *out = ConfigStore.get_str_args.out;
    size_t out_cap = ConfigStore.get_str_args.out_cap;
    const char *def = ConfigStore.get_str_args.def;

    if (!out || out_cap == 0)
    {
        ConfigStore.n = 0;
        return;
    }
    size_t n = protocore_nvs_get_str(CONFIG_STORE_CTX(work)->ns, key, out, out_cap);
    if (n > 0)
    {
        ConfigStore.n = n;
        return;
    }
    // Absent: the default is this layer's, so the seam never has to know there is one.
    n = def ? str.len(def, out_cap) : 0;
    if (n > out_cap - 1)
    {
        n = out_cap - 1;
    }
    if (n)
    {
        mem.cpy(out, def, n);
    }
    out[n] = '\0';
    ConfigStore.n = n;
    return;
}

static void config_store_set_u32(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStore.set_u32_args.key;
    uint32_t val = ConfigStore.set_u32_args.val;

    ConfigStore.ok = protocore_nvs_put_u32(CONFIG_STORE_CTX(work)->ns, key, val);
    return;
}

static void config_store_get_u32(uint8_t *restrict work)
{
    (void)work;
    ConfigStore.ms = 0;
    const char *key = ConfigStore.get_u32_args.key;
    uint32_t def = ConfigStore.get_u32_args.def;

    ConfigStore.ms = protocore_nvs_get_u32(CONFIG_STORE_CTX(work)->ns, key, def);
    return;
}

static void config_store_set_blob(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStore.set_blob_args.key;
    const void *data = ConfigStore.set_blob_args.data;
    size_t len = ConfigStore.set_blob_args.len;

    ConfigStore.ok = protocore_nvs_put_blob(CONFIG_STORE_CTX(work)->ns, key, data, len);
    return;
}

static void config_store_get_blob(uint8_t *restrict work)
{
    (void)work;
    ConfigStore.n = 0;
    const char *key = ConfigStore.get_blob_args.key;
    void *out = ConfigStore.get_blob_args.out;
    size_t out_cap = ConfigStore.get_blob_args.out_cap;

    ConfigStore.n = protocore_nvs_get_blob(CONFIG_STORE_CTX(work)->ns, key, out, out_cap);
    return;
}

static void config_store_erase(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStore.erase_args.key;

    ConfigStore.ok = protocore_nvs_erase(CONFIG_STORE_CTX(work)->ns, key);
    return;
}

static void config_store_clear(uint8_t *restrict work)
{
    (void)work;

    ConfigStore.ok = protocore_nvs_clear(CONFIG_STORE_CTX(work)->ns);
    return;
}

ConfigStoreNs ConfigStore = {.begin = config_store_begin,
                             .set_str = config_store_set_str,
                             .get_str = config_store_get_str,
                             .set_u32 = config_store_set_u32,
                             .get_u32 = config_store_get_u32,
                             .set_blob = config_store_set_blob,
                             .get_blob = config_store_get_blob,
                             .erase = config_store_erase,
                             .clear = config_store_clear};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CONFIG_STORE
