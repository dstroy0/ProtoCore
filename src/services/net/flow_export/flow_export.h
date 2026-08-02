// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file flow_export.h
 * @brief Flow-record export codec (PC_ENABLE_FLOW_EXPORT) - zero-heap exporter-side
 *        builders for NetFlow v5, NetFlow v9 (RFC 3954), and IPFIX (RFC 7011), so a device
 *        can ship on-device flow accounting to a collector over the existing UDP transport.
 *
 * Three formats, all big-endian on the wire:
 *  - NetFlow v5: a fixed legacy layout - a 24-octet header then N fixed 48-octet records.
 *  - NetFlow v9 / IPFIX: the template-then-data model - the exporter sends Template records
 *    describing a record's field layout, then Data records that match a template by id.
 *
 * The v9 / IPFIX side is a small cursor (@ref FlowWriter): begin the message, emit a
 * template, open a data set, append records, then finish (which patches the message
 * length for IPFIX or the record count for v9). Field offsets verified against RFC 7011
 * (IPFIX), RFC 3954 (NetFlow v9), and the published v5 record layout.
 *
 * This is the wire codec only; the flow cache (the 5-tuple + counters) is the app's, and
 * the datagram send is `pc_udp_sendto`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FLOW_EXPORT_H
#define PROTOCORE_FLOW_EXPORT_H

#include "protocore_config.h"

#if PC_ENABLE_FLOW_EXPORT

// ---- NetFlow v5 (fixed legacy format) ----

#define FLOW_V5_HEADER_SIZE 24
#define FLOW_V5_RECORD_SIZE 48

/** @brief NetFlow v5 packet header (the builder fills version=5). */
typedef struct
{
    uint16_t count;             ///< number of records that follow
    uint32_t sys_uptime;        ///< ms since the device booted
    uint32_t unix_secs;         ///< seconds since the epoch
    uint32_t unix_nsecs;        ///< residual nanoseconds
    uint32_t flow_sequence;     ///< running count of exported flows
    uint8_t engine_type;        ///< flow-switching engine type
    uint8_t engine_id;          ///< flow-switching engine id
    uint16_t sampling_interval; ///< sampling mode (top 2 bits) + interval
} FlowV5Header;

/** @brief One NetFlow v5 flow record (pad1 / pad2 are zero-filled by the builder). */
typedef struct
{
    uint32_t src_addr; ///< source IPv4 (host order; written big-endian)
    uint32_t dst_addr; ///< destination IPv4
    uint32_t next_hop; ///< next-hop router IPv4
    uint16_t input;    ///< ingress interface SNMP index
    uint16_t output;   ///< egress interface SNMP index
    uint32_t d_pkts;   ///< packets in the flow
    uint32_t d_octets; ///< bytes in the flow
    uint32_t first;    ///< sys_uptime at flow start
    uint32_t last;     ///< sys_uptime at last packet
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t tcp_flags; ///< cumulative OR of TCP flags
    uint8_t prot;      ///< IP protocol
    uint8_t tos;       ///< IP type of service
    uint16_t src_as;
    uint16_t dst_as;
    uint8_t src_mask; ///< source prefix length
    uint8_t dst_mask; ///< destination prefix length
} FlowV5Record;

/** @brief Write the 24-octet v5 header; returns FLOW_V5_HEADER_SIZE, or 0 on overflow. */
size_t flow_v5_write_header(uint8_t *buf, size_t cap, const FlowV5Header *h);

/** @brief Write one 48-octet v5 record; returns FLOW_V5_RECORD_SIZE, or 0 on overflow. */
size_t flow_v5_write_record(uint8_t *buf, size_t cap, const FlowV5Record *r);

// ---- NetFlow v9 / IPFIX (template-then-data) ----

/** @brief A template field specifier: an Information Element id + its on-wire octet length. */
typedef struct
{
    uint16_t type;
    uint16_t length;
} FlowField;

/** @brief Cursor for building one v9 / IPFIX message. Treat the fields as opaque. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t pos;
    size_t set_start; ///< offset of the open Set/FlowSet header, or 0 when none is open
    uint16_t records; ///< running record count (the v9 'count' field)
    uint8_t version;  ///< 9 (NetFlow v9) or 10 (IPFIX)
    proto_bool error; ///< sticky overflow / misuse flag
} FlowWriter;

/** @brief Begin an IPFIX (version 10) message: writes the 16-octet header (length patched on finish). */
proto_bool flow_ipfix_begin(FlowWriter *w, uint8_t *buf, size_t cap, uint32_t export_time, uint32_t seq,
                            uint32_t domain_id);

/** @brief Begin a NetFlow v9 message: writes the 20-octet header (count patched on finish). */
proto_bool flow_v9_begin(FlowWriter *w, uint8_t *buf, size_t cap, uint32_t sys_uptime, uint32_t unix_secs, uint32_t seq,
                         uint32_t source_id);

/**
 * @brief Emit a Template (Set ID 2 for IPFIX, FlowSet ID 0 for v9) describing @p fields.
 * @param template_id the id data records will reference (>= 256).
 */
proto_bool flow_export_template(FlowWriter *w, uint16_t template_id, const FlowField *fields, size_t field_count);

/** @brief Open a Data Set/FlowSet for @p template_id (must match a previously emitted template). */
proto_bool flow_export_data_begin(FlowWriter *w, uint16_t template_id);

/** @brief Append one already-encoded data record (its fields must match the template, big-endian). */
proto_bool flow_export_data_record(FlowWriter *w, const uint8_t *record, size_t len);

/** @brief Close the open Data Set (patches its length; pads to a 4-octet boundary for v9). */
proto_bool flow_export_data_end(FlowWriter *w);

/** @brief Finish the message (auto-closes an open set); returns total bytes, or 0 on error. */
size_t flow_export_finish(FlowWriter *w);

#endif // PC_ENABLE_FLOW_EXPORT

#endif // PROTOCORE_FLOW_EXPORT_H
