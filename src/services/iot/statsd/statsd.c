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
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_STATSD

#include "mmgr/protomem/protomem.h"                               // mem.cpy: the spans a line is assembled from
#include "mmgr/protostr/protostr.h"                               // str.copy / str.len: the bounded field moves
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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define STATSD_OFF_CTX 0u
static_assert(STATSD_OFF_CTX + sizeof(struct StatsdStorage) <= PROTOCORE_STATSD_BORROW,
              "PROTOCORE_STATSD_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define STATSD_CTX(w) ((struct StatsdStorage *)(void *)((w) + STATSD_OFF_CTX))

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

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_STATSD_BORROW persistent bytes, or null while the pool was short
} StatsdOwnCtx;
static StatsdOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_statsd_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_STATSD_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
            // A borrow arrives zeroed, and these do not start at zero.
            STATSD_CTX(s_own.span)->port = PROTOCORE_STATSD_PORT;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

// Parse the daemon address and store the port and the tag list every later line carries.
static void statsd_init(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    STATSD_CTX(work)->ready = PROTO_FALSE;
    Statsd.ok = PROTO_FALSE;
    STATSD_CTX(work)->port = Statsd.server.port ? Statsd.server.port : PROTOCORE_STATSD_PORT;
    if (Statsd.tags.global && Statsd.tags.global[0])
    {
        (void)str.copy(STATSD_CTX(work)->tags, Statsd.tags.global, sizeof(STATSD_CTX(work)->tags));
    }
    else
    {
        STATSD_CTX(work)->tags[0] = '\0';
    }
    if (!Statsd.server.addr)
    {
        return;
    }
    Ip.args.text = Statsd.server.addr;
    Ip.args.out = &STATSD_CTX(work)->server;
    Ip.parse(ip_work);
    STATSD_CTX(work)->ready = Ip.ok;
    Statsd.ok = Ip.ok;
}

// Build one metric line into ns->line, and report its length in ns->n.
static void statsd_format(uint8_t *restrict work)
{
    (void)work;
    char *out = Statsd.line.out;
    const size_t cap = Statsd.line.cap;
    const char *name = Statsd.metric.name;
    const char *value = Statsd.value.text;
    const StatsdType type = Statsd.metric.type;
    const char *tags = Statsd.tags.metric;
    Statsd.n = 0;
    Statsd.ok = PROTO_FALSE;
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
    const size_t rn = rate_str(rbuf, Statsd.metric.rate);
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
    Statsd.n = pos;
    Statsd.ok = PROTO_TRUE;
}

// Stamp the stored tag list, format into the client's line storage, and send it as one datagram.
static void statsd_emit(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    Statsd.ok = PROTO_FALSE;
    Statsd.n = 0;
    if (!STATSD_CTX(work)->ready)
    {
        return;
    }
    Statsd.tags.metric = STATSD_CTX(work)->tags[0] ? STATSD_CTX(work)->tags : NULL;
    Statsd.line.out = STATSD_CTX(work)->buf;
    Statsd.line.cap = sizeof(STATSD_CTX(work)->buf);
    statsd_format(work);
    if (Statsd.n == 0)
    {
        return;
    }
    // Nothing acknowledges a datagram, so ok reports only that the stack took the octets.
    UdpClient.dst = &STATSD_CTX(work)->server;
    UdpClient.dst_port = STATSD_CTX(work)->port;
    UdpClient.data = (const uint8_t *)STATSD_CTX(work)->buf;
    UdpClient.len = Statsd.n;
    UdpClient.sendto(protocore_udp_client_span());
    Statsd.ok = UdpClient.ok;
}

// Add value.i64 to the bucket, annotated with metric.rate.
static void statsd_count(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    STATSD_CTX(work)->val[i64_str(STATSD_CTX(work)->val, Statsd.value.i64)] = '\0';
    Statsd.value.text = STATSD_CTX(work)->val;
    Statsd.metric.type = STATSD_COUNTER;
    statsd_emit(work);
}

// Assign value.i64 to the bucket.
static void statsd_gauge(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    STATSD_CTX(work)->val[i64_str(STATSD_CTX(work)->val, Statsd.value.i64)] = '\0';
    Statsd.value.text = STATSD_CTX(work)->val;
    Statsd.metric.type = STATSD_GAUGE;
    Statsd.metric.rate = 1.0f;
    statsd_emit(work);
}

// Adjust the bucket by value.i64, the sign written so the daemon adds rather than assigns.
static void statsd_gauge_delta(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    STATSD_CTX(work)->val[i64_delta_str(STATSD_CTX(work)->val, Statsd.value.i64)] = '\0';
    Statsd.value.text = STATSD_CTX(work)->val;
    Statsd.metric.type = STATSD_GAUGE;
    Statsd.metric.rate = 1.0f;
    statsd_emit(work);
}

// Record value.ms milliseconds.
static void statsd_timing(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    STATSD_CTX(work)->val[u64_str(STATSD_CTX(work)->val, Statsd.value.ms)] = '\0';
    Statsd.value.text = STATSD_CTX(work)->val;
    Statsd.metric.type = STATSD_TIMING;
    Statsd.metric.rate = 1.0f;
    statsd_emit(work);
}

// Count value.member as one unique occurrence. The member is sent where it lies, not copied.
static void statsd_set(uint8_t *restrict work)
{
    Statsd.value.text = Statsd.value.member ? Statsd.value.member : "";
    Statsd.metric.type = STATSD_SET;
    Statsd.metric.rate = 1.0f;
    statsd_emit(work);
}

// Designated, so a member's position in the struct does not decide what it binds to.
StatsdNs Statsd = {.init = statsd_init,
                   .format = statsd_format,
                   .count = statsd_count,
                   .gauge = statsd_gauge,
                   .gauge_delta = statsd_gauge_delta,
                   .timing = statsd_timing,
                   .set = statsd_set};

#endif // PROTOCORE_ENABLE_STATSD
