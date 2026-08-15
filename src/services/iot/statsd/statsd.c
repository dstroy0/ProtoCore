// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file statsd.c
 * @brief The StatsD metrics client: the line framer and the send. See statsd.h.
 *
 * format() builds `<metricname>:<value>|<type>[|@<rate>][|#<tags>]` into the caller's buffer and
 * touches no socket. Every other metric call renders its value into the client's scratch, formats
 * into the client's line storage, and hands those octets to the UDP sending side as one datagram.
 *
 * The value and the sample rate are rendered digit by digit, so nothing here needs the C runtime's
 * 64-bit or floating-point conversion.
 */

#include "services/iot/statsd/statsd.h"

#if PROTOCORE_ENABLE_STATSD

#include "mmgr/protomem.h"                               // mem.cpy: the spans a line is assembled from
#include "mmgr/protostr.h"                               // str.copy / str.len: the bounded field moves
#include "network_drivers/transport/udp/client/client.h" // UdpClient.sendto: one metric, one datagram
#include "shared/ip/ip.h"                                // Ip.parse: the daemon address, once

/** @brief Bytes the stored DogStatsD tag list occupies, the NUL included. */
#ifndef PROTOCORE_STATSD_TAGS_MAX
#define PROTOCORE_STATSD_TAGS_MAX 96
#endif

/** @brief Bytes one rendered value occupies: 20 digits, a sign, and the NUL. */
#ifndef PROTOCORE_STATSD_VALUE_MAX
#define PROTOCORE_STATSD_VALUE_MAX 24
#endif

/** @brief Bytes one rendered sample rate occupies: "0." and three digits, and the NUL. */
#define PROTOCORE_STATSD_RATE_MAX 8

/**
 * @brief The client's compile-time storage: the daemon, the stored tags, and the two scratches.
 *
 * All of it BSS, so a metric costs no heap.
 */
struct StatsdStorage
{
    protocore_ip server;                  ///< the daemon address, parsed by an init
    uint16_t port;                        ///< its UDP port
    char tags[PROTOCORE_STATSD_TAGS_MAX]; ///< the DogStatsD list every metric carries
    proto_bool ready;                     ///< the daemon address parsed; every send is gated on it
    char val[PROTOCORE_STATSD_VALUE_MAX]; ///< the value a metric call renders
    char buf[PROTOCORE_STATSD_LINE_MAX];  ///< the line a metric call builds
};

/**
 * @brief The client's state and the calls that reach it - what StatsdNs points at.
 *
 * @var StatsdInternal::store  the daemon, the stored tags, and the value and line scratches
 * @var StatsdInternal::ns     the handle a caller sets a call's members on
 */
struct StatsdInternal
{
    struct StatsdStorage *store;
    StatsdNs *ns;
};

static struct StatsdStorage s_store = {.port = PROTOCORE_STATSD_PORT};

static struct StatsdInternal s_statsd = {.store = &s_store, .ns = &Statsd};

// Unsigned to decimal, most significant digit first, no terminator. Returns the digit count.
static size_t u64_str(char *b, uint64_t v)
{
    if (v == 0)
    {
        b[0] = '0';
        return 1;
    }
    char tmp[20];
    size_t n = 0;
    while (v)
    {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    for (size_t i = 0; i < n; i++)
    {
        b[i] = tmp[n - 1 - i];
    }
    return n;
}

// Signed to decimal. The most negative value negates through -(v+1)+1, which stays in range.
static size_t i64_str(char *b, int64_t v)
{
    if (v < 0)
    {
        b[0] = '-';
        return 1 + u64_str(b + 1, (uint64_t)(-(v + 1)) + 1);
    }
    return u64_str(b, (uint64_t)v);
}

// Signed to "+N" or "-N", the signed gauge value that adjusts rather than assigns.
static size_t i64_delta_str(char *b, int64_t v)
{
    if (v >= 0)
    {
        b[0] = '+';
        return 1 + u64_str(b + 1, (uint64_t)v);
    }
    return i64_str(b, v); // already carries the '-'
}

// A rate in (0,1) to "0.xxx", thousandths, trailing zeros dropped. 0 or >= 1 renders nothing.
static size_t rate_str(char *b, float r)
{
    if (r >= 1.0f || r <= 0.0f)
    {
        return 0;
    }
    int m = (int)(r * 1000.0f + 0.5f);
    if (m <= 0)
    {
        m = 1;
    }
    if (m > 999)
    {
        m = 999;
    }
    b[0] = '0';
    b[1] = '.';
    b[2] = (char)('0' + (m / 100) % 10);
    b[3] = (char)('0' + (m / 10) % 10);
    b[4] = (char)('0' + m % 10);
    size_t len = 5;
    while (len > 3 && b[len - 1] == '0')
    {
        len--; // "0.100" to "0.1"
    }
    return len;
}

// Append len bytes at *pos while the line still leaves room for a trailing NUL. False the moment it
// would not fit, so an over-long line reports 0 rather than a truncated metric.
static inline proto_bool line_append(char *out, size_t cap, size_t *pos, const char *src, size_t len)
{
    if (*pos + len >= cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(out + *pos, src, len);
    *pos += len;
    return PROTO_TRUE;
}

// Parse the daemon address and store the port and the tag list every later line carries.
static void statsd_init(struct StatsdInternal *restrict ctx)
{
    ctx->store->ready = PROTO_FALSE;
    ctx->ns->ok = PROTO_FALSE;
    ctx->store->port = ctx->ns->server.port ? ctx->ns->server.port : PROTOCORE_STATSD_PORT;
    if (ctx->ns->tags.global && ctx->ns->tags.global[0])
    {
        (void)str.copy(ctx->store->tags, ctx->ns->tags.global, sizeof(ctx->store->tags));
    }
    else
    {
        ctx->store->tags[0] = '\0';
    }
    if (!ctx->ns->server.addr)
    {
        return;
    }
    Ip.args.text = ctx->ns->server.addr;
    Ip.args.out = &ctx->store->server;
    Ip.parse(Ip.internal);
    ctx->store->ready = Ip.ok;
    ctx->ns->ok = Ip.ok;
}

// Build one metric line into ns->line, and report its length in ns->n.
static void statsd_format(struct StatsdInternal *restrict ctx)
{
    char *out = ctx->ns->line.out;
    const size_t cap = ctx->ns->line.cap;
    const char *name = ctx->ns->metric.name;
    const char *value = ctx->ns->value.text;
    const StatsdType type = ctx->ns->metric.type;
    const char *tags = ctx->ns->tags.metric;
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!out || cap == 0 || !name || !name[0] || !value)
    {
        return;
    }
    if (type != STATSD_COUNTER && type != STATSD_GAUGE && type != STATSD_TIMING && type != STATSD_SET)
    {
        return;
    }

    // `<metricname>:<value>|`, then the type token: a timing writes "ms", the other three write the
    // one character their selector holds.
    const char t = (char)type;
    size_t pos = 0;
    if (!line_append(out, cap, &pos, name, str.len(name, cap)) || !line_append(out, cap, &pos, ":", 1) ||
        !line_append(out, cap, &pos, value, str.len(value, cap)) || !line_append(out, cap, &pos, "|", 1))
    {
        return;
    }
    if (type == STATSD_TIMING)
    {
        if (!line_append(out, cap, &pos, "ms", 2))
        {
            return;
        }
    }
    else if (!line_append(out, cap, &pos, &t, 1))
    {
        return;
    }

    // `|@<rate>` then `|#<tags>`, each written only when it renders to something.
    char rbuf[PROTOCORE_STATSD_RATE_MAX];
    const size_t rn = rate_str(rbuf, ctx->ns->metric.rate);
    if (rn && (!line_append(out, cap, &pos, "|@", 2) || !line_append(out, cap, &pos, rbuf, rn)))
    {
        return;
    }
    if (tags && tags[0] &&
        (!line_append(out, cap, &pos, "|#", 2) || !line_append(out, cap, &pos, tags, str.len(tags, cap))))
    {
        return;
    }

    out[pos] = '\0'; // pos <= cap-1 by construction
    ctx->ns->n = pos;
    ctx->ns->ok = PROTO_TRUE;
}

// Stamp the stored tag list, format into the client's line storage, and send it as one datagram.
static void statsd_emit(struct StatsdInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!ctx->store->ready)
    {
        return;
    }
    ctx->ns->tags.metric = ctx->store->tags[0] ? ctx->store->tags : NULL;
    ctx->ns->line.out = ctx->store->buf;
    ctx->ns->line.cap = sizeof(ctx->store->buf);
    statsd_format(ctx);
    if (ctx->ns->n == 0)
    {
        return;
    }
    // Nothing acknowledges a datagram, so ok reports only that the stack took the octets.
    UdpClient.dst = &ctx->store->server;
    UdpClient.dst_port = ctx->store->port;
    UdpClient.data = (const uint8_t *)ctx->store->buf;
    UdpClient.len = ctx->ns->n;
    UdpClient.sendto(UdpClient.internal);
    ctx->ns->ok = UdpClient.ok;
}

// Add value.i64 to the bucket, annotated with metric.rate.
static void statsd_count(struct StatsdInternal *restrict ctx)
{
    ctx->store->val[i64_str(ctx->store->val, ctx->ns->value.i64)] = '\0';
    ctx->ns->value.text = ctx->store->val;
    ctx->ns->metric.type = STATSD_COUNTER;
    statsd_emit(ctx);
}

// Assign value.i64 to the bucket.
static void statsd_gauge(struct StatsdInternal *restrict ctx)
{
    ctx->store->val[i64_str(ctx->store->val, ctx->ns->value.i64)] = '\0';
    ctx->ns->value.text = ctx->store->val;
    ctx->ns->metric.type = STATSD_GAUGE;
    ctx->ns->metric.rate = 1.0f;
    statsd_emit(ctx);
}

// Adjust the bucket by value.i64, the sign written so the daemon adds rather than assigns.
static void statsd_gauge_delta(struct StatsdInternal *restrict ctx)
{
    ctx->store->val[i64_delta_str(ctx->store->val, ctx->ns->value.i64)] = '\0';
    ctx->ns->value.text = ctx->store->val;
    ctx->ns->metric.type = STATSD_GAUGE;
    ctx->ns->metric.rate = 1.0f;
    statsd_emit(ctx);
}

// Record value.ms milliseconds.
static void statsd_timing(struct StatsdInternal *restrict ctx)
{
    ctx->store->val[u64_str(ctx->store->val, ctx->ns->value.ms)] = '\0';
    ctx->ns->value.text = ctx->store->val;
    ctx->ns->metric.type = STATSD_TIMING;
    ctx->ns->metric.rate = 1.0f;
    statsd_emit(ctx);
}

// Count value.member as one unique occurrence. The member is sent where it lies, not copied.
static void statsd_set(struct StatsdInternal *restrict ctx)
{
    ctx->ns->value.text = ctx->ns->value.member ? ctx->ns->value.member : "";
    ctx->ns->metric.type = STATSD_SET;
    ctx->ns->metric.rate = 1.0f;
    statsd_emit(ctx);
}

// Designated, so a member's position in the struct does not decide what it binds to.
StatsdNs Statsd = {.init = statsd_init,
                   .format = statsd_format,
                   .count = statsd_count,
                   .gauge = statsd_gauge,
                   .gauge_delta = statsd_gauge_delta,
                   .timing = statsd_timing,
                   .set = statsd_set,
                   .internal = &s_statsd};

#endif // PROTOCORE_ENABLE_STATSD
