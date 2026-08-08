// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file statsd.h
 * @brief StatsD metrics client - push counters/gauges/timings/sets to a StatsD collector.
 *
 * StatsD is the de-facto push metrics protocol: one line per metric, `name:value|type`, over
 * UDP (fire-and-forget). It is spoken by Graphite/StatsD, Telegraf, Datadog, InfluxDB, and
 * most observability stacks. This is the push counterpart to the pull-based Prometheus
 * `/metrics` endpoint: instead of a scraper polling the device, the device shoves metrics out
 * as things happen - handy behind NAT/firewalls where nothing can reach in to scrape.
 *
 * Types: counter (`c`), gauge (`g`, absolute or a signed `+`/`-` delta), timing (`ms`), and
 * set (`s`). Optional sample rate (`|@0.1`) and DogStatsD tags (`|#env:prod,host:a`).
 *
 * The line builder (pc_statsd_format) is pure and host-tested; the emit helpers format the value
 * and send via the transport UDP service (Udp.client->sendto), so they are host-testable through
 * its capture seam too. Zero heap; gated by PC_ENABLE_STATSD.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STATSD_H
#define PROTOCORE_STATSD_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_STATSD

/** @brief StatsD metric type codes (the token after the `|`). */
typedef enum PROTO_ENUM_PACKED
{
    STATSD_COUNTER = 'c',
    STATSD_GAUGE = 'g',
    STATSD_TIMING = 'm', ///< emitted as "ms"
    STATSD_SET = 's',
} StatsdType;

/**
 * @brief Format one StatsD line: `name:value|type[|@rate][|#tags]`. Pure - no I/O.
 *
 * @param out    output buffer (NUL-terminated on success).
 * @param cap    capacity of @p out.
 * @param name   metric name (e.g. "api.requests").
 * @param value  the value already rendered as text (e.g. "1", "-3", "42.5").
 * @param type   a ::StatsdType code ('c','g','m','s').
 * @param rate   sample rate in (0,1]; >= 1 emits no `|@` annotation.
 * @param tags   DogStatsD tag string "k:v,k2:v2" (no leading `#`), or nullptr for none.
 * @return       line length (excluding the NUL), or 0 on a bad argument or overflow.
 */
size_t pc_statsd_format(char *out, size_t cap, const char *name, const char *value, StatsdType type, float rate,
                        const char *tags);

/**
 * @brief Point the client at a collector and set optional global tags (added to every metric).
 * @param host        collector hostname/IP.
 * @param port        UDP port (0 selects PC_STATSD_PORT).
 * @param global_tags DogStatsD tags applied to every metric, or nullptr.
 */
void pc_statsd_begin(const char *host, uint16_t port, const char *global_tags);

/** @brief Increment a counter by @p delta (may be negative). */
void pc_statsd_count(const char *name, int64_t delta);

/** @brief Increment a counter, annotated with sample @p rate (0,1] so the server scales up. */
void pc_statsd_count_sampled(const char *name, int64_t delta, float rate);

/** @brief Set a gauge to an absolute @p value. */
void pc_statsd_gauge(const char *name, int64_t value);

/** @brief Adjust a gauge by a signed @p delta (sent as a `+`/`-` gauge update). */
void pc_statsd_gauge_delta(const char *name, int64_t delta);

/** @brief Record a timing/duration of @p ms milliseconds. */
void pc_statsd_timing(const char *name, uint32_t ms);

/** @brief Add a unique @p member to a set (counts distinct values server-side). */
void pc_statsd_set(const char *name, const char *member);

#endif // PC_ENABLE_STATSD

PROTO_END_DECLS

#endif // PROTOCORE_STATSD_H
