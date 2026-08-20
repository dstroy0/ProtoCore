// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file der.h
 * @brief ASN.1 Distinguished Encoding Rules: one TLV at a time, over the caller's bytes.
 *
 * X.690 sec 10 makes DER a canonical subset of BER: every length is definite, and the shortest form
 * that fits. That is the whole of what this reads - an indefinite length, a redundant long form, or
 * a length that runs past the caller's buffer is refused rather than guessed at, because a parser
 * that accepts more than the encoding allows accepts two encodings of the same certificate and
 * signature verification covers only one of them.
 *
 * Nothing is copied and nothing is allocated. A read reports a tag, a pointer into the caller's own
 * bytes, a length, and where the next value begins, so walking a structure is a sequence of reads
 * against one buffer that outlives them all.
 *
 * The caller holds the position. A chain walk has two certificates open at once, so the cursor is a
 * value on the call rather than state in this module.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DER_H
#define PROTOCORE_DER_H

#include "protocore_config.h" // the entry point: the widths

PROTOCORE_BEGIN_DECLS

/** @name X.690 sec 8.1.2 identifier octets: the universal tags this profile reads.
 *  @{ */
#define PROTOCORE_DER_BOOLEAN 0x01u
#define PROTOCORE_DER_INTEGER 0x02u
#define PROTOCORE_DER_BIT_STRING 0x03u
#define PROTOCORE_DER_OCTET_STRING 0x04u
#define PROTOCORE_DER_NULL 0x05u
#define PROTOCORE_DER_OID 0x06u
#define PROTOCORE_DER_UTF8_STRING 0x0Cu
#define PROTOCORE_DER_PRINTABLE_STRING 0x13u
#define PROTOCORE_DER_IA5_STRING 0x16u
#define PROTOCORE_DER_UTC_TIME 0x17u
#define PROTOCORE_DER_GENERALIZED_TIME 0x18u
#define PROTOCORE_DER_SEQUENCE 0x30u
#define PROTOCORE_DER_SET 0x31u
/** @} */

/** @brief X.690 sec 8.1.2.5: the constructed bit, set on a value that holds other values. */
#define PROTOCORE_DER_CONSTRUCTED 0x20u

/** @brief X.690 sec 8.1.2.2: a context-specific tag, as an X.509 field's [n]. */
#define PROTOCORE_DER_CONTEXT(n) (0x80u | (uint8_t)(n))
#define PROTOCORE_DER_CONTEXT_CONSTRUCTED(n) (0xA0u | (uint8_t)(n))

/** @brief One value in the caller's bytes: what it is, where its content is, and what follows. */
typedef struct
{
    uint8_t tag;            ///< the identifier octet (X.690 sec 8.1.2)
    const uint8_t *content; ///< the content octets, in the caller's buffer
    size_t len;             ///< how many
    size_t next;            ///< the offset the following value begins at
} DerTlv;

/** @brief The bytes a read walks, and where in them it starts. */
typedef struct
{
    const uint8_t *buf; ///< the encoding, the caller's
    size_t len;         ///< how much of it there is
    size_t pos;         ///< where this read begins
} DerReadArgs;

/** @brief An object identifier a comparison names, in its encoded form (X.690 sec 8.19). */
typedef struct
{
    const uint8_t *oid; ///< the OID's content octets, without the tag and length
    size_t oid_len;     ///< how many
} DerOidArgs;

/**
 * @brief The one reader.
 *
 * A caller sets the members a call takes, invokes it through ::Der, and reads the outcome off the
 * same handle.
 *
 * @var DerNs::read_args  the bytes a read walks, and where it starts
 * @var DerNs::oid_args   the OID a comparison names
 * @var DerNs::tlv        what a read found
 * @var DerNs::ok         a call's true/false outcome
 * @var DerNs::u64        an INTEGER's value, or a time as seconds since the POSIX epoch
 * @var DerNs::read       one value at @c read_args.pos: its tag, content and successor
 * @var DerNs::enter      the same, and then the first value INSIDE it; refuses a primitive
 * @var DerNs::uint       the INTEGER at @c read_args.pos as an unsigned value; refuses a negative
 *                        one, and one wider than 64 bits
 * @var DerNs::bitstring  the BIT STRING at @c read_args.pos, past its unused-bits octet; refuses
 *                        any count of unused bits but zero, which is what a key or a signature has
 * @var DerNs::oid_eq     whether the OID at @c read_args.pos is the one @c oid_args names
 * @var DerNs::time       the UTCTime or GeneralizedTime at @c read_args.pos, as seconds since the
 *                        POSIX epoch (RFC 5280 sec 4.1.2.5)
 *
 * No storage member: every call works in the caller's buffer and reports on this handle.
 */
typedef struct
{
    DerReadArgs read_args;
    DerOidArgs oid_args;
    DerTlv tlv;
    proto_bool ok;
    uint64_t u64;
} DerVars;

/** @brief The operands and the outcome. */
extern DerVars DerV;

/** @brief The entries. */
typedef struct
{
    void (*const read)(uint8_t *restrict work);
    void (*const enter)(uint8_t *restrict work);
    void (*const uint)(uint8_t *restrict work);
    void (*const bitstring)(uint8_t *restrict work);
    void (*const oid_eq)(uint8_t *restrict work);
    void (*const time)(uint8_t *restrict work);
} DerNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DerV or a region of the borrow at a fixed offset.
void protocore_der_read(uint8_t *restrict work);
void protocore_der_enter(uint8_t *restrict work);
void protocore_der_uint(uint8_t *restrict work);
void protocore_der_bitstring(uint8_t *restrict work);
void protocore_der_oid_eq(uint8_t *restrict work);
void protocore_der_time(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Der.read(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DerNs Der __attribute__((unused)) = {
    .read = protocore_der_read,
    .enter = protocore_der_enter,
    .uint = protocore_der_uint,
    .bitstring = protocore_der_bitstring,
    .oid_eq = protocore_der_oid_eq,
    .time = protocore_der_time,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_DER_H
