// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_TRANSPORT_COMP_H
#define PROTOCORE_TRANSPORT_COMP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file comp.h
 * @brief RFC 4253 sec 6.2 compression: the negotiated stream, both directions.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_COMP_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Negotiated compression algorithm, held per direction. */
typedef enum PROTO_ENUM_PACKED
{
    SSH_COMP_NONE = 0,        ///< no compression
    SSH_COMP_ZLIB = 1,        ///< "zlib" (RFC 4253) - starts right after NEWKEYS
    SSH_COMP_ZLIB_DELAYED = 2 ///< "zlib@openssh.com" - starts after SSH_MSG_USERAUTH_SUCCESS
} SshCompAlg;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*reset)(uint8_t *, uint8_t);
    void (*set_s2c)(uint8_t *, uint8_t, SshCompAlg);
    void (*on_newkeys)(uint8_t *, uint8_t);
    void (*on_auth_success)(uint8_t *, uint8_t);
    proto_bool (*s2c_active)(uint8_t *, uint8_t);
    int (*s2c)(uint8_t *, uint8_t, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
    void (*set_c2s)(uint8_t *, uint8_t, SshCompAlg);
    proto_bool (*c2s_active)(uint8_t *, uint8_t);
    int (*c2s)(uint8_t *, uint8_t, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
} CompNs;
PROTOCORE_NS_LAYOUT(CompNs, reset, set_s2c, on_newkeys, on_auth_success, s2c_active, s2c, set_c2s, c2s_active, c2s);

/**
 * @brief Reset compression state for slot i (fresh connection). Does NOT run .
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 */
void protocore_comp_reset(uint8_t *work, uint8_t i);
/**
 * @brief Record the s2c algorithm negotiated in KEXINIT (::SshCompAlg).
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 * @param alg Alg
 */
void protocore_comp_set_s2c(uint8_t *work, uint8_t i, SshCompAlg alg);
/**
 * @brief NEWKEYS completed: start the stream now if `zlib` was negotiated .
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 */
void protocore_comp_on_newkeys(uint8_t *work, uint8_t i);
/**
 * @brief SSH_MSG_USERAUTH_SUCCESS sent: start the stream if .
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 */
void protocore_comp_on_auth_success(uint8_t *work, uint8_t i);
/**
 * @brief True once the s2c stream is active and outbound payloads must be .
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_comp_s2c_active(uint8_t *work, uint8_t i);
/**
 * @brief Compress one outbound payload, continuing the session's zlib stream.
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 * @param src Src
 * @param src_len Src len
 * @param dst Dst
 * @param dst_cap Dst cap
 * @param out_len Out len
 * @return The int.
 */
int protocore_comp_s2c(uint8_t *work, uint8_t i, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                       size_t *out_len);
/**
 * @brief Record the client-to-server algorithm negotiated in KEXINIT .
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 * @param alg Alg
 */
void protocore_comp_set_c2s(uint8_t *work, uint8_t i, SshCompAlg alg);
/**
 * @brief True once the c2s stream is active and inbound payloads must be .
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_comp_c2s_active(uint8_t *work, uint8_t i);
/**
 * @brief Decompress one inbound payload, continuing the session's .
 * @param work PROTOCORE_COMP_BORROW bytes the caller took. Not held past the call.
 * @param i I
 * @param src Src
 * @param src_len Src len
 * @param dst Dst
 * @param dst_cap Dst cap
 * @param out_len Out len
 * @return The int.
 */
int protocore_comp_c2s(uint8_t *work, uint8_t i, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                       size_t *out_len);

/**
 * @brief The bytes every entry here runs out of: one compressor per SSH connection.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. The transport and the server both drive the same connection's streams, so the
 * bytes belong to this module rather than to either caller. Taken once from the end of the plaintext
 * pool, which no mark and no release walks, because a zlib@openssh.com stream takes its window over
 * from one packet to the next.
 *
 * @return the span.
 */
uint8_t *protocore_ssh_comp_span(void);

/** @brief Module namespace. */
PROTOCORE_NS CompNs Comp PROTOCORE_UNUSED = {.reset = protocore_comp_reset,
                                             .set_s2c = protocore_comp_set_s2c,
                                             .on_newkeys = protocore_comp_on_newkeys,
                                             .on_auth_success = protocore_comp_on_auth_success,
                                             .s2c_active = protocore_comp_s2c_active,
                                             .s2c = protocore_comp_s2c,
                                             .set_c2s = protocore_comp_set_c2s,
                                             .c2s_active = protocore_comp_c2s_active,
                                             .c2s = protocore_comp_c2s};

PROTOCORE_END_DECLS

#endif // PROTOCORE_TRANSPORT_COMP_H
