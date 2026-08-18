// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_telemetry.h
 * @brief One point in line protocol, cast to a collector as one UDP datagram
 *        (PROTOCORE_ENABLE_UDP_TELEMETRY).
 *
 * The payload is line protocol, the text format InfluxData specifies for a point. That is a vendor
 * specification, published as "Line protocol" in the InfluxData documentation (InfluxDB v2,
 * reference/syntax/line-protocol; InfluxDB v1, write_protocols/line_protocol_reference). No IETF
 * document governs it and it carries no RFC number.
 *
 * The syntax that reference gives is
 *
 *     <measurement>[,<tag_key>=<tag_value>...] <field_key>=<field_value>[,...] [<timestamp>]
 *
 * and it names four elements: measurement (required), tag set (optional), field set (required, at
 * least one entry) and timestamp (optional, Unix nanoseconds by default). A field value carries its
 * type in its suffix: `i` signed integer, `u` unsigned integer, no suffix float. Tag keys and tag
 * values escape comma, equals and space with a backslash ("Special characters").
 *
 * The transport is UDP, RFC 768. Its "User Interface" section names the send: "an operation that
 * allows a datagram to be sent, specifying the data, source and destination ports and addresses to
 * be sent". RFC 768 also states "delivery and duplicate protection are not guaranteed", so a write
 * reports only that the stack took the octets: nothing is acknowledged and nothing is retried.
 *
 * A line is built into a buffer the caller owns, one element per call, and this module holds the
 * position in it. That buffer has to outlive the calls between a measurement and its write. Nothing
 * here allocates. A build with no network stack builds lines and refuses every send.
 *
 * The module exports one symbol, @ref UdpTelemetry. Everything in udp_telemetry.c has internal
 * linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_TELEMETRY_H
#define PROTOCORE_UDP_TELEMETRY_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_UDP_TELEMETRY

PROTOCORE_BEGIN_DECLS

/** @brief RFC 768 "User Interface": the destination address and port every datagram carries. */
typedef struct
{
    const char *addr; ///< the collector's address as text, v4 or v6, parsed once by a begin
    uint16_t port;    ///< its UDP port
} UdpTelemetryCollectorArgs;

/** @brief Where one line is built, and the measurement it opens with (line protocol element 1). */
typedef struct
{
    char *buf;               ///< the caller's buffer; it outlives the calls that build into it
    size_t cap;              ///< how much room it has, the NUL included
    const char *measurement; ///< the measurement name; NULL opens the line with nothing
} UdpTelemetryLineArgs;

/** @brief One tag set entry, `,tag_key=tag_value` (line protocol element 2). */
typedef struct
{
    const char *key;   ///< the tag key; comma, equals and space are escaped
    const char *value; ///< its tag value, escaped the same way; NULL writes nothing after the equals
} UdpTelemetryTagArgs;

/** @brief One field set entry, `field_key=field_value` (line protocol element 3). */
typedef struct
{
    const char *key;  ///< the field key
    int64_t i64;      ///< the value a field_int writes, suffixed `i`
    uint64_t u64;     ///< the value a field_uint writes, suffixed `u`
    float f32;        ///< the value a field_float writes, unsuffixed
    uint8_t decimals; ///< digits after the point a field_float writes
} UdpTelemetryFieldArgs;

/** @brief The trailing timestamp (line protocol element 4). */
typedef struct
{
    int64_t unix_ns; ///< the point's time in Unix nanoseconds, the default precision
} UdpTelemetryTimestampArgs;

/** @brief The octets one datagram carries. */
typedef struct
{
    const char *data; ///< the bytes a send hands the stack
    size_t len;       ///< how many
} UdpTelemetryPayloadArgs;

/**
 * @brief The line protocol caster.
 *
 * A caller sets the members a call takes, invokes it through ::UdpTelemetry, and reads the outcome
 * off the same handle. A measurement opens a line, tag and field_* append its elements in that
 * order, and a write sends the whole line as one datagram.
 *
 * No slot member: one collector, one line at a time, so no call names a row.
 *
 * @var UdpTelemetryNs::collector    where the datagrams go (RFC 768 "User Interface")
 * @var UdpTelemetryNs::line         the buffer a line is built in and the measurement it opens with
 * @var UdpTelemetryNs::tags         one tag set entry
 * @var UdpTelemetryNs::fields       one field set entry, in the width the call takes
 * @var UdpTelemetryNs::time         the trailing timestamp
 * @var UdpTelemetryNs::payload      the octets a send hands the stack
 * @var UdpTelemetryNs::ok           a call's true/false outcome; on a build call, the line is a
 *                                   complete point so far
 * @var UdpTelemetryNs::overflow     an append did not fit; every later append is a no-op
 * @var UdpTelemetryNs::n            octets the line holds, the NUL excluded
 * @var UdpTelemetryNs::begin        parse the collector address and store it with its port
 * @var UdpTelemetryNs::measurement  bind @c line and open it with the measurement
 * @var UdpTelemetryNs::tag          append one tag set entry, before any field
 * @var UdpTelemetryNs::field_int    append `field_key=<i64>i`
 * @var UdpTelemetryNs::field_uint   append `field_key=<u64>u`
 * @var UdpTelemetryNs::field_float  append `field_key=<f32>` to @c decimals places
 * @var UdpTelemetryNs::timestamp    append the trailing timestamp, after the field set
 * @var UdpTelemetryNs::send         send @c payload to the collector as one datagram
 * @var UdpTelemetryNs::write        send the built line as one datagram; an incomplete line sends
 *                                   nothing
 */
typedef struct
{
    UdpTelemetryCollectorArgs collector; ///< where the datagrams go
    UdpTelemetryLineArgs line;           ///< where a line is built
    UdpTelemetryTagArgs tags;            ///< one tag set entry
    UdpTelemetryFieldArgs fields;        ///< one field set entry
    UdpTelemetryTimestampArgs time;      ///< the trailing timestamp
    UdpTelemetryPayloadArgs payload;     ///< what a send carries

    proto_bool ok;
    proto_bool overflow;
    size_t n;

    void (*const begin)(uint8_t *restrict work);
    void (*const measurement)(uint8_t *restrict work);
    void (*const tag)(uint8_t *restrict work);
    void (*const field_int)(uint8_t *restrict work);
    void (*const field_uint)(uint8_t *restrict work);
    void (*const field_float)(uint8_t *restrict work);
    void (*const timestamp)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
    void (*const write)(uint8_t *restrict work);
} UdpTelemetryNs;

/** @brief The one symbol this module exports. */
extern UdpTelemetryNs UdpTelemetry;

/**
 * @brief The PROTOCORE_UDP_TELEMETRY_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_udp_telemetry_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_UDP_TELEMETRY

#endif // PROTOCORE_UDP_TELEMETRY_H
