// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file statsd.h
 * @brief The StatsD metrics client: one metric line per UDP datagram.
 *
 * **No formal specification governs StatsD.** It is a line protocol that originated at Etsy, and the
 * only description of it is the reference implementation's own documentation, github.com/statsd/statsd
 * `README.md` and `docs/metric_types.md`. There is no IETF RFC for it, and no OASIS, OMG, W3C, CNCF
 * or Eclipse document defines it. The names below are that documentation's own.
 *
 * `docs/metric_types.md` gives the line as `<metricname>:<value>|<type>` and names four types:
 *
 *     Counting  gorets:1|c       "Add 1 to the 'gorets' bucket."
 *     Sampling  gorets:1|c|@0.1  "this counter is being sent sampled every 1/10th of the time"
 *     Timing    glork:320|ms     "The glork took 320ms to complete this time."
 *     Gauges    gaugor:333|g     "will maintain its value until it is next set"
 *     Sets      uniques:765|s    "counting unique occurrences of events between flushes"
 *
 * The sample rate is the counter's and the timer's: "The field is optional and defaults to 1."
 * A signed gauge value adjusts instead of assigning, `gaugor:-10|g` then `gaugor:+4|g` taking 333 to
 * 327. `README.md`: "Each stat is in its own 'bucket'. They are not predefined anywhere", and its
 * example names 8125 as the port the reference daemon listens on.
 *
 * The `|#k:v,k2:v2` tail is not Etsy's. It is DogStatsD, Datadog's extension, whose datagram format
 * is `<METRIC_NAME>:<VALUE>|<TYPE>|@<SAMPLE_RATE>|#<TAG_KEY_1>:<TAG_VALUE_1>,<TAG_2>` and whose tag
 * field is "A comma separated list of strings. Use colons for key/value tags (`env:prod`)". A daemon
 * that does not speak it ignores the field.
 *
 * The daemon address and the tag list every metric carries are copied into fixed storage by an init,
 * so nothing a caller passes has to outlive the call. Values are rendered by hand and the line is
 * assembled into storage, so a metric costs no heap and nothing lands on a task stack.
 *
 * The module exports one symbol, @ref Statsd. Everything in statsd.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STATSD_H
#define PROTOCORE_STATSD_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_STATSD

PROTOCORE_BEGIN_DECLS

/**
 * @brief The four metric types of github.com/statsd/statsd `docs/metric_types.md`.
 *
 * The value is the selector a call sets, not the token the line carries: a timing selects with 'm'
 * and writes `ms`.
 */
typedef enum PROTO_ENUM_PACKED
{
    STATSD_COUNTER = 'c', ///< Counting: `gorets:1|c`
    STATSD_GAUGE = 'g',   ///< Gauges: `gaugor:333|g`, or a signed value to adjust
    STATSD_TIMING = 'm',  ///< Timing: written as `ms`, `glork:320|ms`
    STATSD_SET = 's',     ///< Sets: `uniques:765|s`
} StatsdType;

/** @brief The StatsD daemon every datagram is sent to. */
typedef struct
{
    const char *addr; ///< its address as text, v4 or v6, parsed once by an init
    uint16_t port;    ///< its UDP port; 0 selects ::PROTOCORE_STATSD_PORT, the reference daemon's 8125
} StatsdServerArgs;

/** @brief The DogStatsD tag lists, "k:v,k2:v2" with no leading `#`. */
typedef struct
{
    const char *global; ///< the list an init stores and every metric carries; NULL or "" stores none
    const char *metric; ///< the list one format writes; a metric call stamps the stored one here
} StatsdTagArgs;

/** @brief What one line names, in the grammar `<metricname>:<value>|<type>[|@<rate>]`. */
typedef struct
{
    const char *name; ///< the bucket, "api.requests"; empty formats nothing
    StatsdType type;  ///< which of the four types; a metric call stamps its own
    float rate;       ///< the sample rate in (0,1); 0 or >= 1 writes no `|@` field
} StatsdMetricArgs;

/** @brief The value a line carries, in the form the call taking it reads. */
typedef struct
{
    const char *text;   ///< the value already rendered, what a format reads
    int64_t i64;        ///< a counter delta, a gauge's absolute value, or a signed gauge adjustment
    uint32_t ms;        ///< a timing in milliseconds
    const char *member; ///< the set member counted as one unique occurrence; NULL writes empty
} StatsdValueArgs;

/** @brief Where a formatted metric line lands. */
typedef struct
{
    char *out;  ///< the buffer a format writes the line into
    size_t cap; ///< how much room it has, the NUL included
} StatsdLineArgs;

/** @brief The client's own state and the calls that reach it, described only in statsd.c. */
struct StatsdInternal;

/**
 * @brief The StatsD metrics client.
 *
 * A caller sets the members a call takes, invokes it through ::Statsd, and reads the outcome off the
 * same handle.
 *
 * No slot member: one client sends to one daemon, so no call names a row.
 *
 * @var StatsdNs::server       the daemon an init parses and every metric is sent to
 * @var StatsdNs::tags         the DogStatsD tag lists, the stored one and the per-line one
 * @var StatsdNs::metric       the bucket, the type and the sample rate one line carries
 * @var StatsdNs::value        the value one line carries, in each form a call reads
 * @var StatsdNs::line         the buffer a format writes into
 * @var StatsdNs::ok           a call's true/false outcome
 * @var StatsdNs::n            the line length a format wrote, excluding the NUL, 0 if it did not fit
 * @var StatsdNs::init         parse the daemon address and copy its port and @c tags.global into storage
 * @var StatsdNs::format       build one line from @c metric, @c value.text and @c tags.metric into @c line
 * @var StatsdNs::count        add @c value.i64 to the bucket as `|c`, annotated with @c metric.rate
 * @var StatsdNs::gauge        assign @c value.i64 to the bucket as `|g`
 * @var StatsdNs::gauge_delta  adjust the bucket by @c value.i64 as a signed `|g`
 * @var StatsdNs::timing       record @c value.ms as `|ms`
 * @var StatsdNs::set          count @c value.member as one unique occurrence, `|s`
 * @var StatsdNs::internal     the client's state and the calls that reach it
 *
 * @c format touches no socket. Every other metric call renders its value into the client's scratch,
 * stamps its own @c metric.type and the stored @c tags.global onto @c tags.metric, formats into the
 * client's line storage, and sends those octets as one datagram. @c gauge, @c gauge_delta, @c timing
 * and @c set stamp @c metric.rate to 1, the unsampled rate, so no `|@` field is written.
 */
typedef struct
{
    StatsdServerArgs server; ///< where the datagrams go
    StatsdTagArgs tags;      ///< what every line, and this line, is tagged with
    StatsdMetricArgs metric; ///< what one line names
    StatsdValueArgs value;   ///< what one line carries
    StatsdLineArgs line;     ///< where the formatted line lands

    proto_bool ok;
    size_t n;

    void (*init)(struct StatsdInternal *ctx);
    void (*format)(struct StatsdInternal *ctx);
    void (*count)(struct StatsdInternal *ctx);
    void (*gauge)(struct StatsdInternal *ctx);
    void (*gauge_delta)(struct StatsdInternal *ctx);
    void (*timing)(struct StatsdInternal *ctx);
    void (*set)(struct StatsdInternal *ctx);

    struct StatsdInternal *internal;
} StatsdNs;

/** @brief The one symbol this module exports. */
extern StatsdNs Statsd;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_STATSD

#endif // PROTOCORE_STATSD_H
