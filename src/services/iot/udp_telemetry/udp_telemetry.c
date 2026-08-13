// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_telemetry.c
 * @brief InfluxDB line-protocol builder (pure) + UDP cast to a collector.
 *
 * The builder is host-tested; the cast uses Udp.client->sendto on ESP32 and is a
 * no-op on host builds (no transport dependency pulled into the unit test).
 */

#include "services/iot/udp_telemetry/udp_telemetry.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_UDP_TELEMETRY

#include <stdio.h>

// ---------------------------------------------------------------------------
// Line builder (pure)
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/udp/udp.h"
#endif
static void line_append(protocore_line *l, const char *s)
{
    if (l->overflow)
    {
        return;
    }
    size_t n = strnlen(s, l->cap + 1);
    if (l->pos + n >= l->cap) // keep room for the null terminator
    {
        l->overflow = PROTO_TRUE;
        return;
    }
    mem.cpy(l->buf + l->pos, s, n);
    l->pos += n;
    l->buf[l->pos] = '\0';
}

// Separator before a field: a space before the first, a comma after.
static void line_sep(protocore_line *l)
{
    line_append(l, l->have_fields ? "," : " ");
    l->have_fields = PROTO_TRUE;
}

void protocore_line_init(protocore_line *l, char *buf, size_t cap, const char *measurement)
{
    l->buf = buf;
    l->cap = cap;
    l->pos = 0;
    l->overflow = PROTO_FALSE;
    l->have_fields = PROTO_FALSE;
    if (cap)
    {
        buf[0] = '\0';
    }
    line_append(l, measurement ? measurement : "");
}

// Append a string with InfluxDB tag/key escaping: comma, equals and space are
// backslash-escaped (line protocol, "Special characters").
static void line_append_escaped(protocore_line *l, const char *s)
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

void protocore_line_add_tag(protocore_line *l, const char *key, const char *val)
{
    // Tags are part of the series key: they come right after the measurement,
    // comma-separated, BEFORE the space-separated fields. Adding one after a field
    // is a misuse -> fail the line closed.
    if (l->have_fields)
    {
        l->overflow = PROTO_TRUE;
        return;
    }
    line_append(l, ",");
    line_append_escaped(l, key);
    line_append(l, "=");
    line_append_escaped(l, val);
}

void protocore_line_set_timestamp(protocore_line *l, int64_t timestamp)
{
    if (!l->have_fields) // a line needs at least one field before the timestamp
    {
        l->overflow = PROTO_TRUE;
        return;
    }
    char num[24];
    protocore_sb sb_num = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.put(&sb_num, " ");
    Sb.i64(&sb_num, (int64_t)((long long)timestamp));
    if (Sb.finish(&sb_num) == 0)
    {
        num[0] = '\0'; // space-separated trailing timestamp
    }
    line_append(l, num);
}

void protocore_line_add_int(protocore_line *l, const char *field, int64_t v)
{
    char num[24];
    protocore_sb sb_num2 = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.i64(&sb_num2, (int64_t)((long long)v));
    Sb.put(&sb_num2, "i");
    if (Sb.finish(&sb_num2) == 0)
    {
        num[0] = '\0'; // InfluxDB integer suffix
    }
    line_sep(l);
    line_append(l, field);
    line_append(l, "=");
    line_append(l, num);
}

void protocore_line_add_uint(protocore_line *l, const char *field, uint64_t v)
{
    char num[24];
    protocore_sb sb_num3 = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.u64(&sb_num3, (uint64_t)((unsigned long long)v));
    Sb.put(&sb_num3, "u");
    if (Sb.finish(&sb_num3) == 0)
    {
        num[0] = '\0'; // InfluxDB UInteger suffix 'u' (not signed 'i')
    }
    line_sep(l);
    line_append(l, field);
    line_append(l, "=");
    line_append(l, num);
}

void protocore_line_add_float(protocore_line *l, const char *field, float v, uint8_t decimals)
{
    char num[32];
    protocore_sb sb_num4 = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.fixed(&sb_num4, (double)((double)v), (unsigned)((int)decimals));
    if (Sb.finish(&sb_num4) == 0)
    {
        num[0] = '\0';
    }
    line_sep(l);
    line_append(l, field);
    line_append(l, "=");
    line_append(l, num);
}

size_t protocore_line_len(const protocore_line *l)
{
    return l->pos;
}

proto_bool protocore_line_ok(const protocore_line *l)
{
    return !l->overflow && l->have_fields;
}

// ---------------------------------------------------------------------------
// Cast
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK

// All UDP-telemetry cast state, owned by one instance (internal linkage): the collector
// endpoint and the begun flag, grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    protocore_ip collector; // parsed once by begin(); a cast is a build and a queue
    uint16_t port;
    proto_bool begun;
} UdpTelemetryCtx;
static UdpTelemetryCtx s_ut;

void protocore_udp_telemetry_begin(const char *collector_ip, uint16_t port)
{
    s_ut.begun = Ip.parse(collector_ip, &s_ut.collector);
    s_ut.port = port;
}

proto_bool protocore_udp_telemetry_send(const char *data, size_t len)
{
    if (!s_ut.begun || !data)
    {
        return PROTO_FALSE;
    }
    return Udp.client->sendto(&s_ut.collector, s_ut.port, (const uint8_t *)data, len);
}

#else // host build - no network

void protocore_udp_telemetry_begin(const char *collector_ip, uint16_t port)
{
    (void)collector_ip;
    (void)port;
}

proto_bool protocore_udp_telemetry_send(const char *buf, size_t pos)
{
    (void)buf;
    (void)pos;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HAS_NET_STACK

proto_bool protocore_udp_telemetry_cast(const protocore_line *l)
{
    if (!protocore_line_ok(l))
    {
        return PROTO_FALSE;
    }
    return protocore_udp_telemetry_send(l->buf, l->pos);
}

#endif // PROTOCORE_ENABLE_UDP_TELEMETRY
