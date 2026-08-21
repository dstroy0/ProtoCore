// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SHA1_H
#define PROTOCORE_SHA1_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file sha1.h
 * @brief SHA-1 (FIPS 180-4) - one-shot digest.
 *
 * The shared SHA-1 primitive. On Arduino (ESP32) delegates to the hardware SHA accelerator; on native
 * builds a portable software implementation is used. Used for the WebSocket opening handshake
 * (RFC 6455 §4.2.2) and other legacy digest needs. Output is always 20 bytes.
 *
 * @c work is PROTOCORE_SHA1_BORROW secure bytes the CALLER took, at an address it knows. It is not held past the call,
 * so nothing here aliases it. The caller releases it, and the pool wipes on release; this module neither takes it,
 * holds it, releases it, nor wipes it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief SHA-1 digest length in bytes. */
#define PROTOCORE_SHA1_DIGEST_LEN 20

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*hash)(uint8_t *, const uint8_t *, size_t, uint8_t *);
} Sha1Ns;
PROTOCORE_NS_LAYOUT(Sha1Ns, hash);

/**
 * @brief Digest the whole message in one call.
 * @param work PROTOCORE_SHA1_BORROW bytes the caller took. Not held past the call.
 * @param data the bytes
 * @param len how many
 * @param out PROTOCORE_SHA1_DIGEST_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sha1_hash(uint8_t *work, const uint8_t *data, size_t len, uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS Sha1Ns Sha1 PROTOCORE_UNUSED = {.hash = protocore_sha1_hash};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SHA1_H
