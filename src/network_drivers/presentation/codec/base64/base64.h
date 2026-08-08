// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file base64.h
 * @brief Base64 encoder/decoder.
 *
 * Used to encode the SHA-1 digest in the WebSocket handshake response
 * (RFC 6455 §4.2.2) and to decode Basic Auth credentials (RFC 7617).
 *
 * **Encode** is a portable software codec on every target (fast; it only handles
 * the public WebSocket-accept digest). **Decode** touches the secret Basic-auth
 * credentials, so on the ESP32 it uses mbedTLS's constant-time decoder (side-channel
 * hardened) and on the native test target the portable software decoder. See
 * base64.cpp and docs/FEATURE_PERFORMANCE.md section 2.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BASE64_H
#define PROTOCORE_BASE64_H

#include "protocore_config.h" // PROTO_BEGIN_DECLS: the .cpp benches and sketches include this header
#include <stddef.h>
#include <stdint.h>

PROTO_BEGIN_DECLS

/**
 * @brief The two alphabets, each in both directions.
 *
 * @var Base64Ns::encode      write @p src_len bytes as NUL-terminated base64; @p dst holds at least
 *                            `((src_len + 2) / 3) * 4 + 1` bytes
 * @var Base64Ns::decode      read a NUL-terminated base64 string into at most @p dst_cap bytes; the
 *                            count written, or 0 on an invalid character or an output past the cap.
 *                            A caller that terminates afterward passes one less than the buffer size
 * @var Base64Ns::url_encode  the same encode in the '-' '_' alphabet with no '=' padding
 *                            (RFC 4648 sec 5); the character count written, never longer than encode's
 * @var Base64Ns::url_decode  read @p src_len base64url characters, stopping at an '='. Strict: the
 *                            '+' '/' characters are refused, so a JWS segment (RFC 7515) decodes as
 *                            base64url alone and never as a mixed alphabet. Streaming, so the final
 *                            group may be 2 or 3 characters. Bounded by @p dst_cap like decode
 */
typedef struct
{
    void (*encode)(const uint8_t *src, size_t src_len, char *dst);
    size_t (*decode)(const char *src, uint8_t *dst, size_t dst_cap);
    size_t (*url_encode)(const uint8_t *src, size_t src_len, char *dst);
    size_t (*url_decode)(const char *src, size_t src_len, uint8_t *dst, size_t dst_cap);
} Base64Ns;

/** @brief The one symbol this module exports. */
extern const Base64Ns Base64;

PROTO_END_DECLS

#endif
