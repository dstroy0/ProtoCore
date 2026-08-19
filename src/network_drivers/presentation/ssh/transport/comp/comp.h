// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file comp.h
 * @brief RFC 4253 sec 6.2 compression: the negotiated stream, both directions.
 */

#ifndef PROTOCORE_TRANSPORT_COMP_H
#define PROTOCORE_TRANSPORT_COMP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_ZLIB

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Negotiated compression algorithm, held per direction. */
typedef enum PROTO_ENUM_PACKED
{
    SSH_COMP_NONE = 0,        ///< no compression
    SSH_COMP_ZLIB = 1,        ///< "zlib" (RFC 4253) - starts right after NEWKEYS
    SSH_COMP_ZLIB_DELAYED = 2 ///< "zlib@openssh.com" - starts after SSH_MSG_USERAUTH_SUCCESS
} SshCompAlg;

/** @brief What reset takes: i. */
typedef struct
{
    uint8_t i;
} CompResetArgs;

/** @brief What set_s2c takes: i, alg. */
typedef struct
{
    uint8_t i;
    SshCompAlg alg;
} CompSetS2cArgs;

/** @brief What on_newkeys takes: i. */
typedef struct
{
    uint8_t i;
} CompOnNewkeysArgs;

/** @brief What on_auth_success takes: i. */
typedef struct
{
    uint8_t i;
} CompOnAuthSuccessArgs;

/** @brief What s2c_active takes: i. */
typedef struct
{
    uint8_t i;
} CompS2cActiveArgs;

/** @brief What s2c takes: i, src, src_len, dst, dst_cap, out_len. */
typedef struct
{
    uint8_t i;
    const uint8_t *src;
    size_t src_len;
    uint8_t *dst;
    size_t dst_cap;
    size_t *out_len;
} CompS2cArgs;

/** @brief What set_c2s takes: i, alg. */
typedef struct
{
    uint8_t i;
    SshCompAlg alg;
} CompSetC2sArgs;

/** @brief What c2s_active takes: i. */
typedef struct
{
    uint8_t i;
} CompC2sActiveArgs;

/** @brief What c2s takes: i, src, src_len, dst, dst_cap, out_len. */
typedef struct
{
    uint8_t i;
    const uint8_t *src;
    size_t src_len;
    uint8_t *dst;
    size_t dst_cap;
    size_t *out_len;
} CompC2sArgs;

/**
 * @brief RFC 4253 sec 6.2 compression: the negotiated stream, both directions.
 *
 * A caller sets the members a call takes, invokes it through ::Comp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Comp.reset_args.i = ...;
 *   Comp.reset(work);
 *
 * @var CompNs::reset_args  what reset takes: i
 * @var CompNs::set_s2c_args  what set_s2c takes: i, alg
 * @var CompNs::on_newkeys_args  what on_newkeys takes: i
 * @var CompNs::on_auth_success_args  what on_auth_success takes: i
 * @var CompNs::s2c_active_args  what s2c_active takes: i
 * @var CompNs::s2c_args  what s2c takes: i, src, src_len, dst, dst_cap, out_len
 * @var CompNs::set_c2s_args  what set_c2s takes: i, alg
 * @var CompNs::c2s_active_args  what c2s_active takes: i
 * @var CompNs::c2s_args  what c2s takes: i, src, src_len, dst, dst_cap, out_len
 * @var CompNs::ok  a call's true/false outcome
 * @var CompNs::n  0 on success (*out_len set), -1 on overflow / oversized input / ...
 * @var CompNs::reset  reset compression state for slot i (fresh connection). Does NOT run ...
 * @var CompNs::set_s2c  record the s2c algorithm negotiated in KEXINIT (::SshCompAlg)
 * @var CompNs::on_newkeys  NEWKEYS completed: start the stream now if `zlib` was negotiated ...
 * @var CompNs::on_auth_success  SSH_MSG_USERAUTH_SUCCESS sent: start the stream if ...
 * @var CompNs::s2c_active  true once the s2c stream is active and outbound payloads must be ...
 * @var CompNs::s2c  compress one outbound payload, continuing the session's zlib stream
 * @var CompNs::set_c2s  record the client-to-server algorithm negotiated in KEXINIT ...
 * @var CompNs::c2s_active  true once the c2s stream is active and inbound payloads must be ...
 * @var CompNs::c2s  decompress one inbound payload, continuing the session's ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    CompResetArgs reset_args;
    CompSetS2cArgs set_s2c_args;
    CompOnNewkeysArgs on_newkeys_args;
    CompOnAuthSuccessArgs on_auth_success_args;
    CompS2cActiveArgs s2c_active_args;
    CompS2cArgs s2c_args;
    CompSetC2sArgs set_c2s_args;
    CompC2sActiveArgs c2s_active_args;
    CompC2sArgs c2s_args;

    proto_bool ok;
    int n;

    void (*const reset)(uint8_t *restrict work);
    void (*const set_s2c)(uint8_t *restrict work);
    void (*const on_newkeys)(uint8_t *restrict work);
    void (*const on_auth_success)(uint8_t *restrict work);
    void (*const s2c_active)(uint8_t *restrict work);
    void (*const s2c)(uint8_t *restrict work);
    void (*const set_c2s)(uint8_t *restrict work);
    void (*const c2s_active)(uint8_t *restrict work);
    void (*const c2s)(uint8_t *restrict work);
} CompNs;

/** @brief The one symbol this module exports. */
extern CompNs Comp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_ZLIB

#endif // PROTOCORE_TRANSPORT_COMP_H
