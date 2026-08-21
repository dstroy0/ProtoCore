// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_TRANSPORT_EXTENSION_H
#define PROTOCORE_TRANSPORT_EXTENSION_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file extension.h
 * @brief RFC 8308 extension negotiation.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_EXTENSION_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/**
 * @brief Bytes an indicator name takes in a kex_algorithms list, with its separating comma.
 *
 * Both indicator names are the same length, so one bound covers either role.
 */
#define SSH_EXT_INFO_INDICATOR_MAX sizeof(",ext-info-s")

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    const char *(*info_indicator)(uint8_t *, proto_bool);
    int (*build)(uint8_t *, uint8_t *, size_t *, size_t);
} ExtensionNs;
PROTOCORE_NS_LAYOUT(ExtensionNs, info_indicator, build);

/**
 * @brief The RFC 8308 sec 2.2 indicator name belonging to a role. .
 * @param work PROTOCORE_EXTENSION_BORROW bytes the caller took. Not held past the call.
 * @param client_role True for the client's indicator, false for the server's
 * @return The const char *.
 */
const char *protocore_extension_info_indicator(uint8_t *work, proto_bool client_role);
/**
 * @brief Build SSH_MSG_EXT_INFO advertising server-sig-algs (RFC 8308). .
 * @param work PROTOCORE_EXTENSION_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param len Len
 * @param cap Cap
 * @return The int.
 */
int protocore_extension_build(uint8_t *work, uint8_t *out, size_t *len, size_t cap);

/** @brief Module namespace. */
PROTOCORE_NS ExtensionNs Extension PROTOCORE_UNUSED = {.info_indicator = protocore_extension_info_indicator,
                                                       .build = protocore_extension_build};

PROTOCORE_END_DECLS

#endif // PROTOCORE_TRANSPORT_EXTENSION_H
