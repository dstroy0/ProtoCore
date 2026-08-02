// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config_store.h
 * @brief Typed NVS configuration store (PC_ENABLE_CONFIG_STORE).
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
 * (`PC_CONFIG_MAX_ENTRIES` x `PC_CONFIG_VAL_MAX`) so the typed contract is
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

#include "protocore_config.h"

#if PC_ENABLE_CONFIG_STORE

/**
 * @brief Open a configuration namespace (e.g. "wifi", "net"). Call once before
 *        get/set. On ESP32 this opens the NVS namespace read-write.
 * @return true on success.
 */
proto_bool pc_config_begin(const char *ns);

/** @brief Store a string value. @return true on success. */
proto_bool pc_config_set_str(const char *key, const char *val);

/**
 * @brief Read a string value into @p out (always null-terminated, bounded by
 *        @p out_cap). Returns @p def when the key is absent.
 * @return number of characters written (excluding the null terminator).
 */
size_t pc_config_get_str(const char *key, char *out, size_t out_cap, const char *def);

/** @brief Store a `uint32_t` value. @return true on success. */
proto_bool pc_config_set_u32(const char *key, uint32_t val);

/** @brief Read a `uint32_t` value, or @p def if the key is absent. */
uint32_t pc_config_get_u32(const char *key, uint32_t def);

/** @brief Store a raw blob. @return true on success. */
proto_bool pc_config_set_blob(const char *key, const void *data, size_t len);

/**
 * @brief Read a blob into @p out (bounded by @p out_cap).
 * @return number of bytes written (0 if the key is absent).
 */
size_t pc_config_get_blob(const char *key, void *out, size_t out_cap);

/** @brief Erase a single key. @return true if the key existed. */
proto_bool pc_config_erase(const char *key);

/** @brief Erase every key in the open namespace. @return true on success. */
proto_bool pc_config_clear(void);

#endif // PC_ENABLE_CONFIG_STORE
#endif // PROTOCORE_CONFIG_STORE_H
