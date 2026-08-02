// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file device_id.h
 * @brief Stable device UUID derived from the chip MAC (PC_ENABLE_DEVICE_ID).
 *
 * pc_uuid_from_mac() computes a deterministic RFC 4122 version-5 UUID from a
 * 6-byte MAC: namespace = the RFC 4122 DNS namespace, name = the lowercase MAC
 * hex, hashed with the library's SHA-1. The same MAC always yields the same
 * UUID, so it is a stable device identity (mDNS hostname, MQTT client ID, ...)
 * that needs no storage. pc_device_uuid() reads the ESP32 factory MAC and
 * formats it (ESP32 only). Pure, host-testable core; no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DEVICE_ID_H
#define PROTOCORE_DEVICE_ID_H

#include "protocore_config.h"

#if PC_ENABLE_DEVICE_ID

/** @brief Length of a formatted UUID string including the null terminator. */
#define PC_UUID_STR_LEN 37

/**
 * @brief Format a deterministic RFC 4122 v5 UUID from a 6-byte MAC.
 *
 * @param mac  six MAC bytes.
 * @param out  buffer of at least PC_UUID_STR_LEN bytes; receives
 *             "xxxxxxxx-xxxx-5xxx-yxxx-xxxxxxxxxxxx" (lowercase, null-terminated).
 */
void pc_uuid_from_mac(const uint8_t mac[6], char out[PC_UUID_STR_LEN]);

#if PROTOCORE_HOT
/**
 * @brief Format this device's UUID from its ESP32 factory (WiFi STA) MAC.
 * @param out  buffer of at least PC_UUID_STR_LEN bytes.
 */
void pc_device_uuid(char out[PC_UUID_STR_LEN]);
#endif

#endif // PC_ENABLE_DEVICE_ID
#endif // PROTOCORE_DEVICE_ID_H
