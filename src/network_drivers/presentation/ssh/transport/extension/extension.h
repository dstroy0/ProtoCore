// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file extension.h
 * @brief RFC 8308 extension negotiation.
 */

#ifndef PROTOCORE_TRANSPORT_EXTENSION_H
#define PROTOCORE_TRANSPORT_EXTENSION_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/**
 * @brief Bytes an indicator name takes in a kex_algorithms list, with its separating comma.
 *
 * Both indicator names are the same length, so one bound covers either role.
 */
#define SSH_EXT_INFO_INDICATOR_MAX sizeof(",ext-info-s")

/** @brief The True for the client's indicator, false for the server's. */
typedef struct
{
    proto_bool client_role; ///< True for the client's indicator, false for the server's
} ExtensionInfoIndicatorArgs;
/** @brief What build takes: out, len, cap. */
typedef struct
{
    uint8_t *out;
    size_t *len;
    size_t cap;
} ExtensionBuildArgs;
/**
 * @brief RFC 8308 extension negotiation.
 *
 * A caller sets the members a call takes, invokes it through ::Extension with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Extension.info_indicator_args.client_role = ...;
 *   Extension.info_indicator(work);
 *   // Extension.text is what the call reports
 *
 * @var ExtensionNs::info_indicator_args  the True for the client's indicator, false for the server's
 * @var ExtensionNs::build_args  what build takes: out, len, cap
 * @var ExtensionNs::ok  a call's true/false outcome
 * @var ExtensionNs::text  the bare name, with no leading comma
 * @var ExtensionNs::n  0 on success, -1 on buffer overflow
 * @var ExtensionNs::info_indicator  the RFC 8308 sec 2.2 indicator name belonging to a role. ...
 * @var ExtensionNs::build  build SSH_MSG_EXT_INFO advertising server-sig-algs (RFC 8308). ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ExtensionInfoIndicatorArgs info_indicator_args;
    ExtensionBuildArgs build_args;
    proto_bool ok;
    const char *text;
    int n;
} ExtensionVars;

/** @brief The operands and the outcome. */
extern ExtensionVars ExtensionV;

/** @brief The entries. */
typedef struct
{
    void (*const info_indicator)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
} ExtensionNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ExtensionV or a region of the borrow at a fixed offset.
void protocore_extension_info_indicator(uint8_t *restrict work);
void protocore_extension_build(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Extension.info_indicator(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ExtensionNs Extension __attribute__((unused)) = {
    .info_indicator = protocore_extension_info_indicator,
    .build = protocore_extension_build,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH

#endif // PROTOCORE_TRANSPORT_EXTENSION_H
