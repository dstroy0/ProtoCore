// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_wire.h
 * @brief The DNS name on the wire (RFC 1035 sec 3.1, sec 4.1.4): labels in, dotted string out.
 *
 * RFC 1035 sec 3.1: a domain name is a sequence of labels, each one a length octet followed by that
 * many octets, ending in the null label of the root, which is a zero length octet. The high order
 * two bits of a length octet are 00 for a label. RFC 1035 sec 4.1.4 gives 11 to a pointer, a two
 * octet sequence whose remaining fourteen bits are the OFFSET of a prior occurrence of the same name
 * in the message, and reserves 10 and 01.
 *
 * Every DNS-shaped protocol carries that encoding, so it is written once here: the unicast server
 * answering a query and the multicast responder advertising a service read and write names through
 * this handle.
 *
 * Names compare case-insensitively (RFC 1035 sec 2.3.3), which is a property of the encoding rather
 * than of either caller, so the comparison sits here too.
 *
 * Pure: no state, no allocation, no I/O.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_WIRE_H
#define PROTOCORE_DNS_WIRE_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/** @brief Longest label a name may carry, the six bits a length octet leaves (RFC 1035 sec 2.3.4). */
#define PROTOCORE_DNS_LABEL_MAX 63u

/** @brief Pointers one name may follow before it is read as a loop (RFC 1035 sec 4.1.4). */
#define PROTOCORE_DNS_PTR_HOPS 8u

/**
 * @brief The message a name is read out of, and where its dotted text lands (RFC 1035 sec 4.1).
 */
typedef struct
{
    const uint8_t *pkt;   ///< the message the name sits in
    size_t len;           ///< octets in that message
    size_t off;           ///< where the name's first length octet sits
    char *out;            ///< where the dotted, NUL-terminated name is written
    size_t out_cap;       ///< octets that buffer holds
    proto_bool allow_ptr; ///< follow a pointer (RFC 1035 sec 4.1.4); false makes one a decode failure
} DnsWireMsgArgs;

/**
 * @brief The dotted name to write, and where its label sequence lands (RFC 1035 sec 3.1).
 */
typedef struct
{
    const char *dotted; ///< the name as text, labels separated by dots, trailing root dot optional
    uint8_t *out;       ///< where the labels and the root octet are written
    size_t out_cap;     ///< octets that buffer holds
} DnsWireTextArgs;

/** @brief The pair a case-insensitive compare judges (RFC 1035 sec 2.3.3). */
typedef struct
{
    const char *a; ///< the left dotted name
    const char *b; ///< the right dotted name
} DnsWireCmpArgs;

/**
 * @brief The DNS name codec: labels to a dotted string, a dotted string to labels, and the compare.
 *
 * A caller sets the members a call takes, invokes it through ::DnsWire, and reads the outcome off
 * the same handle.
 *
 * @var DnsWireNs::msg       the message a decode reads, and where its dotted text lands
 * @var DnsWireNs::text      the dotted name an encode writes, and where its labels land
 * @var DnsWireNs::cmp       the pair a compare judges
 * @var DnsWireNs::ok        a decode's or a compare's true/false outcome
 * @var DnsWireNs::next      offset just past the name as it sits at @c msg.off, 0 on a failed decode
 * @var DnsWireNs::n         octets an encode wrote, 0 when it wrote none
 * @var DnsWireNs::decode    read the name at @c msg.off into @c msg.out as a dotted string
 * @var DnsWireNs::encode    write @c text.dotted into @c text.out as labels plus the root octet
 * @var DnsWireNs::eq        compare two dotted names ignoring ASCII case (RFC 1035 sec 2.3.3)
 *
 * decode follows pointers only with @c msg.allow_ptr, at most ::PROTOCORE_DNS_PTR_HOPS of them, so a
 * message that points at itself terminates. The first name in a message has nothing earlier to point
 * at, which makes a pointer in a question malformed by construction: the unicast server clears the
 * flag, the responder reading answers sets it. @c next is where the name ends as it sits at
 * @c msg.off, so a name ending in a pointer reports two octets past the pointer rather than the end
 * of what it pointed at, and the caller keeps walking the record the name came from.
 *
 * decode reports false on a truncated name, a reserved label type, a label over
 * ::PROTOCORE_DNS_LABEL_MAX, a name that does not fit @c msg.out_cap, or more than
 * ::PROTOCORE_DNS_PTR_HOPS pointers.
 *
 * encode writes no pointer: an OFFSET is meaningful against one particular message, so composing one
 * belongs to whoever lays that message out. An empty name is the root octet alone. It reports 0 when
 * the name does not fit @c text.out_cap, carries an empty label, or carries one over
 * ::PROTOCORE_DNS_LABEL_MAX.
 *
 * No storage member: every octet a call touches belongs to the caller, so nothing survives a call.
 */
typedef struct
{
    DnsWireMsgArgs msg;   ///< what a decode reads (RFC 1035 sec 4.1.4)
    DnsWireTextArgs text; ///< what an encode writes (RFC 1035 sec 3.1)
    DnsWireCmpArgs cmp;   ///< what a compare judges (RFC 1035 sec 2.3.3)
    proto_bool ok;
    size_t next;
    size_t n;
} DnsWireVars;

/** @brief The operands and the outcome. */
extern DnsWireVars DnsWireV;

/** @brief The entries. */
typedef struct
{
    void (*const decode)(uint8_t *restrict work);
    void (*const encode)(uint8_t *restrict work);
    void (*const eq)(uint8_t *restrict work);
} DnsWireNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DnsWireV or a region of the borrow at a fixed offset.
void protocore_dns_wire_decode(uint8_t *restrict work);
void protocore_dns_wire_encode(uint8_t *restrict work);
void protocore_dns_wire_eq(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `DnsWire.decode(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DnsWireNs DnsWire __attribute__((unused)) = {
    .decode = protocore_dns_wire_decode,
    .encode = protocore_dns_wire_encode,
    .eq = protocore_dns_wire_eq,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_DNS_WIRE_H
