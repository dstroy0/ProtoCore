// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_TRANSPORT_ZLIB_H
#define PROTOCORE_TRANSPORT_ZLIB_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file zlib.h
 * @brief RFC 1950 / 1951 deflate, as SSH negotiates it.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_ZLIB_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Hash-table buckets for the LZ77 3-byte match search (2^bits). */
#define SSH_ZLIB_HASH_BITS 13
#define SSH_ZLIB_HASH_SIZE (1 << SSH_ZLIB_HASH_BITS)

/** @brief Work buffer capacity the compressor needs: window history + one input payload. */
#define SSH_ZLIB_WORK_SIZE ((size_t)PROTOCORE_SSH_ZLIB_WINDOW + (size_t)PROTOCORE_SSH_ZLIB_MAX_IN)

/**
 * @brief Streaming server-to-client DEFLATE compressor (one per SSH connection).
 *
 * The window (history) lives at the front of @ref work; @ref hist bytes are valid. Hash chains
 * (@ref head / @ref prev) are rebuilt over the history each packet, so a slid buffer needs no chain
 * fix-up. All pointers are caller-owned; ssh_deflate_init() wires them and seeds the fixed tables.
 */
typedef struct
{
    uint8_t *work;          ///< history+input work buffer, capacity SSH_ZLIB_WORK_SIZE.
    uint16_t *head;         ///< hash bucket heads, SSH_ZLIB_HASH_SIZE entries.
    uint16_t *prev;         ///< hash chain (absolute-position indexed), SSH_ZLIB_WORK_SIZE entries.
    uint16_t *ll_code;      ///< fixed literal/length Huffman codes (bit-reversed), 288 entries.
    uint8_t *ll_len;        ///< their bit lengths, 288 entries.
    uint16_t *d_code;       ///< fixed distance Huffman codes (bit-reversed), 30 entries.
    uint8_t *d_len;         ///< their bit lengths, 30 entries.
    size_t hist;            ///< bytes of history currently at the front of @ref work.
    proto_bool header_sent; ///< true once the leading 2-byte zlib header has been emitted.
} SshDeflate;

/**
 * @brief Worst-case compressed size for @p src_len input (header + block overhead + sync marker).
 *
 * Callers size @p dst with this. Fixed-Huffman can expand incompressible data slightly; the bound
 * covers the 2-byte header, per-byte worst case, end-of-block, and the 4-byte sync marker.
 */
static inline size_t ssh_deflate_bound(size_t src_len)
{
    return 2 + src_len + (src_len >> 3) + 32;
}

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*init)(uint8_t *restrict, SshDeflate *, uint8_t *, uint16_t *, uint16_t *, uint16_t *, uint8_t *, uint16_t *,
                 uint8_t *);
    int (*packet)(uint8_t *restrict, SshDeflate *, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
} ZlibNs;
PROTOCORE_NS_LAYOUT(ZlibNs, init, packet);

/**
 * @brief Bind caller memory to a compressor and reset it to stream start.
 * @param work PROTOCORE_ZLIB_BORROW bytes the caller took. Not held past the call.
 * @param z the compressor to initialize
 * @param win history+input buffer, >= SSH_ZLIB_WORK_SIZE bytes
 * @param head hash heads, SSH_ZLIB_HASH_SIZE uint16 entries
 * @param prev hash chain, SSH_ZLIB_WORK_SIZE uint16 entries
 * @param ll_code Ll code
 * @param ll_len Ll len
 * @param d_code D code
 * @param d_len D len
 */
void protocore_zlib_init(uint8_t *restrict work, SshDeflate *z, uint8_t *win, uint16_t *head, uint16_t *prev,
                         uint16_t *ll_code, uint8_t *ll_len, uint16_t *d_code, uint8_t *d_len);
/**
 * @brief Compress one packet payload, continuing the session's zlib stream. .
 * @param work PROTOCORE_ZLIB_BORROW bytes the caller took. Not held past the call.
 * @param z the compressor
 * @param src Src
 * @param src_len Src len
 * @param dst Dst
 * @param dst_cap Dst cap
 * @param out_len set to the compressed length on success
 * @return The int.
 */
int protocore_zlib_packet(uint8_t *restrict work, SshDeflate *z, const uint8_t *src, size_t src_len, uint8_t *dst,
                          size_t dst_cap, size_t *out_len);

/** @brief Module namespace. */
PROTOCORE_NS ZlibNs Zlib PROTOCORE_UNUSED = {.init = protocore_zlib_init, .packet = protocore_zlib_packet};

PROTOCORE_END_DECLS

#endif // PROTOCORE_TRANSPORT_ZLIB_H
