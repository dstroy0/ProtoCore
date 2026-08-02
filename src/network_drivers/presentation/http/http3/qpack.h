// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file qpack.h
 * @brief QPACK field-section compression for HTTP/3 (RFC 9204).
 *
 * QPACK is HTTP/3's header compression. It reuses RFC 7541's prefix-integer coding and Huffman
 * code (shared here via pc_hpack_prim.h) and adds a 99-entry static table, an encoded field-section
 * prefix, and its own field-line representations.
 *
 * This codec is static-table-only and needs no per-connection state: the encoder emits indexed /
 * literal representations against the static table (never inserting into a dynamic table), and it
 * advertises SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0, so a conformant peer's encoder never sends a
 * dynamic-table reference. The decoder therefore rejects (returns false) any representation that
 * references the dynamic table or a non-zero Required Insert Count. Pure, zero heap, host-tested
 * against the RFC 9204 Appendix B.1 worked example.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QPACK_H
#define PROTOCORE_QPACK_H

#include "protocore_config.h"

#if PC_ENABLE_HTTP3

/** @brief Callback invoked for each decoded header; return false to abort the decode. */
typedef proto_bool (*QpackEmitFn)(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len);

/**
 * @brief Write the encoded field-section prefix for a static-only section.
 * Required Insert Count = 0, Base = 0 -> the two bytes {0x00, 0x00} (RFC 9204 sec 4.5.1).
 * @return bytes written (2), or 0 if @p cap < 2.
 */
size_t pc_qpack_encode_prefix(uint8_t *out, size_t cap);

/**
 * @brief Encode one header field (server side): a full static match -> Indexed Field Line; a name
 * match -> Literal Field Line with (static) Name Reference; otherwise Literal Field Line with
 * Literal Name. Strings are Huffman-coded when that is not longer.
 * @return bytes written, or 0 on overflow. A complete field section is pc_qpack_encode_prefix()
 * followed by one pc_qpack_encode_header() per field.
 */
size_t pc_qpack_encode_header(uint8_t *out, size_t cap, const char *name, size_t name_len, const char *value,
                              size_t value_len);

/**
 * @brief Decode a whole QPACK field section (prefix + representations), emitting each header.
 * @param scratch  caller buffer holding one header's name+value during each emit call.
 * @return true if the section decoded cleanly; false on malformed input, overflow, or any
 * dynamic-table reference (indexed/literal post-base, dynamic name/index, or non-zero RIC).
 */
proto_bool pc_qpack_decode(const uint8_t *block, size_t len, char *scratch, size_t scratch_cap, QpackEmitFn emit,
                           void *ctx);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_QPACK_H
