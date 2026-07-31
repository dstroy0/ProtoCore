// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha1.h
 * @brief SHA-1 (FIPS 180-4) - one-shot digest.
 *
 * The shared SHA-1 primitive. On Arduino (ESP32) delegates to mbedtls_sha1() (hardware SHA
 * accelerator); on native builds a portable software implementation is used. Used for the WebSocket
 * opening handshake (RFC 6455 §4.2.2) and other legacy digest needs. Output is always 20 bytes.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA1_H
#define PROTOCORE_SHA1_H

#include <stddef.h>
#include <stdint.h>

/** @brief SHA-1 digest length in bytes. */
#define PC_SHA1_DIGEST_LEN 20

/**
 * @brief Compute a SHA-1 digest over an arbitrary byte buffer.
 *
 * @param data    Input bytes.
 * @param len     Number of input bytes.
 * @param digest  Output buffer; must be at least PC_SHA1_DIGEST_LEN bytes.
 */
void pc_sha1(const uint8_t *data, size_t len, uint8_t digest[PC_SHA1_DIGEST_LEN]);

#endif // PROTOCORE_SHA1_H
