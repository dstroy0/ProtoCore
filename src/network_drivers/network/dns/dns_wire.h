// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_wire.h
 * @brief The DNS name on the wire (RFC 1035 sec 3.1, sec 4.1.4): labels in, dotted string out.
 *
 * A name is a run of length-prefixed labels ending in a zero byte, and a length byte with its top
 * two bits set is a pointer to a name earlier in the same message instead. Every DNS-shaped protocol
 * carries that encoding, so it is written once here: the unicast server answering a query and the
 * mDNS responder advertising a service read and write names through these.
 *
 * Names compare case-insensitively (sec 2.3.3), which is a property of the encoding rather than of
 * either caller, so the comparison sits here too.
 *
 * Pure: no state, no allocation, no I/O.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_WIRE_H
#define PROTOCORE_DNS_WIRE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/** @brief Longest label a name may carry (RFC 1035 sec 2.3.4). */
#define PC_DNS_LABEL_MAX 63u

/** @brief Pointer hops one name may take before it is read as a loop. */
#define PC_DNS_PTR_HOPS 8u

/**
 * @brief Decode the name at @p off in @p pkt into @p out as a dotted, NUL-terminated string.
 *
 * With @p allow_ptr, follows compression pointers, at most ::PC_DNS_PTR_HOPS of them, so a message
 * that points at itself terminates; without it a pointer is a decode failure. A question carries the
 * first name in its message and so has nothing earlier to point at, which makes a pointer there
 * malformed by construction: the unicast server refuses one, the mDNS responder reading answers
 * takes them.
 *
 * @p next receives the offset just past the name as it sits at @p off: where the name ends in a
 * pointer that is two bytes past the pointer, not the end of what it pointed at, which is what lets
 * a caller keep walking the record the name came from.
 *
 * @return false on a truncated name, an undefined label type, a label over ::PC_DNS_LABEL_MAX, a
 *         name that does not fit @p out_cap, or more than ::PC_DNS_PTR_HOPS hops.
 */
proto_bool pc_dns_name_decode(const uint8_t *pkt, size_t len, size_t off, char *out, size_t out_cap, size_t *next,
                              proto_bool allow_ptr);

/**
 * @brief Encode the dotted name @p dotted into @p out as length-prefixed labels plus the root zero.
 *
 * Writes no compression pointer: a pointer is only meaningful against one particular message, so
 * composing one belongs to whoever is laying that message out. An empty name is the root byte alone.
 *
 * @return bytes written, or 0 when the name does not fit @p cap, carries an empty label, or carries
 *         one over ::PC_DNS_LABEL_MAX.
 */
size_t pc_dns_name_encode(uint8_t *out, size_t cap, const char *dotted);

/** @brief Compare two dotted names ignoring ASCII case (RFC 1035 sec 2.3.3). */
proto_bool pc_dns_name_eq(const char *a, const char *b);

PROTO_END_DECLS

#endif // PROTOCORE_DNS_WIRE_H
