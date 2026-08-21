// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_QPACK_H
#define PROTOCORE_QPACK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file qpack.h
 * @brief QPACK field-section compression for HTTP/3 (RFC 9204).
 *
 * QPACK is HTTP/3's header compression. It reuses RFC 7541's prefix-integer coding and Huffman
 * code (shared here via protocore_hpack_prim.h) and adds a 99-entry static table, an encoded field-section
 * prefix, and its own field-line representations.
 *
 * This codec is static-table-only and needs no per-connection state: the encoder emits indexed /
 * literal representations against the static table (never inserting into a dynamic table), and it
 * advertises SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0, so a conformant peer's encoder never sends a
 * dynamic-table reference. The decoder therefore rejects (returns false) any representation that
 * references the dynamic table or a non-zero Required Insert Count. Pure, zero heap, host-tested
 * against the RFC 9204 Appendix B.1 worked example.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_QPACK_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Callback invoked for each decoded header; return false to abort the decode. */
typedef proto_bool (*QpackEmitFn)(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len);

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*encode_prefix)(uint8_t *, uint8_t *, size_t);
    size_t (*encode_header)(uint8_t *, uint8_t *, size_t, const char *, size_t, const char *, size_t);
    proto_bool (*decode)(uint8_t *, const uint8_t *, size_t, char *, size_t, QpackEmitFn, void *);
} QpackNs;
PROTOCORE_NS_LAYOUT(QpackNs, encode_prefix, encode_header, decode);

/**
 * @brief Write the encoded field-section prefix for a static-only section. .
 * @param work PROTOCORE_QPACK_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_qpack_encode_prefix(uint8_t *work, uint8_t *out, size_t cap);
/**
 * @brief Encode one header field (server side): a full static match -> .
 * @param work PROTOCORE_QPACK_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param name Name
 * @param name_len Name len
 * @param value Value
 * @param value_len Value len
 * @return The size_t.
 */
size_t protocore_qpack_encode_header(uint8_t *work, uint8_t *out, size_t cap, const char *name, size_t name_len,
                                     const char *value, size_t value_len);
/**
 * @brief Decode a whole QPACK field section (prefix + representations), .
 * @param work PROTOCORE_QPACK_BORROW bytes the caller took. Not held past the call.
 * @param block Block
 * @param len Len
 * @param scratch caller buffer holding one header's name+value during each emit call
 * @param scratch_cap Scratch cap
 * @param emit Emit
 * @param ctx Ctx
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_qpack_decode(uint8_t *work, const uint8_t *block, size_t len, char *scratch, size_t scratch_cap,
                                  QpackEmitFn emit, void *ctx);

/** @brief Callback invoked for each decoded header; return false to abort the decode. */
typedef proto_bool (*QpackEmitFn)(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len);

/** @brief Module namespace. */
PROTOCORE_NS QpackNs Qpack PROTOCORE_UNUSED = {.encode_prefix = protocore_qpack_encode_prefix,
                                               .encode_header = protocore_qpack_encode_header,
                                               .decode = protocore_qpack_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_QPACK_H
