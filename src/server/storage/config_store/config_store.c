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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CONFIG_STORE

#include "config_store.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from

#include "test/core_setup/hal/nvs.h"

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

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    CONFIG_STORE_OFF_CTX % _Alignof(ConfigStoreCtx) == 0,
    "CONFIG_STORE_OFF_CTX is not a multiple of alignof(ConfigStoreCtx) - CONFIG_STORE_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define CONFIG_STORE_CTX(w) ((ConfigStoreCtx *)(void *)((w) + CONFIG_STORE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_CONFIG_STORE_BORROW persistent bytes
} ConfigStoreOwnCtx;
static ConfigStoreOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_config_store_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_CONFIG_STORE_BORROW).buf;
    }
    return s_own.span;
}

void protocore_config_store_begin(uint8_t *restrict work)
{
    (void)work;
    const char *ns = ConfigStoreV.begin_args.ns;

    if (!ns || !ns[0] || str.len(ns, PROTOCORE_CONFIG_KEY_MAX) >= PROTOCORE_CONFIG_KEY_MAX)
    {
        CONFIG_STORE_CTX(work)->ns[0] = '\0';
        ConfigStoreV.ok = PROTO_FALSE;
        return;
    }
    str.copy(CONFIG_STORE_CTX(work)->ns, ns, sizeof(CONFIG_STORE_CTX(work)->ns));
    CONFIG_STORE_CTX(work)->ns[PROTOCORE_CONFIG_KEY_MAX - 1] = '\0';
    ConfigStoreV.ok = PROTO_TRUE;
    return;
}

void protocore_config_store_set_str(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStoreV.set_str_args.key;
    const char *val = ConfigStoreV.set_str_args.val;

    ConfigStoreV.ok = protocore_nvs_put_str(CONFIG_STORE_CTX(work)->ns, key, val);
    return;
}

void protocore_config_store_get_str(uint8_t *restrict work)
{
    (void)work;
    ConfigStoreV.n = 0;
    const char *key = ConfigStoreV.get_str_args.key;
    char *out = ConfigStoreV.get_str_args.out;
    size_t out_cap = ConfigStoreV.get_str_args.out_cap;
    const char *def = ConfigStoreV.get_str_args.def;

    if (!out || out_cap == 0)
    {
        ConfigStoreV.n = 0;
        return;
    }
    size_t n = protocore_nvs_get_str(CONFIG_STORE_CTX(work)->ns, key, out, out_cap);
    if (n > 0)
    {
        ConfigStoreV.n = n;
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
    ConfigStoreV.n = n;
    return;
}

void protocore_config_store_set_u32(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStoreV.set_u32_args.key;
    uint32_t val = ConfigStoreV.set_u32_args.val;

    ConfigStoreV.ok = protocore_nvs_put_u32(CONFIG_STORE_CTX(work)->ns, key, val);
    return;
}

void protocore_config_store_get_u32(uint8_t *restrict work)
{
    (void)work;
    ConfigStoreV.ms = 0;
    const char *key = ConfigStoreV.get_u32_args.key;
    uint32_t def = ConfigStoreV.get_u32_args.def;

    ConfigStoreV.ms = protocore_nvs_get_u32(CONFIG_STORE_CTX(work)->ns, key, def);
    return;
}

void protocore_config_store_set_blob(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStoreV.set_blob_args.key;
    const void *data = ConfigStoreV.set_blob_args.data;
    size_t len = ConfigStoreV.set_blob_args.len;

    ConfigStoreV.ok = protocore_nvs_put_blob(CONFIG_STORE_CTX(work)->ns, key, data, len);
    return;
}

void protocore_config_store_get_blob(uint8_t *restrict work)
{
    (void)work;
    ConfigStoreV.n = 0;
    const char *key = ConfigStoreV.get_blob_args.key;
    void *out = ConfigStoreV.get_blob_args.out;
    size_t out_cap = ConfigStoreV.get_blob_args.out_cap;

    ConfigStoreV.n = protocore_nvs_get_blob(CONFIG_STORE_CTX(work)->ns, key, out, out_cap);
    return;
}

void protocore_config_store_erase(uint8_t *restrict work)
{
    (void)work;
    const char *key = ConfigStoreV.erase_args.key;

    ConfigStoreV.ok = protocore_nvs_erase(CONFIG_STORE_CTX(work)->ns, key);
    return;
}

void protocore_config_store_clear(uint8_t *restrict work)
{
    (void)work;

    ConfigStoreV.ok = protocore_nvs_clear(CONFIG_STORE_CTX(work)->ns);
    return;
}

/** @brief The operands and the outcome. */
ConfigStoreVars ConfigStoreV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CONFIG_STORE
