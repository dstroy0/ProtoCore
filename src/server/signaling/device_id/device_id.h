// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file device_id.h
 * @brief Stable device UUID derived from the chip MAC (PROTOCORE_ENABLE_DEVICE_ID).
 *
 * protocore_uuid_from_mac() computes a deterministic RFC 4122 version-5 UUID from a
 * 6-byte MAC: namespace = the RFC 4122 DNS namespace, name = the lowercase MAC
 * hex, hashed with the library's SHA-1. The same MAC always yields the same
 * UUID, so it is a stable device identity (mDNS hostname, MQTT client ID, ...)
 * that needs no storage. protocore_device_uuid() reads the part's factory MAC
 * and formats it. Pure, host-testable core; no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DEVICE_ID_H
#define PROTOCORE_DEVICE_ID_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_DEVICE_ID

PROTOCORE_BEGIN_DECLS

/** @brief Length of a formatted UUID string including the null terminator. */
#define PROTOCORE_UUID_STR_LEN 37

/**
 * @brief Format a deterministic RFC 4122 v5 UUID from a 6-byte MAC.
 *
 * @param mac  six MAC bytes.
 * @param out  buffer of at least PROTOCORE_UUID_STR_LEN bytes; receives
 *             "xxxxxxxx-xxxx-5xxx-yxxx-xxxxxxxxxxxx" (lowercase, null-terminated).
 */
/** @brief The address a UUID is derived from, and where the text lands. */
typedef struct
{
    const uint8_t *mac; ///< the six address bytes a format reads, when the caller supplies them
    char *out;          ///< PROTOCORE_UUID_STR_LEN bytes the formatted UUID is written into
} DeviceIdArgs;

/**
 * @brief The stable MAC-derived device identity.
 *
 * @var DeviceIdNs::args      the address a UUID is derived from, and where the text lands
 * @var DeviceIdNs::from_mac  format a UUIDv5 from the caller's address
 * @var DeviceIdNs::uuid      format one from the part's own burned-in address
 *
 * No storage member: both calls write into the caller's buffer and hold nothing.
 */
typedef struct
{
    DeviceIdArgs args;
} DeviceIdVars;

/** @brief The operands and the outcome. */
extern DeviceIdVars DeviceIdV;

/** @brief The entries. */
typedef struct
{
    void (*const from_mac)(uint8_t *restrict work);
    void (*const uuid)(uint8_t *restrict work);
} DeviceIdNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DeviceIdV or a region of the borrow at a fixed offset.
void protocore_device_id_from_mac(uint8_t *restrict work);
void protocore_device_id_uuid(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `DeviceId.from_mac(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DeviceIdNs DeviceId __attribute__((unused)) = {
    .from_mac = protocore_device_id_from_mac,
    .uuid = protocore_device_id_uuid,
};

#if PROTOCORE_HAS_VENDOR_MAC
/**
 * @brief Format this device's UUID from its burned-in factory station MAC.
 * @param out  buffer of at least PROTOCORE_UUID_STR_LEN bytes.
 */

#endif

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DEVICE_ID

#endif // PROTOCORE_DEVICE_ID_H
