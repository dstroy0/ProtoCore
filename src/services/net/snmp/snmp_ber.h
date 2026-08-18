// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_ber.h
 * @brief The SNMP serialization: ASN.1 Basic Encoding Rules over a caller buffer.
 *
 * RFC 3417 sec 3.1: "Each instance of a message is serialized (i.e., encoded according to the
 * convention of [BER]) onto a single UDP over IPv4 datagram, using the algorithm specified in
 * Section 8." The encoding rules themselves are not an IETF RFC: they are ITU-T Recommendation
 * X.690 (ISO/IEC 8825-1), which RFC 3417 cites as [BER].
 *
 * RFC 3417 sec 8 states the two limits this codec is built to. Item (1): "When encoding the
 * length field, only the definite form is used; use of the indefinite form encoding is
 * prohibited. Note that when using the definite-long form, it is permissible to use more than the
 * minimum number of length octets necessary to encode the length field." A constructed type
 * therefore opens with a reserved 3-octet definite-long length that is back-patched at close, so
 * no value is buffered and no octet is shifted. Item (2): simple types are encoded primitive, and
 * the constructed form is used only for SEQUENCE.
 *
 * The tags cover exactly what SNMP puts on the wire: the ASN.1 simple types, the SMIv2
 * application-wide types (RFC 2578 sec 7.1.5 through 7.1.10), the variable-binding exception
 * markers and the context-specific PDU tags of the RFC 3416 sec 3 ASN.1.
 *
 * Encoder and decoder both run over caller-provided fixed buffers, so the module holds nothing
 * between calls and is unit-testable with no network stack under it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SNMP_BER_H
#define PROTOCORE_SNMP_BER_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SNMP

PROTOCORE_BEGIN_DECLS

/** @brief The identifier octets SNMP puts on the wire. */
typedef enum PROTO_ENUM_PACKED
{
    // ASN.1 simple types, encoded primitive (RFC 3417 sec 8 item 2).
    SNMP_TAG_BER_INTEGER = 0x02,
    SNMP_TAG_BER_OCTET_STRING = 0x04,
    SNMP_TAG_BER_NULL = 0x05,
    SNMP_TAG_BER_OID = 0x06,
    SNMP_TAG_BER_SEQUENCE = 0x30,
    // SMIv2 application-wide types (RFC 2578 sec 7.1.5 through 7.1.10).
    SNMP_TAG_SNMP_IPADDRESS = 0x40,
    SNMP_TAG_SNMP_COUNTER32 = 0x41,
    SNMP_TAG_SNMP_GAUGE32 = 0x42,
    SNMP_TAG_SNMP_TIMETICKS = 0x43,
    SNMP_TAG_SNMP_OPAQUE = 0x44,
    SNMP_TAG_SNMP_COUNTER64 = 0x46,
    // VarBind CHOICE exception markers (RFC 3416 sec 3; used per sec 4.2.1 and sec 4.2.2).
    SNMP_TAG_SNMP_NO_SUCH_OBJECT = 0x80,
    SNMP_TAG_SNMP_NO_SUCH_INSTANCE = 0x81,
    SNMP_TAG_SNMP_END_OF_MIB_VIEW = 0x82,
    // PDU tags, context-specific constructed (RFC 3416 sec 3).
    SNMP_TAG_SNMP_PDU_GET = 0xA0,      ///< GetRequest-PDU ::= [0] IMPLICIT PDU
    SNMP_TAG_SNMP_PDU_GETNEXT = 0xA1,  ///< GetNextRequest-PDU ::= [1] IMPLICIT PDU
    SNMP_TAG_SNMP_PDU_RESPONSE = 0xA2, ///< Response-PDU ::= [2] IMPLICIT PDU
    SNMP_TAG_SNMP_PDU_SET = 0xA3,      ///< SetRequest-PDU ::= [3] IMPLICIT PDU
    SNMP_TAG_SNMP_PDU_GETBULK = 0xA5,  ///< GetBulkRequest-PDU ::= [5] IMPLICIT BulkPDU
    SNMP_TAG_SNMP_PDU_INFORM = 0xA6,   ///< InformRequest-PDU ::= [6] IMPLICIT PDU
    SNMP_TAG_SNMP_PDU_TRAPV2 = 0xA7,   ///< SNMPv2-Trap-PDU ::= [7] IMPLICIT PDU
    SNMP_TAG_SNMP_PDU_REPORT = 0xA8,   ///< Report-PDU ::= [8] IMPLICIT PDU
} SnmpTag;

/**
 * @brief An encoder's cursor: the caller buffer, how far it is written, and whether it still fits.
 *
 * The cursor is the caller's, so several encodings can be open at once (a PDU into one buffer
 * while the message that will carry it is framed in another).
 */
typedef struct BerEnc
{
    uint8_t *buf;  ///< the buffer octets are written into
    size_t cap;    ///< how many it holds
    size_t len;    ///< how many are written
    proto_bool ok; ///< no write has run past cap
} BerEnc;

/** @brief A decoder's cursor: the octets being read, and how far the read has walked. */
typedef struct
{
    const uint8_t *buf; ///< the octets being read
    size_t len;         ///< how many
    size_t pos;         ///< the next octet a read takes
    proto_bool ok;      ///< no read has run past len
} BerDec;

/** @brief The caller buffer a codec runs over. */
typedef struct
{
    uint8_t *out;      ///< where an encoder writes its octets
    const uint8_t *in; ///< the octets a decoder reads
    size_t cap;        ///< octets available at @c out, or held by @c in
} SnmpBerBufArgs;

/** @brief The TLV a write carries: its identifier octet and the value under it. */
typedef struct
{
    uint8_t tag;          ///< the identifier octet the write emits
    long ival;            ///< the INTEGER value
    uint32_t uval;        ///< the non-negative application-type value (RFC 2578 sec 7.1.6 through 7.1.8)
    const uint8_t *bytes; ///< OCTET STRING octets, or pre-encoded octets a raw append copies
    size_t len;           ///< how many
    const uint32_t *arcs; ///< the OBJECT IDENTIFIER subidentifiers
    size_t arc_count;     ///< how many, at least 2
    size_t token;         ///< in: the constructed type a close back-patches; out: the one an open reserved
} SnmpBerTlvArgs;

/** @brief Where a read lands what it took. */
typedef struct
{
    uint32_t *arc_out; ///< where an OBJECT IDENTIFIER read lands its subidentifiers
    size_t arc_cap;    ///< how many that holds, at least 2
    size_t skip;       ///< value octets a skip steps over
} SnmpBerReadArgs;

/**
 * @brief The SNMP serialization (RFC 3417 sec 8, over ITU-T X.690).
 *
 * A caller binds a cursor with an init, sets the members a call takes, invokes it through
 * ::SnmpBer, and reads the outcome off the same handle. Nesting is explicit: an open reports the
 * token its close needs, so the caller holds it while the inner types are written.
 *
 * No storage member: both cursors are the caller's and every call reads or writes only through
 * them, so the module keeps nothing between calls.
 *
 * @var SnmpBerNs::enc       the encoder cursor a write acts on
 * @var SnmpBerNs::dec       the decoder cursor a read acts on
 * @var SnmpBerNs::buf       the caller buffer an init binds a cursor to
 * @var SnmpBerNs::tlv       the identifier octet and value a write carries
 * @var SnmpBerNs::read_args  where a read lands what it took
 * @var SnmpBerNs::ok        a call's true/false outcome: the cursor still fits its buffer
 * @var SnmpBerNs::tag       the identifier octet a header read took
 * @var SnmpBerNs::vlen      the value length that header states, in octets
 * @var SnmpBerNs::ival      the value an INTEGER read took
 * @var SnmpBerNs::n         subidentifiers an OBJECT IDENTIFIER read landed
 * @var SnmpBerNs::enc_init          bind an encoder cursor to @c buf.out for @c buf.cap octets
 * @var SnmpBerNs::put_integer       write an INTEGER, two's complement, minimal
 * @var SnmpBerNs::put_uint          write a non-negative value under @c tlv.tag, big-endian, minimal
 * @var SnmpBerNs::put_octet_string  write @c tlv.bytes under @c tlv.tag
 * @var SnmpBerNs::put_null          write NULL, the value a GetRequest-PDU binding carries
 * @var SnmpBerNs::put_oid           write an OBJECT IDENTIFIER from @c tlv.arcs
 * @var SnmpBerNs::put_tlv           write one primitive TLV verbatim
 * @var SnmpBerNs::put_raw           append already-encoded octets, no header of their own
 * @var SnmpBerNs::seq_begin         open a constructed type, reserving its definite-long length
 * @var SnmpBerNs::seq_end           close it, back-patching that length (RFC 3417 sec 8 item 1)
 * @var SnmpBerNs::dec_init          bind a decoder cursor to @c buf.in for @c buf.cap octets
 * @var SnmpBerNs::read_header       take a tag and length, leaving the cursor at the value
 * @var SnmpBerNs::read_integer      take an INTEGER, sign-extended from its first octet
 * @var SnmpBerNs::read_oid          take an OBJECT IDENTIFIER into @c read.arc_out
 * @var SnmpBerNs::skip              step the cursor past @c read.skip value octets
 */
typedef struct
{
    BerEnc *enc; ///< the encoder cursor every write names
    BerDec *dec; ///< the decoder cursor every read names

    SnmpBerBufArgs buf;        ///< the caller buffer an init binds
    SnmpBerTlvArgs tlv;        ///< what a write carries
    SnmpBerReadArgs read_args; ///< where a read lands

    proto_bool ok;
    uint8_t tag;
    size_t vlen;
    long ival;
    size_t n;

    void (*const enc_init)(uint8_t *restrict work);
    void (*const put_integer)(uint8_t *restrict work);
    void (*const put_uint)(uint8_t *restrict work);
    void (*const put_octet_string)(uint8_t *restrict work);
    void (*const put_null)(uint8_t *restrict work);
    void (*const put_oid)(uint8_t *restrict work);
    void (*const put_tlv)(uint8_t *restrict work);
    void (*const put_raw)(uint8_t *restrict work);
    void (*const seq_begin)(uint8_t *restrict work);
    void (*const seq_end)(uint8_t *restrict work);
    void (*const dec_init)(uint8_t *restrict work);
    void (*const read_header)(uint8_t *restrict work);
    void (*const read_integer)(uint8_t *restrict work);
    void (*const read_oid)(uint8_t *restrict work);
    void (*const skip)(uint8_t *restrict work);
} SnmpBerNs;

/** @brief The one symbol this module exports. */
extern SnmpBerNs SnmpBer;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNMP

#endif // PROTOCORE_SNMP_BER_H
