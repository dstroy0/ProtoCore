// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file s7comm.h
 * @brief Siemens S7comm PDU codec (PC_ENABLE_S7COMM) - zero-heap builder + parser for the
 *        S7-300/400 communication PDUs, carried inside a COTP Data TPDU (services/fieldbus/cotp) over
 *        ISO-on-TCP (port 102).
 *
 * An S7comm PDU starts with a header then a parameter section then an optional data section:
 * @code
 *   0x32 ROSCTR  redundancy(2)  pdu-ref(2)  param-len(2)  data-len(2)  [err-class err-code]
 *   <parameter ...>  <data ...>
 * @endcode
 * The header is 10 octets, or 12 for an Ack_Data response (which adds a 2-octet error code).
 * A Read Var job (function 0x04) carries one or more S7-ANY request items (area / DB / byte
 * address / element count); the Ack_Data response carries, per item, a return code + a data
 * transport size + a length + the value bytes. Per the protocol, the response length is in
 * BITS for the bit/byte/int transport sizes (3/4/5) and in BYTES otherwise, and each item
 * is padded to an even length except the last.
 *
 * Constants and the length rule are verified against the Wireshark S7comm dissector. This
 * codec produces / consumes the S7 PDU; wrap it with `pc_cotp_build_dt` + `pc_tpkt_build`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_S7COMM_H
#define PROTOCORE_S7COMM_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_S7COMM

#define S7_PROTOCOL_ID 0x32 ///< constant first octet of every S7comm PDU

// ROSCTR (message type).
#define S7_ROSCTR_JOB 0x01
#define S7_ROSCTR_ACK 0x02
#define S7_ROSCTR_ACK_DATA 0x03
#define S7_ROSCTR_USERDATA 0x07

// Parameter function codes.
#define S7_FUNC_SETUP_COMM 0xF0
#define S7_FUNC_READ_VAR 0x04
#define S7_FUNC_WRITE_VAR 0x05

// Memory area codes (in an S7-ANY item).
#define S7_AREA_INPUTS 0x81  ///< process inputs (I/E)
#define S7_AREA_OUTPUTS 0x82 ///< process outputs (Q/A)
#define S7_AREA_FLAGS 0x83   ///< flags / merker (M)
#define S7_AREA_DB 0x84      ///< data blocks (DB)
#define S7_AREA_COUNTER 0x1C
#define S7_AREA_TIMER 0x1D

// Request-item transport sizes (the element type).
#define S7_TS_BIT 1
#define S7_TS_BYTE 2
#define S7_TS_CHAR 3
#define S7_TS_WORD 4
#define S7_TS_INT 5
#define S7_TS_DWORD 6
#define S7_TS_DINT 7
#define S7_TS_REAL 8

// Response data transport sizes (length-in-bits for BIT/BYTE/INT = 3/4/5).
#define S7_DTS_NULL 0
#define S7_DTS_BIT 3
#define S7_DTS_BYTE 4
#define S7_DTS_INT 5
#define S7_DTS_DINT 6
#define S7_DTS_REAL 7
#define S7_DTS_OCTET 9

#define S7_SYNTAX_S7ANY 0x10 ///< S7-ANY address syntax id
#define S7_RET_OK 0xFF       ///< data item return code: success

/** @brief Build a Setup Communication job. Returns the PDU length, or 0 on overflow. */
size_t pc_s7_build_setup(uint8_t *buf, size_t cap, uint16_t pdu_ref, uint16_t max_amq_calling, uint16_t max_amq_called,
                         uint16_t pdu_size);

/** @brief One Read Var item (an S7-ANY pointer). */
typedef struct
{
    uint8_t area;           ///< S7_AREA_*
    uint16_t db_number;     ///< DB number (0 for non-DB areas)
    uint32_t byte_address;  ///< starting byte address
    uint8_t transport_size; ///< S7_TS_* (element type)
    uint16_t count;         ///< number of elements
} S7ReadItem;

/** @brief Build a Read Var job for @p n items. Returns the PDU length, or 0 on overflow. */
size_t pc_s7_build_read_request(uint8_t *buf, size_t cap, uint16_t pdu_ref, const S7ReadItem *items, size_t n);

/** @brief One Write Var item: an S7-ANY pointer (as for a read) plus the value bytes to write. */
typedef struct
{
    uint8_t area;                ///< S7_AREA_*
    uint16_t db_number;          ///< DB number (0 for non-DB areas)
    uint32_t byte_address;       ///< starting byte address
    uint8_t transport_size;      ///< S7_TS_* (parameter-spec element type)
    uint16_t count;              ///< number of elements (parameter spec)
    uint8_t data_transport_size; ///< S7_DTS_* (data item; sets the bit/byte length rule)
    const uint8_t *data;         ///< value bytes to write
    uint16_t data_len;           ///< value length in BYTES
} S7WriteItem;

/**
 * @brief Build a Write Var job (function 0x05) for @p n items. Mirrors the read request's parameter (the same
 *        12-octet S7-ANY item specs) and appends a data section: per item a return code (0x00) + data
 *        transport size + a 2-octet length + the value bytes, each item padded to an even length except the
 *        last. The length field is in BITS for the bit/byte/int data transport sizes (3/4/5) and in BYTES
 *        otherwise, matching pc_s7_read_next_item. @return the PDU length, or 0 on overflow / bad input.
 */
size_t pc_s7_build_write_request(uint8_t *buf, size_t cap, uint16_t pdu_ref, const S7WriteItem *items, size_t n);

/** @brief A parsed S7comm header. @ref param / @ref data point INTO the source buffer. */
typedef struct
{
    uint8_t rosctr;
    uint16_t pdu_ref;
    uint16_t param_len;
    uint16_t data_len;
    uint8_t error_class; ///< Ack_Data only
    uint8_t error_code;  ///< Ack_Data only
    size_t header_len;   ///< 10 or 12
    const uint8_t *param;
    const uint8_t *data;
} S7Header;

/** @brief Parse + validate an S7comm header (protocol id, lengths). */
proto_bool pc_s7_parse_header(const uint8_t *buf, size_t len, S7Header *out);

/** @brief One Read Var response data item. @ref data points INTO the source buffer. */
typedef struct
{
    uint8_t return_code;    ///< S7_RET_OK on success
    uint8_t transport_size; ///< S7_DTS_*
    const uint8_t *data;    ///< value bytes
    size_t data_len;        ///< value length in BYTES (the bit length is converted)
} S7DataItem;

/**
 * @brief Read the next Read Var response data item from the data section.
 * @param data     the @ref S7Header data pointer; @param data_len its data_len.
 * @param offset   in/out cursor, start at 0; advanced past the item (and its even-pad).
 * @return true on a complete item; false at end-of-section or on truncation.
 */
proto_bool pc_s7_read_next_item(const uint8_t *data, size_t data_len, size_t *offset, S7DataItem *out);

#endif // PC_ENABLE_S7COMM

PROTO_END_DECLS

#endif // PROTOCORE_S7COMM_H
