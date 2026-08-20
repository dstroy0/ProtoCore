// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_store.h
 * @brief Typed NVS configuration store (PROTOCORE_ENABLE_CONFIG_STORE).
 *
 * A small typed key/value API for core device settings - WiFi credentials, IP
 * configuration, feature toggles - that routes them into the ESP32's native
 * Non-Volatile Storage (NVS) partition as binary entries, rather than a JSON
 * text file on the filesystem. NVS is wear-levelled and independent of the
 * LittleFS/SPIFFS partition, so configuration survives a filesystem corruption
 * and credentials live in the storage area meant for them.
 *
 * Three value types: null-terminated strings, `uint32_t`, and raw blobs - each
 * with a default returned when the key is absent. On ESP32 the backend is the
 * Arduino `Preferences` NVS wrapper; on host builds it is a fixed in-memory table
 * (`PROTOCORE_CONFIG_MAX_ENTRIES` x `PROTOCORE_CONFIG_VAL_MAX`) so the typed contract is
 * unit-testable without flash.
 *
 * Writes hit NVS, so call the setters at provisioning / config time, not in the
 * request hot path. Keys are limited to 15 chars (NVS), plus null.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CONFIG_STORE_H
#define PROTOCORE_CONFIG_STORE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CONFIG_STORE

PROTOCORE_BEGIN_DECLS

// PROTOCORE_CONFIG_STORE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums it
// into its arena. A caller takes them once and passes the pointer to every call. How they are
// carved is this module's and is never named here.

/** @brief What begin takes. */
typedef struct
{
    const char *ns;
} ConfigStoreBeginArgs;
/** @brief What set_str takes. */
typedef struct
{
    const char *key;
    const char *val;
} ConfigStoreSetStrArgs;
/** @brief What get_str takes. */
typedef struct
{
    const char *key;
    char *out;
    size_t out_cap;
    const char *def;
} ConfigStoreGetStrArgs;
/** @brief What set_u32 takes. */
typedef struct
{
    const char *key;
    uint32_t val;
} ConfigStoreSetU32Args;
/** @brief What get_u32 takes. */
typedef struct
{
    const char *key;
    uint32_t def;
} ConfigStoreGetU32Args;
/** @brief What set_blob takes. */
typedef struct
{
    const char *key;
    const void *data;
    size_t len;
} ConfigStoreSetBlobArgs;
/** @brief What get_blob takes. */
typedef struct
{
    const char *key;
    void *out;
    size_t out_cap;
} ConfigStoreGetBlobArgs;
/** @brief What erase takes. */
typedef struct
{
    const char *key;
} ConfigStoreEraseArgs;
typedef struct
{
    ConfigStoreBeginArgs begin_args;
    ConfigStoreSetStrArgs set_str_args;
    ConfigStoreGetStrArgs get_str_args;
    ConfigStoreSetU32Args set_u32_args;
    ConfigStoreGetU32Args get_u32_args;
    ConfigStoreSetBlobArgs set_blob_args;
    ConfigStoreGetBlobArgs get_blob_args;
    ConfigStoreEraseArgs erase_args;
    proto_bool ok;
    int n;
    uint32_t ms;
} ConfigStoreVars;

/** @brief The operands and the outcome. */
extern ConfigStoreVars ConfigStoreV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
    void (*const set_str)(uint8_t *restrict work);
    void (*const get_str)(uint8_t *restrict work);
    void (*const set_u32)(uint8_t *restrict work);
    void (*const get_u32)(uint8_t *restrict work);
    void (*const set_blob)(uint8_t *restrict work);
    void (*const get_blob)(uint8_t *restrict work);
    void (*const erase)(uint8_t *restrict work);
    void (*const clear)(uint8_t *restrict work);
} ConfigStoreNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ConfigStoreV or a region of the borrow at a fixed offset.
void protocore_config_store_begin(uint8_t *restrict work);
void protocore_config_store_set_str(uint8_t *restrict work);
void protocore_config_store_get_str(uint8_t *restrict work);
void protocore_config_store_set_u32(uint8_t *restrict work);
void protocore_config_store_get_u32(uint8_t *restrict work);
void protocore_config_store_set_blob(uint8_t *restrict work);
void protocore_config_store_get_blob(uint8_t *restrict work);
void protocore_config_store_erase(uint8_t *restrict work);
void protocore_config_store_clear(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `ConfigStore.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ConfigStoreNs ConfigStore __attribute__((unused)) = {
    .begin = protocore_config_store_begin,
    .set_str = protocore_config_store_set_str,
    .get_str = protocore_config_store_get_str,
    .set_u32 = protocore_config_store_set_u32,
    .get_u32 = protocore_config_store_get_u32,
    .set_blob = protocore_config_store_set_blob,
    .get_blob = protocore_config_store_get_blob,
    .erase = protocore_config_store_erase,
    .clear = protocore_config_store_clear,
};

/**
 * @brief The PROTOCORE_CONFIG_STORE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_config_store_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CONFIG_STORE

#endif // PROTOCORE_CONFIG_STORE_H
