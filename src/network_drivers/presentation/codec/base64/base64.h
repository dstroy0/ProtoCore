// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_BASE64

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What encode takes: src, src_len, dst. */
typedef struct
{
    const uint8_t *src;
    size_t src_len;
    char *dst;
} Base64EncodeArgs;
/** @brief What decode takes: src, dst, dst_cap. */
typedef struct
{
    const char *src;
    uint8_t *dst;
    size_t dst_cap;
} Base64DecodeArgs;
/** @brief What url_encode takes: src, src_len, dst. */
typedef struct
{
    const uint8_t *src;
    size_t src_len;
    char *dst;
} Base64UrlEncodeArgs;
/** @brief What url_decode takes: src, src_len, dst, dst_cap. */
typedef struct
{
    const char *src;
    size_t src_len;
    uint8_t *dst;
    size_t dst_cap;
} Base64UrlDecodeArgs;
/**
 * @brief Base64 encoder/decoder.
 *
 * A caller sets the members a call takes, invokes it through ::Base64 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Base64.encode_args.src = ...;
 *   Base64.encode_args.src_len = ...;
 *   Base64.encode_args.dst = ...;
 *   Base64.encode(work);
 *
 * @var Base64Ns::encode_args  what encode takes: src, src_len, dst
 * @var Base64Ns::decode_args  what decode takes: src, dst, dst_cap
 * @var Base64Ns::url_encode_args  what url_encode takes: src, src_len, dst
 * @var Base64Ns::url_decode_args  what url_decode takes: src, src_len, dst, dst_cap
 * @var Base64Ns::ok  a call's true/false outcome
 * @var Base64Ns::n  the count a call reports
 * @var Base64Ns::encode  write src_len bytes as NUL-terminated base64; dst holds at least
 * @var Base64Ns::decode  read a NUL-terminated base64 string into at most dst_cap bytes; the
 * @var Base64Ns::url_encode  the same encode in the '-' '_' alphabet with no '=' padding
 * @var Base64Ns::url_decode  read src_len base64url characters, stopping at an '='. Strict: the
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Base64EncodeArgs encode_args;
    Base64DecodeArgs decode_args;
    Base64UrlEncodeArgs url_encode_args;
    Base64UrlDecodeArgs url_decode_args;
    proto_bool ok;
    size_t n;
} Base64Vars;

/** @brief The operands and the outcome. */
extern Base64Vars Base64V;

/** @brief The entries. */
typedef struct
{
    void (*const encode)(uint8_t *restrict work);
    void (*const decode)(uint8_t *restrict work);
    void (*const url_encode)(uint8_t *restrict work);
    void (*const url_decode)(uint8_t *restrict work);
} Base64Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Base64V or a region of the borrow at a fixed offset.
void protocore_base64_encode(uint8_t *restrict work);
void protocore_base64_decode(uint8_t *restrict work);
void protocore_base64_url_encode(uint8_t *restrict work);
void protocore_base64_url_decode(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Base64.encode(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Base64Ns Base64 __attribute__((unused)) = {
    .encode = protocore_base64_encode,
    .decode = protocore_base64_decode,
    .url_encode = protocore_base64_url_encode,
    .url_decode = protocore_base64_url_decode,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BASE64

#endif // PROTOCORE_BASE64_H
