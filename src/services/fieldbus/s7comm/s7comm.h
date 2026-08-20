// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file s7comm.h
 * @brief Siemens S7comm PDU codec (PROTOCORE_ENABLE_S7COMM) - zero-heap builder + parser for the
 *        S7-300/400 communication PDUs, carried inside a COTP Data TPDU (services/fieldbus/cotp) over
 *        ISO-on-TCP (port 102).
 *
 * An S7comm PDU starts with a header then a parameter section then an optional data section:
 * @code
 *   0x32 ROSCTR  redundancy(2)  pdu-ref(2)  param-len(2)  data-len(2)  [err-class err-code]
 *   <parameter ...>  <data ...>
 * @endcode
 * The header is 10 octets, or 12 for a response ROSCTR (Ack or Ack_Data), which adds a 2-octet error code.
 * A Read Var job (function 0x04) carries one or more S7-ANY request items (area / DB / byte
 * address / element count); the Ack_Data response carries, per item, a return code + a data
 * transport size + a length + the value bytes. Per the protocol, the response length is in
 * BITS for the bit/byte/int transport sizes (3/4/5) and in BYTES otherwise, and each item
 * is padded to an even length except the last.
 *
 * Constants and the length rule are verified against the Wireshark S7comm dissector. This
 * codec produces / consumes the S7 PDU; wrap it with `Cotp.build_dt` + `Cotp.tpkt_build`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_S7COMM_H
#define PROTOCORE_S7COMM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_S7COMM

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief One Read Var item (an S7-ANY pointer). */
typedef struct
{
    uint8_t area;           ///< S7_AREA_*
    uint16_t db_number;     ///< DB number (0 for non-DB areas)
    uint32_t byte_address;  ///< starting byte address
    uint8_t transport_size; ///< S7_TS_* (element type)
    uint16_t count;         ///< number of elements
} S7ReadItem;

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

/** @brief A parsed S7comm header. @ref param / @ref data point INTO the source buffer. */
typedef struct
{
    uint8_t rosctr;
    uint16_t pdu_ref;
    uint16_t param_len;
    uint16_t data_len;
    uint8_t error_class; ///< Ack / Ack_Data only
    uint8_t error_code;  ///< Ack / Ack_Data only
    size_t header_len;   ///< 10 or 12
    const uint8_t *param;
    const uint8_t *data;
} S7Header;

/** @brief One Read Var response data item. @ref data points INTO the source buffer. */
typedef struct
{
    uint8_t return_code;    ///< S7_RET_OK on success
    uint8_t transport_size; ///< S7_DTS_*
    const uint8_t *data;    ///< value bytes
    size_t data_len;        ///< value length in BYTES (the bit length is converted)
} S7DataItem;

/** @brief What build_setup takes: buf, cap, pdu_ref, max_amq_calling, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t pdu_ref;
    uint16_t max_amq_calling;
    uint16_t max_amq_called;
    uint16_t pdu_size;
} S7commBuildSetupArgs;

/** @brief What build_read_request takes: buf, cap, pdu_ref, items, n. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t pdu_ref;
    const S7ReadItem *items;
    size_t n;
} S7commBuildReadRequestArgs;

/** @brief What build_write_request takes: buf, cap, pdu_ref, items, n. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t pdu_ref;
    const S7WriteItem *items;
    size_t n;
} S7commBuildWriteRequestArgs;

/** @brief What parse_header takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    S7Header *out;
} S7commParseHeaderArgs;

/** @brief What read_next_item takes: data, data_len, offset, out. */
typedef struct
{
    const uint8_t *data; ///< the S7Header data pointer; data_len its data_len
    size_t data_len;
    size_t *offset; ///< in/out cursor, start at 0; advanced past the item (and its even-pad)
    S7DataItem *out;
} S7commReadNextItemArgs;

/**
 * @brief Siemens S7comm PDU codec (PROTOCORE_ENABLE_S7COMM) - zero-heap builder + parser for the S7-300/400
 * communication PDUs, carried inside a COTP Data TPDU (services/fieldbus/cotp) over ISO-on-TCP (port 102).
 *
 * A caller sets the members a call takes, invokes it through ::S7comm with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   S7comm.build_setup_args.buf = ...;
 *   S7comm.build_setup_args.cap = ...;
 *   S7comm.build_setup_args.pdu_ref = ...;
 *   S7comm.build_setup_args.max_amq_calling = ...;
 *   S7comm.build_setup_args.max_amq_called = ...;
 *   S7comm.build_setup_args.pdu_size = ...;
 *   S7comm.build_setup(work);
 *   // S7comm.n is what the call reports
 *
 * @var S7commNs::build_setup_args  what build_setup takes: buf, cap, pdu_ref, max_amq_calling,
 * @var S7commNs::build_read_request_args  what build_read_request takes: buf, cap, pdu_ref, items, n
 * @var S7commNs::build_write_request_args  what build_write_request takes: buf, cap, pdu_ref, items, n
 * @var S7commNs::parse_header_args  what parse_header takes: buf, len, out
 * @var S7commNs::read_next_item_args  what read_next_item takes: data, data_len, offset, out
 * @var S7commNs::ok  true on a complete item; false at end-of-section or on truncation
 * @var S7commNs::n  the count a call reports
 * @var S7commNs::build_setup  build a Setup Communication job. Returns the PDU length, or 0 on ...
 * @var S7commNs::build_read_request  build a Read Var job for n items. Returns the PDU length, or 0 on ...
 * @var S7commNs::build_write_request  build a Write Var job (function 0x05) for n items. Mirrors the read ...
 * @var S7commNs::parse_header  parse + validate an S7comm header (protocol id, lengths)
 * @var S7commNs::read_next_item  read the next Read Var response data item from the data section
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    S7commBuildSetupArgs build_setup_args;
    S7commBuildReadRequestArgs build_read_request_args;
    S7commBuildWriteRequestArgs build_write_request_args;
    S7commParseHeaderArgs parse_header_args;
    S7commReadNextItemArgs read_next_item_args;
    proto_bool ok;
    size_t n;
} S7commVars;

/** @brief The operands and the outcome. */
extern S7commVars S7commV;

/** @brief The entries. */
typedef struct
{
    void (*const build_setup)(uint8_t *restrict work);
    void (*const build_read_request)(uint8_t *restrict work);
    void (*const build_write_request)(uint8_t *restrict work);
    void (*const parse_header)(uint8_t *restrict work);
    void (*const read_next_item)(uint8_t *restrict work);
} S7commNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in S7commV or a region of the borrow at a fixed offset.
void protocore_s7comm_build_setup(uint8_t *restrict work);
void protocore_s7comm_build_read_request(uint8_t *restrict work);
void protocore_s7comm_build_write_request(uint8_t *restrict work);
void protocore_s7comm_parse_header(uint8_t *restrict work);
void protocore_s7comm_read_next_item(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `S7comm.build_setup(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const S7commNs S7comm __attribute__((unused)) = {
    .build_setup = protocore_s7comm_build_setup,
    .build_read_request = protocore_s7comm_build_read_request,
    .build_write_request = protocore_s7comm_build_write_request,
    .parse_header = protocore_s7comm_parse_header,
    .read_next_item = protocore_s7comm_read_next_item,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_S7COMM

#endif // PROTOCORE_S7COMM_H
