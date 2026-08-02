// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_telemetry.h
 * @brief Fire-and-forget UDP telemetry cast (PC_ENABLE_UDP_TELEMETRY).
 *
 * Builds a metric line in InfluxDB line protocol -
 * `measurement,tag=v field=val,field2=val2 timestamp` (optional tags + trailing
 * timestamp; integer fields carry the `i` suffix, unsigned `u`, floats are plain)
 * - into a caller buffer, then casts it to a configured
 * collector over UDP (pc_udp_sendto), zero-heap and fire-and-forget (no ACK, no
 * retry). The line builder is pure and host-tested; only the send touches the
 * network (ESP32; a no-op on host builds).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UDP_TELEMETRY_H
#define PROTOCORE_UDP_TELEMETRY_H

#include "protocore_config.h"

#if PC_ENABLE_UDP_TELEMETRY

// ---------------------------------------------------------------------------
// Host-testable line builder (InfluxDB line protocol)
// ---------------------------------------------------------------------------

/** @brief Builder for one telemetry line over a caller buffer. */
typedef struct
{
    char *buf;              ///< destination buffer.
    size_t cap;             ///< buffer capacity in bytes.
    size_t pos;             ///< bytes written so far (excludes the null terminator).
    proto_bool overflow;    ///< true once a write did not fit (line is then unusable).
    proto_bool have_fields; ///< true once at least one field is present (comma control).
} pc_line;

/** @brief Start a line for @p measurement (bound to @p buf / @p cap). */
void pc_line_init(pc_line *l, char *buf, size_t cap, const char *measurement);

/**
 * @brief Append a `,key=value` tag (InfluxDB tag set, part of the series key).
 *
 * Tags MUST be added before any field (they sit between the measurement and the
 * fields); adding one after a field fails the line closed. Key and value are
 * escaped per line protocol (comma / equals / space backslash-escaped).
 */
void pc_line_add_tag(pc_line *l, const char *key, const char *val);

/**
 * @brief Append the trailing ` <timestamp>` (line protocol; nanoseconds by default
 *        on InfluxDB). Call after all fields; a line with no field fails closed.
 */
void pc_line_set_timestamp(pc_line *l, int64_t timestamp);

/** @brief Append `field=<v>i` (integer field). */
void pc_line_add_int(pc_line *l, const char *field, int64_t v);

/** @brief Append `field=<v>i` (unsigned integer field). */
void pc_line_add_uint(pc_line *l, const char *field, uint64_t v);

/** @brief Append `field=<v>` (float field, @p decimals places). */
void pc_line_add_float(pc_line *l, const char *field, float v, uint8_t decimals);

/** @brief Encoded length (bytes), excluding the null terminator. */
size_t pc_line_len(const pc_line *l);

/** @brief True if every field fit and the line has at least one field. */
proto_bool pc_line_ok(const pc_line *l);

// ---------------------------------------------------------------------------
// Cast (ESP32; no-op on host)
// ---------------------------------------------------------------------------

/** @brief Set the collector endpoint (dotted-quad IPv4 + UDP port). */
void pc_udp_telemetry_begin(const char *collector_ip, uint16_t port);

/** @brief Cast @p len raw bytes to the collector. @return false if not begun / host. */
proto_bool pc_udp_telemetry_send(const char *data, size_t len);

/** @brief Cast a built line to the collector (no-op if the line overflowed). */
proto_bool pc_udp_telemetry_cast(const pc_line *l);

#endif // PC_ENABLE_UDP_TELEMETRY
#endif // PROTOCORE_UDP_TELEMETRY_H
