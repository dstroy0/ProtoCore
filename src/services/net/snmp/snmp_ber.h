// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_ber.h
 * @brief Zero-heap ASN.1 BER encoder/decoder for the SNMP agent (PC_ENABLE_SNMP).
 *
 * A minimal, bounded TLV codec covering exactly the types SNMP uses: INTEGER,
 * OCTET STRING, NULL, OBJECT IDENTIFIER, SEQUENCE, and the SNMP application
 * types (Counter32/Gauge32/TimeTicks/IpAddress/Counter64) and PDU context tags.
 * Encoder and decoder both operate over caller-provided fixed buffers - no heap.
 * This is the shared base for SNMP v1/v2c and (later) v3; it is unit-tested on
 * its own (env:native_snmp) since it needs no lwIP.
 */

#ifndef PROTOCORE_SNMP_BER_H
#define PROTOCORE_SNMP_BER_H

#include "protocore_config.h"

#if PC_ENABLE_SNMP

// ASN.1 / SNMP tags
typedef enum PROTO_ENUM_PACKED
{
    SNMP_TAG_BER_INTEGER = 0x02,
    SNMP_TAG_BER_OCTET_STRING = 0x04,
    SNMP_TAG_BER_NULL = 0x05,
    SNMP_TAG_BER_OID = 0x06,
    SNMP_TAG_BER_SEQUENCE = 0x30,
    // SNMP application types (RFC 2578)
    SNMP_TAG_SNMP_IPADDRESS = 0x40,
    SNMP_TAG_SNMP_COUNTER32 = 0x41,
    SNMP_TAG_SNMP_GAUGE32 = 0x42,
    SNMP_TAG_SNMP_TIMETICKS = 0x43,
    SNMP_TAG_SNMP_OPAQUE = 0x44,
    SNMP_TAG_SNMP_COUNTER64 = 0x46,
    // VarBind exception markers (RFC 3416)
    SNMP_TAG_SNMP_NO_SUCH_OBJECT = 0x80,
    SNMP_TAG_SNMP_NO_SUCH_INSTANCE = 0x81,
    SNMP_TAG_SNMP_END_OF_MIB_VIEW = 0x82,
    // PDU tags (context-specific, constructed)
    SNMP_TAG_SNMP_PDU_GET = 0xA0,
    SNMP_TAG_SNMP_PDU_GETNEXT = 0xA1,
    SNMP_TAG_SNMP_PDU_RESPONSE = 0xA2,
    SNMP_TAG_SNMP_PDU_SET = 0xA3,
    SNMP_TAG_SNMP_PDU_GETBULK = 0xA5,
    SNMP_TAG_SNMP_PDU_TRAPV2 = 0xA7,
    SNMP_TAG_SNMP_PDU_REPORT = 0xA8,
} SnmpTag;

// ---------------------------------------------------------------------------
// Encoder - forward writer over a caller buffer. Constructed types reserve a
// 3-byte long-form length that is back-patched at close (valid BER; accepted by
// net-snmp etc.), so no buffering or shifting is needed.
// ---------------------------------------------------------------------------
typedef struct BerEnc
{
    uint8_t *buf;
    size_t cap;
    size_t len;
    proto_bool ok;
} BerEnc;

void pc_ber_enc_init(BerEnc *e, uint8_t *buf, size_t cap);

proto_bool pc_ber_put_integer(BerEnc *e, long v);               ///< INTEGER (signed, minimal)
proto_bool pc_ber_put_uint(BerEnc *e, uint8_t tag, uint32_t v); ///< non-negative int with @p tag
proto_bool pc_ber_put_octet_string(BerEnc *e, uint8_t tag, const uint8_t *d,
                                   size_t n);                                    ///< OCTET STRING / IpAddress / Opaque
proto_bool pc_ber_put_null(BerEnc *e);                                           ///< NULL
proto_bool pc_ber_put_oid(BerEnc *e, const uint32_t *arcs, size_t n);            ///< OBJECT IDENTIFIER (n >= 2)
proto_bool pc_ber_put_tlv(BerEnc *e, uint8_t tag, const uint8_t *val, size_t n); ///< raw primitive TLV
proto_bool pc_ber_put_raw(BerEnc *e, const uint8_t *bytes, size_t n);            ///< append pre-encoded bytes verbatim

size_t pc_ber_seq_begin(BerEnc *e, uint8_t tag); ///< open a constructed type; returns a token
void pc_ber_seq_end(BerEnc *e, size_t token);    ///< close it (back-patch the length)

// ---------------------------------------------------------------------------
// Decoder - forward reader over a buffer.
// ---------------------------------------------------------------------------
typedef struct
{
    const uint8_t *buf;
    size_t len;
    size_t pos;
    proto_bool ok;
} BerDec;

void pc_ber_dec_init(BerDec *d, const uint8_t *buf, size_t len);

/** @brief Read a tag + length; on success @p d->pos is left at the value. */
proto_bool pc_ber_read_header(BerDec *d, uint8_t *tag, size_t *length);
/** @brief Read an INTEGER into @p out. */
proto_bool pc_ber_read_integer(BerDec *d, long *out);
/** @brief Read an OBJECT IDENTIFIER into @p arcs (capacity @p max); count in @p n. */
proto_bool pc_ber_read_oid(BerDec *d, uint32_t *arcs, size_t max, size_t *n);
/** @brief Advance the cursor past @p length value bytes. */
proto_bool pc_ber_skip(BerDec *d, size_t length);

#endif // PC_ENABLE_SNMP

#endif // PROTOCORE_SNMP_BER_H
