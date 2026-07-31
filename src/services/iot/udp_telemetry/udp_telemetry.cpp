// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_telemetry.cpp
 * @brief InfluxDB line-protocol builder (pure) + UDP cast to a collector.
 *
 * The builder is host-tested; the cast uses pc_udp_sendto on ESP32 and is a
 * no-op on host builds (no transport dependency pulled into the unit test).
 */

#include "services/iot/udp_telemetry/udp_telemetry.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_UDP_TELEMETRY

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Line builder (pure)
// ---------------------------------------------------------------------------

#if defined(ARDUINO)
#include "network_drivers/transport/udp.h"
#endif
static void line_append(pc_line *l, const char *s)
{
    if (l->overflow)
    {
        return;
    }
    size_t n = strnlen(s, l->cap + 1);
    if (l->pos + n >= l->cap) // keep room for the null terminator
    {
        l->overflow = true;
        return;
    }
    memcpy(l->buf + l->pos, s, n);
    l->pos += n;
    l->buf[l->pos] = '\0';
}

// Separator before a field: a space before the first, a comma after.
static void line_sep(pc_line *l)
{
    line_append(l, l->have_fields ? "," : " ");
    l->have_fields = true;
}

void pc_line_init(pc_line *l, char *buf, size_t cap, const char *measurement)
{
    l->buf = buf;
    l->cap = cap;
    l->pos = 0;
    l->overflow = false;
    l->have_fields = false;
    if (cap)
    {
        buf[0] = '\0';
    }
    line_append(l, measurement ? measurement : "");
}

// Append a string with InfluxDB tag/key escaping: comma, equals and space are
// backslash-escaped (line protocol, "Special characters").
static void line_append_escaped(pc_line *l, const char *s)
{
    if (!s)
    {
        return;
    }
    for (const char *p = s; *p; p++)
    {
        if (*p == ',' || *p == '=' || *p == ' ')
        {
            char esc[3] = {'\\', *p, '\0'};
            line_append(l, esc);
        }
        else
        {
            char one[2] = {*p, '\0'};
            line_append(l, one);
        }
    }
}

void pc_line_add_tag(pc_line *l, const char *key, const char *val)
{
    // Tags are part of the series key: they come right after the measurement,
    // comma-separated, BEFORE the space-separated fields. Adding one after a field
    // is a misuse -> fail the line closed.
    if (l->have_fields)
    {
        l->overflow = true;
        return;
    }
    line_append(l, ",");
    line_append_escaped(l, key);
    line_append(l, "=");
    line_append_escaped(l, val);
}

void pc_line_set_timestamp(pc_line *l, int64_t timestamp)
{
    if (!l->have_fields) // a line needs at least one field before the timestamp
    {
        l->overflow = true;
        return;
    }
    char num[24];
    pc_sb sb_num = {num, sizeof(num), 0, true};
    pc_sb_put(&sb_num, " ");
    pc_sb_i64(&sb_num, (int64_t)((long long)timestamp));
    if (pc_sb_finish(&sb_num) == 0)
    {
        num[0] = '\0'; // space-separated trailing timestamp
    }
    line_append(l, num);
}

void pc_line_add_int(pc_line *l, const char *field, int64_t v)
{
    char num[24];
    pc_sb sb_num2 = {num, sizeof(num), 0, true};
    pc_sb_i64(&sb_num2, (int64_t)((long long)v));
    pc_sb_put(&sb_num2, "i");
    if (pc_sb_finish(&sb_num2) == 0)
    {
        num[0] = '\0'; // InfluxDB integer suffix
    }
    line_sep(l);
    line_append(l, field);
    line_append(l, "=");
    line_append(l, num);
}

void pc_line_add_uint(pc_line *l, const char *field, uint64_t v)
{
    char num[24];
    pc_sb sb_num3 = {num, sizeof(num), 0, true};
    pc_sb_u64(&sb_num3, (uint64_t)((unsigned long long)v));
    pc_sb_put(&sb_num3, "u");
    if (pc_sb_finish(&sb_num3) == 0)
    {
        num[0] = '\0'; // InfluxDB UInteger suffix 'u' (not signed 'i')
    }
    line_sep(l);
    line_append(l, field);
    line_append(l, "=");
    line_append(l, num);
}

void pc_line_add_float(pc_line *l, const char *field, float v, uint8_t decimals)
{
    char num[32];
    pc_sb sb_num4 = {num, sizeof(num), 0, true};
    pc_sb_fixed(&sb_num4, (double)((double)v), (unsigned)((int)decimals));
    if (pc_sb_finish(&sb_num4) == 0)
    {
        num[0] = '\0';
    }
    line_sep(l);
    line_append(l, field);
    line_append(l, "=");
    line_append(l, num);
}

size_t pc_line_len(const pc_line *l)
{
    return l->pos;
}

bool pc_line_ok(const pc_line *l)
{
    return !l->overflow && l->have_fields;
}

// ---------------------------------------------------------------------------
// Cast
// ---------------------------------------------------------------------------

#ifdef ARDUINO

namespace
{
// All UDP-telemetry cast state, owned by one instance (internal linkage): the collector
// endpoint and the begun flag, grouped so it is one named owner, unreachable cross-TU.
struct UdpTelemetryCtx
{
    char ip[16] = {0};
    uint16_t port = 0;
    bool begun = false;
};
UdpTelemetryCtx s_ut;
} // namespace

void pc_udp_telemetry_begin(const char *collector_ip, uint16_t port)
{
    strncpy(s_ut.ip, collector_ip ? collector_ip : "", sizeof(s_ut.ip) - 1);
    s_ut.ip[sizeof(s_ut.ip) - 1] = '\0';
    s_ut.port = port;
    s_ut.begun = true;
}

bool pc_udp_telemetry_send(const char *data, size_t len)
{
    if (!s_ut.begun || !data)
    {
        return false;
    }
    return pc_udp_sendto(s_ut.ip, s_ut.port, (const uint8_t *)data, len);
}

#else // host build - no network

void pc_udp_telemetry_begin(const char *, uint16_t)
{
}

bool pc_udp_telemetry_send(const char *, size_t)
{
    return false;
}

#endif // ARDUINO

bool pc_udp_telemetry_cast(const pc_line *l)
{
    if (!pc_line_ok(l))
    {
        return false;
    }
    return pc_udp_telemetry_send(l->buf, l->pos);
}

#endif // PC_ENABLE_UDP_TELEMETRY
