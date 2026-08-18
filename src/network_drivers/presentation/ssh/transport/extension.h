// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file extension.h
 * @brief RFC 8308 extension negotiation.
 */

#ifndef PROTOCORE_TRANSPORT_EXTENSION_H
#define PROTOCORE_TRANSPORT_EXTENSION_H

#include "network_drivers/presentation/ssh/common.h"


PROTOCORE_BEGIN_DECLS

/**
 * @brief Bytes an indicator name takes in a kex_algorithms list, with its separating comma.
 *
 * Both indicator names are the same length, so one bound covers either role.
 */
#define SSH_EXT_INFO_INDICATOR_MAX sizeof(",ext-info-s")

/**
 * @brief The RFC 8308 sec 2.2 indicator name belonging to a role.
 *
 * "Implementations MUST NOT send an incorrect indicator name for their role." A client's is
 * "ext-info-c" and a server's is "ext-info-s", so an end sends its own and looks for its peer's.
 *
 * @param client_role  True for the client's indicator, false for the server's.
 * @return The bare name, with no leading comma.
 */
const char *ssh_ext_info_indicator(proto_bool client_role);

/**
 * @brief Build SSH_MSG_EXT_INFO advertising server-sig-algs (RFC 8308).
 *
 * Names the public-key signature algorithms this end accepts for userauth, in preference order.
 * Sent once, right after NEWKEYS, when the peer advertised ext-info-c.
 *
 * @return 0 on success, -1 on buffer overflow.
 */
int ssh_extinfo_build(uint8_t *out, size_t *len, size_t cap);

PROTOCORE_END_DECLS

#endif // PROTOCORE_TRANSPORT_EXTENSION_H
