// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nvs.h
 * @brief Non-volatile key-value storage, in the library's own terms.
 *
 * Small named values that outlive a reboot: a WiFi passphrase, an SSH host key, a config field.
 * Every operation names its namespace and its key and completes on its own, so there is no handle
 * to keep, no pool to size, and no open namespace whose lifetime can outlast its caller.
 *
 * Two backends, selected by the vendor: hal/esp/esp_nvs.cpp over the Arduino `Preferences` NVS
 * wrapper, and hal/host/host_nvs.c over a fixed table sized by PC_CONFIG_MAX_ENTRIES,
 * PC_CONFIG_KEY_MAX and PC_CONFIG_VAL_MAX. The host table is what makes every caller above this
 * seam testable on the host.
 *
 * NVS caps a key at 15 characters plus the terminator (PC_CONFIG_KEY_MAX); a longer key is
 * refused rather than truncated.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NVS_H
#define PROTOCORE_NVS_H

#include "protocore_config.h" // the entry point: proto_bool, size_t, the PC_CONFIG_* table sizes

PROTO_BEGIN_DECLS

/** @brief True if @p key exists in @p ns. */
proto_bool pc_nvs_has(const char *ns, const char *key);

/**
 * @brief Copy the bytes stored at @p ns / @p key into @p out.
 * @return the byte count written, or 0 if the key is absent, @p out is null, or @p cap is 0.
 */
size_t pc_nvs_get_blob(const char *ns, const char *key, void *out, size_t cap);

/** @brief Store @p len bytes at @p ns / @p key, replacing any current value. */
proto_bool pc_nvs_put_blob(const char *ns, const char *key, const void *in, size_t len);

/**
 * @brief Copy the string stored at @p ns / @p key into @p out, always null-terminated.
 *
 * No default: a caller that wants one applies it on a 0 return, so this seam decides nothing.
 *
 * @return the character count written excluding the terminator, or 0 if the key is absent.
 */
size_t pc_nvs_get_str(const char *ns, const char *key, char *out, size_t cap);

/** @brief Store the null-terminated @p val at @p ns / @p key. */
proto_bool pc_nvs_put_str(const char *ns, const char *key, const char *val);

/** @brief Read the 32-bit value at @p ns / @p key, or @p def when the key is absent. */
uint32_t pc_nvs_get_u32(const char *ns, const char *key, uint32_t def);

/** @brief Store @p val at @p ns / @p key. */
proto_bool pc_nvs_put_u32(const char *ns, const char *key, uint32_t val);

/** @brief Drop @p key from @p ns. False if it was not there. */
proto_bool pc_nvs_erase(const char *ns, const char *key);

/** @brief Drop every key in @p ns. */
proto_bool pc_nvs_clear(const char *ns);

PROTO_END_DECLS

#endif // PROTOCORE_NVS_H
