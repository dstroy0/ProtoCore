// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file statsd.cpp
 * @brief StatsD metrics client - implementation. See statsd.h.
 *
 * The value and sample-rate are rendered by hand (no printf %lld/%f, which need extra
 * newlib support on some targets), then pc_statsd_format assembles the line and the transport
 * UDP service sends it.
 */

#include "services/iot/statsd/statsd.h"
#include "protocore_config.h"

#if PC_ENABLE_STATSD

#include "network_drivers/transport/udp.h"
#include <string.h>

// Unsigned -> decimal (not NUL-terminated); returns the digit count.
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

// Signed -> decimal (handles INT64_MIN via -(v+1)+1); returns length.
static size_t i64_str(char *b, int64_t v)
{
    if (v < 0)
    {
        b[0] = '-';
        return 1 + u64_str(b + 1, (uint64_t)(-(v + 1)) + 1);
    }
    return u64_str(b, (uint64_t)v);
}

// Signed -> "+N" / "-N" for a StatsD gauge delta.
static size_t i64_delta_str(char *b, int64_t v)
{
    if (v >= 0)
    {
        b[0] = '+';
        return 1 + u64_str(b + 1, (uint64_t)v);
    }
    return i64_str(b, v); // already carries the '-'
}

// Sample rate in (0,1) -> "0.xxx" (<= 3 decimals, trailing zeros trimmed). >= 1 -> no text.
static size_t rate_str(char *b, float r)
{
    if (r >= 1.0f || r <= 0.0f)
    {
        return 0;
    }
    int m = (int)(r * 1000.0f + 0.5f); // thousandths
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
        len--; // trim trailing zeros ("0.100" -> "0.1")
    }
    return len;
}

// Bounded append into out[*pos]; false (and leaves *pos) if it would not fit with room for NUL.
static proto_bool app(char *out, size_t cap, size_t *pos, const char *s, size_t n)
{
    if (*pos + n >= cap)
    {
        return PROTO_FALSE;
    }
    memcpy(out + *pos, s, n);
    *pos += n;
    return PROTO_TRUE;
}

size_t pc_statsd_format(char *out, size_t cap, const char *name, const char *value, StatsdType type, float rate,
                        const char *tags)
{
    if (!out || cap == 0 || !name || !name[0] || !value)
    {
        return 0;
    }
    if (type != STATSD_COUNTER && type != STATSD_GAUGE && type != STATSD_TIMING && type != STATSD_SET)
    {
        return 0;
    }

    size_t pos = 0;
    char t = (char)type;
    if (!app(out, cap, &pos, name, strnlen(name, cap)) || !app(out, cap, &pos, ":", 1) ||
        !app(out, cap, &pos, value, strnlen(value, cap)) || !app(out, cap, &pos, "|", 1))
    {
        return 0;
    }
    if (type == STATSD_TIMING)
    {
        if (!app(out, cap, &pos, "ms", 2))
        {
            return 0;
        }
    }
    else if (!app(out, cap, &pos, &t, 1))
    {
        return 0;
    }

    char rbuf[8];
    size_t rn = rate_str(rbuf, rate);
    if (rn && (!app(out, cap, &pos, "|@", 2) || !app(out, cap, &pos, rbuf, rn)))
    {
        return 0;
    }
    if (tags && tags[0] && (!app(out, cap, &pos, "|#", 2) || !app(out, cap, &pos, tags, strnlen(tags, cap))))
    {
        return 0;
    }

    out[pos] = '\0';
    return pos;
}

// ---------------------------------------------------------------------------
// Emit helpers: render the value, then send via the transport UDP service. Host builds
// send through pc_udp_sendto's stub + capture seam, so these are host-testable too.
// ---------------------------------------------------------------------------

// All StatsD client state, owned by one instance (internal linkage): destination host/port,
// global tags, and the ready flag, grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    char host[64] = {};
    uint16_t port = PC_STATSD_PORT;
    char tags[96] = {};
    proto_bool ready = PROTO_FALSE;
} StatsdCtx;
static StatsdCtx s_statsd;

static void emit(const StatsdCtx &c, const char *name, const char *value, StatsdType type, float rate)
{
    if (!c.ready)
    {
        return;
    }
    char line[PC_STATSD_LINE_MAX];
    size_t n = pc_statsd_format(line, sizeof(line), name, value, type, rate, c.tags[0] ? c.tags : NULL);
    if (n)
    {
        pc_udp_sendto(c.host, c.port, (const uint8_t *)line, n);
    }
}

void pc_statsd_begin(const char *host, uint16_t port, const char *global_tags)
{
    if (!host)
    {
        s_statsd.ready = PROTO_FALSE;
        return;
    }
    strncpy(s_statsd.host, host, sizeof(s_statsd.host) - 1);
    s_statsd.host[sizeof(s_statsd.host) - 1] = '\0';
    s_statsd.port = port ? port : PC_STATSD_PORT;
    if (global_tags)
    {
        strncpy(s_statsd.tags, global_tags, sizeof(s_statsd.tags) - 1);
        s_statsd.tags[sizeof(s_statsd.tags) - 1] = '\0';
    }
    else
    {
        s_statsd.tags[0] = '\0';
    }
    s_statsd.ready = PROTO_TRUE;
}

void pc_statsd_count(const char *name, int64_t delta)
{
    char v[24];
    v[i64_str(v, delta)] = '\0';
    emit(s_statsd, name, v, STATSD_COUNTER, 1.0f);
}

void pc_statsd_count_sampled(const char *name, int64_t delta, float rate)
{
    char v[24];
    v[i64_str(v, delta)] = '\0';
    emit(s_statsd, name, v, STATSD_COUNTER, rate);
}

void pc_statsd_gauge(const char *name, int64_t value)
{
    char v[24];
    v[i64_str(v, value)] = '\0';
    emit(s_statsd, name, v, STATSD_GAUGE, 1.0f);
}

void pc_statsd_gauge_delta(const char *name, int64_t delta)
{
    char v[24];
    v[i64_delta_str(v, delta)] = '\0';
    emit(s_statsd, name, v, STATSD_GAUGE, 1.0f);
}

void pc_statsd_timing(const char *name, uint32_t ms)
{
    char v[16];
    v[u64_str(v, ms)] = '\0';
    emit(s_statsd, name, v, STATSD_TIMING, 1.0f);
}

void pc_statsd_set(const char *name, const char *member)
{
    emit(s_statsd, name, member ? member : "", STATSD_SET, 1.0f);
}

#endif // PC_ENABLE_STATSD
