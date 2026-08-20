// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file flow_export.h
 * @brief The Exporting Process (PROTOCORE_ENABLE_FLOW_EXPORT): builds IPFIX Messages (RFC 7011),
 *        NetFlow Version 9 Export Packets (RFC 3954), and vendor NetFlow Version 5 packets.
 *
 * RFC 7011 sec 3 "IPFIX Message Format": a Message is a Message Header (sec 3.1) followed by one
 * or more Sets (sec 3.3). RFC 3954 sec 5 "Export Packet Format" is the same shape one revision
 * earlier: a Header (sec 5.1) followed by FlowSets. Every field is network byte order.
 *
 * Template-then-data. A Template Record (RFC 7011 sec 3.4.1, RFC 3954 sec 5.2) lists the Field
 * Specifiers a record carries and is given a Template ID; Data Records (RFC 7011 sec 3.4.3,
 * RFC 3954 sec 5.3) then travel in a Set whose Set ID is that Template ID. RFC 7011 sec 3.3.2:
 * "A value of 2 is reserved for Template Sets... Values 256 and above are used for Data Sets."
 * RFC 3954 sec 5.2 uses FlowSet ID 0 for the Template FlowSet and reserves IDs 0-255.
 *
 * A Field Specifier (RFC 7011 sec 3.2) is an Information Element identifier plus a Field Length.
 * The identifier is the elementId of RFC 7012 sec 2.1, from the IANA "IPFIX Information Elements"
 * registry (RFC 7012 sec 7.1). The E bit stays zero here, so no Enterprise Number follows.
 *
 * NetFlow Version 5 has no IETF specification. It is a vendor-defined fixed export format
 * (Cisco Systems NetFlow Version 5); RFC 3954 specifies Version 9 only and does not describe
 * Version 5. The layout built here is that vendor format: a 24-octet header then N 48-octet
 * records.
 *
 * One message is under construction at a time: ipfix_begin or v9_begin, then template_set,
 * data_set_begin, data_record, data_set_end, then message_finish, which patches the IPFIX
 * Message Length (RFC 7011 sec 3.1) or the v9 Count (RFC 3954 sec 5.1) and reports the octets.
 * This is the wire codec only; the flow cache is the app's and the datagram send is
 * `Udp.client->sendto`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FLOW_EXPORT_H
#define PROTOCORE_FLOW_EXPORT_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_FLOW_EXPORT

PROTOCORE_BEGIN_DECLS

#define FLOW_V5_HEADER_SIZE 24 ///< octets in the vendor Version 5 packet header
#define FLOW_V5_RECORD_SIZE 48 ///< octets in one vendor Version 5 flow record

/** @brief Vendor NetFlow Version 5 packet header. The builder writes Version 5 itself. */
typedef struct
{
    uint16_t count;             ///< number of records that follow
    uint32_t sys_uptime;        ///< ms since the device booted
    uint32_t unix_secs;         ///< seconds since the epoch
    uint32_t unix_nsecs;        ///< residual nanoseconds
    uint32_t flow_sequence;     ///< running count of exported flows
    uint8_t engine_type;        ///< flow-switching engine type
    uint8_t engine_id;          ///< flow-switching engine id
    uint16_t sampling_interval; ///< sampling mode in the top 2 bits, interval in the rest
} FlowV5Header;

/** @brief One vendor NetFlow Version 5 flow record. The builder zero-fills both pad spans. */
typedef struct
{
    uint32_t src_addr; ///< source IPv4, host order, written big-endian
    uint32_t dst_addr; ///< destination IPv4
    uint32_t next_hop; ///< next-hop router IPv4
    uint16_t input;    ///< ingress interface SNMP index
    uint16_t output;   ///< egress interface SNMP index
    uint32_t d_pkts;   ///< packets in the flow
    uint32_t d_octets; ///< octets in the flow
    uint32_t first;    ///< sys_uptime at flow start
    uint32_t last;     ///< sys_uptime at the last packet
    uint16_t src_port; ///< source transport port
    uint16_t dst_port; ///< destination transport port
    uint8_t tcp_flags; ///< cumulative OR of the flow's TCP flags
    uint8_t prot;      ///< IP protocol number
    uint8_t tos;       ///< IP type of service
    uint16_t src_as;   ///< source autonomous system
    uint16_t dst_as;   ///< destination autonomous system
    uint8_t src_mask;  ///< source prefix length
    uint8_t dst_mask;  ///< destination prefix length
} FlowV5Record;

/**
 * @brief RFC 7011 sec 3.2 Field Specifier: one Information Element and its on-wire length.
 *        RFC 3954 sec 5.2 calls the same pair Field Type and Field Length.
 */
typedef struct
{
    uint16_t information_element_id; ///< RFC 7012 sec 2.1 elementId, E bit zero
    uint16_t field_length;           ///< RFC 7011 sec 3.2 Field Length, in octets
} FlowFieldSpecifier;

/** @brief Where a builder writes and how far it may go. */
typedef struct
{
    uint8_t *buf; ///< the octets a build fills
    size_t cap;   ///< how many octets it may use
} FlowOutArgs;

/** @brief The vendor Version 5 structures a fixed-format write reads. */
typedef struct
{
    const FlowV5Header *header; ///< the 24-octet packet header a write emits
    const FlowV5Record *record; ///< the 48-octet flow record a write emits
} FlowV5Args;

/** @brief The Message Header fields a begin writes: RFC 7011 sec 3.1, RFC 3954 sec 5.1. */
typedef struct
{
    uint32_t sys_uptime;            ///< RFC 3954 sec 5.1 sysUpTime, ms since the device booted
    uint32_t unix_secs;             ///< RFC 3954 sec 5.1 UNIX Secs, seconds since the epoch
    uint32_t export_time;           ///< RFC 7011 sec 3.1 Export Time, seconds since the epoch
    uint32_t sequence_number;       ///< RFC 3954 sec 5.1 counts Export Packets, RFC 7011 sec 3.1 counts Data Records
    uint32_t observation_domain_id; ///< RFC 7011 sec 3.1 Observation Domain ID, RFC 3954 sec 5.1 Source ID
} FlowMessageArgs;

/** @brief RFC 7011 sec 3.4.1 / RFC 3954 sec 5.2: what one Template Record lists. */
typedef struct
{
    const FlowFieldSpecifier *fields; ///< the Field Specifiers, in wire order
    size_t field_count;               ///< RFC 7011 sec 3.4.1 / RFC 3954 sec 5.2 Field Count
} FlowTemplateArgs;

/** @brief RFC 7011 sec 3.4.3 / RFC 3954 sec 5.3: one already-encoded Data Record. */
typedef struct
{
    const uint8_t *record; ///< its Field Values in Template order, big-endian
    size_t len;            ///< its octet length
} FlowDataArgs;

/**
 * @brief The flow-record Exporting Process: RFC 7011 IPFIX, RFC 3954 NetFlow v9, vendor v5.
 *
 * A caller sets the members a call takes, invokes it through ::FlowExport, and reads the outcome
 * off the same handle.
 *
 * @var FlowExportNs::template_id  RFC 7011 sec 3.4.1 / RFC 3954 sec 5.2 Template ID a Set names
 * @var FlowExportNs::out          where a builder writes and how far it may go
 * @var FlowExportNs::v5           the vendor Version 5 header and record a fixed write emits
 * @var FlowExportNs::message      the Message Header fields a begin writes
 * @var FlowExportNs::tmpl         the Field Specifiers a Template Record lists
 * @var FlowExportNs::data         one encoded Data Record and its length
 * @var FlowExportNs::ok           a call's true/false outcome
 * @var FlowExportNs::n            octets a v5 write emitted, or the finished message length
 * @var FlowExportNs::v5_header      write the 24-octet vendor Version 5 packet header
 * @var FlowExportNs::v5_record      write one 48-octet vendor Version 5 flow record
 * @var FlowExportNs::ipfix_begin    write the IPFIX Message Header (RFC 7011 sec 3.1)
 * @var FlowExportNs::v9_begin       write the NetFlow v9 packet Header (RFC 3954 sec 5.1)
 * @var FlowExportNs::template_set   emit a Template Set (RFC 7011 sec 3.3.2 Set ID 2) or a Template
 *                                   FlowSet (RFC 3954 sec 5.2 FlowSet ID 0)
 * @var FlowExportNs::data_set_begin open a Data Set for template_id (RFC 7011 sec 3.3.2,
 *                                   RFC 3954 sec 5.3)
 * @var FlowExportNs::data_record    append one Data Record to the open Set
 * @var FlowExportNs::data_set_end   patch the Set Length and, for v9, pad to a 4-octet boundary
 * @var FlowExportNs::message_finish close any open Set, patch the IPFIX Length or the v9 Count
 */
typedef struct
{
    uint16_t template_id;    ///< the Template ID a Template Set or a Data Set names
    FlowOutArgs out;         ///< where a builder writes
    FlowV5Args v5;           ///< the vendor Version 5 structures a fixed write emits
    FlowMessageArgs message; ///< the Message Header fields a begin writes
    FlowTemplateArgs tmpl;   ///< the Field Specifiers a Template Record lists
    FlowDataArgs data;       ///< one encoded Data Record
    proto_bool ok;
    size_t n;
} FlowExportVars;

/** @brief The operands and the outcome. */
extern FlowExportVars FlowExportV;

/** @brief The entries. */
typedef struct
{
    void (*const v5_header)(uint8_t *restrict work);
    void (*const v5_record)(uint8_t *restrict work);
    void (*const ipfix_begin)(uint8_t *restrict work);
    void (*const v9_begin)(uint8_t *restrict work);
    void (*const template_set)(uint8_t *restrict work);
    void (*const data_set_begin)(uint8_t *restrict work);
    void (*const data_record)(uint8_t *restrict work);
    void (*const data_set_end)(uint8_t *restrict work);
    void (*const message_finish)(uint8_t *restrict work);
} FlowExportNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in FlowExportV or a region of the borrow at a fixed offset.
void protocore_flow_export_v5_header(uint8_t *restrict work);
void protocore_flow_export_v5_record(uint8_t *restrict work);
void protocore_flow_export_ipfix_begin(uint8_t *restrict work);
void protocore_flow_export_v9_begin(uint8_t *restrict work);
void protocore_flow_export_template_set(uint8_t *restrict work);
void protocore_flow_export_data_set_begin(uint8_t *restrict work);
void protocore_flow_export_data_record(uint8_t *restrict work);
void protocore_flow_export_data_set_end(uint8_t *restrict work);
void protocore_flow_export_message_finish(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `FlowExport.v5_header(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const FlowExportNs FlowExport __attribute__((unused)) = {
    .v5_header = protocore_flow_export_v5_header,
    .v5_record = protocore_flow_export_v5_record,
    .ipfix_begin = protocore_flow_export_ipfix_begin,
    .v9_begin = protocore_flow_export_v9_begin,
    .template_set = protocore_flow_export_template_set,
    .data_set_begin = protocore_flow_export_data_set_begin,
    .data_record = protocore_flow_export_data_record,
    .data_set_end = protocore_flow_export_data_set_end,
    .message_finish = protocore_flow_export_message_finish,
};

/**
 * @brief The PROTOCORE_FLOW_EXPORT_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_flow_export_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FLOW_EXPORT

#endif // PROTOCORE_FLOW_EXPORT_H
