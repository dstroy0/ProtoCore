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

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Encode @p src_len bytes of @p src as Base64.
 *
 * Writes a null-terminated string into @p dst.  @p dst must be at least
 * `((src_len + 2) / 3) * 4 + 1` bytes.
 *
 * @param src     Input bytes.
 * @param src_len Number of input bytes.
 * @param dst     Output buffer (null-terminated Base64 string).
 */
void pc_base64_encode(const uint8_t *src, size_t src_len, char *dst);

/**
 * @brief Decode a null-terminated Base64 string.
 *
 * Writes decoded bytes into @p dst, never writing more than @p dst_cap bytes.
 * Returns the number of decoded bytes, or 0 on invalid input or if the decoded
 * output would exceed @p dst_cap (the write is bounded - no overflow). The
 * caller must leave room for any terminator it adds afterward (pass a capacity
 * one less than the buffer size if it will null-terminate at the returned
 * length).
 *
 * @param src     Null-terminated Base64 input string.
 * @param dst     Output byte buffer.
 * @param dst_cap Capacity of @p dst in bytes.
 * @return        Number of bytes written to @p dst, or 0 on error / overflow.
 */
size_t pc_base64_decode(const char *src, uint8_t *dst, size_t dst_cap);

/**
 * @brief Encode @p src_len bytes as base64url (RFC 4648 section 5): '-' / '_' in
 *        place of '+' / '/', and no '=' padding.
 *
 * Writes a null-terminated string into @p dst, which must hold at least
 * `((src_len + 2) / 3) * 4 + 1` bytes (the same as pc_base64_encode; the URL form is
 * never longer). @return the number of characters written.
 */
size_t pc_base64url_encode(const uint8_t *src, size_t src_len, char *dst);

/**
 * @brief Decode @p src_len characters of base64url (RFC 4648 section 5, '-'/'_'
 *        alphabet; an '=' ends the input).
 *
 * Strict: the standard '+'/'/' characters are rejected so a JWS/JWT segment is
 * decoded as base64url only (RFC 7515), never as a mixed alphabet. Streaming
 * decoder - no padding or buffer length restriction. Writes at most @p dst_cap
 * bytes; returns the number written, or 0 on an invalid character or if the
 * output would exceed @p dst_cap (the write is bounded - no overflow).
 */
size_t pc_base64url_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_cap);

#endif
