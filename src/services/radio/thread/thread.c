// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file thread.c
 * @brief Thread spinel / HDLC-lite framing codec - implementation.
 *
 * HDLC-lite: [payload | FCS(lo,hi)] byte-stuffed and Flag-terminated. The FCS is CRC-16/X-25
 * (poly 0x1021 reflected = 0x8408, init 0xFFFF, reflected, final XOR 0xFFFF); the reserved
 * bytes 0x7E / 0x7D / 0x11 / 0x13 are escaped as 0x7D, (byte XOR 0x20).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

#if PROTOCORE_ENABLE_THREAD

#include "services/radio/thread/thread.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_X25

PROTOCORE_BEGIN_DECLS

static proto_bool is_reserved(uint8_t b)
{
    return b == 0x7E || b == 0x7D || b == 0x11 || b == 0x13;
}

// Append a byte with HDLC stuffing; return false if it would overflow cap.
static proto_bool put_stuffed(uint8_t *out, uint16_t *p, uint16_t cap, uint8_t b)
{
    if (is_reserved(b))
    {
        if (*p + 2 > cap)
        {
            return PROTO_FALSE;
        }
        out[(*p)++] = HDLC_ESCAPE;
        out[(*p)++] = (uint8_t)(b ^ 0x20);
    }
    else
    {
        if (*p + 1 > cap)
        {
            return PROTO_FALSE;
        }
        out[(*p)++] = b;
    }
    return PROTO_TRUE;
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void thread_spinel_fcs(uint8_t *restrict work);
static void thread_spinel_get_u16(uint8_t *restrict work);
static void thread_spinel_get_u32(uint8_t *restrict work);
static void thread_spinel_pack_uint(uint8_t *restrict work);
static void thread_spinel_prop_lookup(uint8_t *restrict work);
static void thread_spinel_put_data(uint8_t *restrict work);
static void thread_spinel_put_u16(uint8_t *restrict work);
static void thread_spinel_put_u32(uint8_t *restrict work);
static void thread_spinel_put_u8(uint8_t *restrict work);
static void thread_spinel_unpack_uint(uint8_t *restrict work);

static void thread_spinel_pack_uint(uint8_t *restrict work)
{
    (void)work;
    uint32_t value = Thread.spinel_pack_uint_args.value;
    uint8_t *out = Thread.spinel_pack_uint_args.out;
    uint8_t cap = Thread.spinel_pack_uint_args.cap;

    if (!out)
    {
        Thread.u8 = 0;
        return;
    }
    uint8_t n = 0;
    do
    {
        if (n >= cap)
        {
            Thread.u8 = 0;
            return;
        }
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value)
        {
            byte |= 0x80; // more bytes follow
        }
        out[n++] = byte;
    } while (value);
    Thread.u8 = n;
}

static void thread_spinel_unpack_uint(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *raw = Thread.spinel_unpack_uint_args.raw;
    uint8_t len = Thread.spinel_unpack_uint_args.len;
    uint32_t *value = Thread.spinel_unpack_uint_args.value;

    if (!raw)
    {
        Thread.n = 0;
        return;
    }
    uint32_t v = 0;
    uint8_t shift = 0;
    for (uint8_t n = 0; n < len; n++)
    {
        uint8_t b = raw[n];
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
        {
            if (value)
            {
                *value = v;
            }
            Thread.n = n + 1;
            return;
        }
        shift += 7;
        if (shift >= 32)
        {
            Thread.n = -1; // does not fit a uint32
            return;
        }
    }
    Thread.n = 0; // truncated - need more bytes
}

static void thread_spinel_command_build(uint8_t *restrict work)
{
    uint8_t header = Thread.spinel_command_build_args.header;
    uint32_t cmd = Thread.spinel_command_build_args.cmd;
    uint32_t prop = Thread.spinel_command_build_args.prop;
    const uint8_t *value = Thread.spinel_command_build_args.value;
    uint16_t value_len = Thread.spinel_command_build_args.value_len;
    uint8_t *out = Thread.spinel_command_build_args.out;
    uint16_t cap = Thread.spinel_command_build_args.cap;

    if (!out || cap < 1 || (value == NULL && value_len > 0))
    {
        Thread.value = 0;
        return;
    }
    uint16_t p = 0;
    out[p++] = header;
    Thread.spinel_pack_uint_args.value = cmd;
    Thread.spinel_pack_uint_args.out = out + p;
    Thread.spinel_pack_uint_args.cap = (uint8_t)(cap - p);
    thread_spinel_pack_uint(work);
    uint8_t n = Thread.u8;
    if (n == 0)
    {
        Thread.value = 0;
        return;
    }
    p += n;
    Thread.spinel_pack_uint_args.value = prop;
    Thread.spinel_pack_uint_args.out = out + p;
    Thread.spinel_pack_uint_args.cap = (uint8_t)(cap > p ? cap - p : 0);
    thread_spinel_pack_uint(work);
    n = Thread.u8;
    if (n == 0)
    {
        Thread.value = 0;
        return;
    }
    p += n;
    if ((uint32_t)p + value_len > cap)
    {
        Thread.value = 0;
        return;
    }
    for (uint16_t i = 0; i < value_len; i++)
    {
        out[p + i] = value[i];
    }
    Thread.value = (uint16_t)(p + value_len);
}

static void thread_spinel_command_parse(uint8_t *restrict work)
{
    const uint8_t *payload = Thread.spinel_command_parse_args.payload;
    uint16_t len = Thread.spinel_command_parse_args.len;
    uint8_t *header = Thread.spinel_command_parse_args.header;
    uint32_t *cmd = Thread.spinel_command_parse_args.cmd;
    uint32_t *prop = Thread.spinel_command_parse_args.prop;
    const uint8_t **value = Thread.spinel_command_parse_args.value;
    uint16_t *value_len = Thread.spinel_command_parse_args.value_len;

    if (!payload || len < 1)
    {
        Thread.n = -1;
        return;
    }
    uint16_t p = 0;
    uint8_t h = payload[p++];
    uint32_t c = 0;
    uint32_t pr = 0;
    Thread.spinel_unpack_uint_args.raw = payload + p;
    Thread.spinel_unpack_uint_args.len = (uint8_t)((len - p) > 255 ? 255 : (len - p));
    Thread.spinel_unpack_uint_args.value = &c;
    thread_spinel_unpack_uint(work);
    int n = Thread.n;
    if (n <= 0)
    {
        Thread.n = -1;
        return;
    }
    p += (uint16_t)n;
    Thread.spinel_unpack_uint_args.raw = payload + p;
    Thread.spinel_unpack_uint_args.len = (uint8_t)((len - p) > 255 ? 255 : (len - p));
    Thread.spinel_unpack_uint_args.value = &pr;
    thread_spinel_unpack_uint(work);
    n = Thread.n;
    if (n <= 0)
    {
        Thread.n = -1;
        return;
    }
    p += (uint16_t)n;
    if (header)
    {
        *header = h;
    }
    if (cmd)
    {
        *cmd = c;
    }
    if (prop)
    {
        *prop = pr;
    }
    if (value)
    {
        *value = payload + p;
    }
    if (value_len)
    {
        *value_len = (uint16_t)(len - p);
    }
    Thread.n = (int)p;
}

// --- Spinel value semantics -------------------------------------------------------------

static void thread_spinel_reader_init(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_reader_init_args.r;
    const uint8_t *value = Thread.spinel_reader_init_args.value;
    uint16_t len = Thread.spinel_reader_init_args.len;

    if (!r)
    {
        return;
    }
    r->buf = value;
    r->len = value ? len : 0;
    r->off = 0;
    r->err = (value == NULL && len > 0);
}

// Reserve n bytes at the cursor; return the read pointer or nullptr (latching err) if short.
static const uint8_t *take(SpinelReader *r, uint16_t n)
{
    if (!r || r->err || (uint32_t)r->off + n > r->len)
    {
        if (r)
        {
            r->err = PROTO_TRUE;
        }
        return NULL;
    }
    const uint8_t *at = r->buf + r->off;
    r->off = (uint16_t)(r->off + n);
    return at;
}

static void thread_spinel_get_bool(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_bool_args.r;
    proto_bool *out = Thread.spinel_get_bool_args.out;

    const uint8_t *b = take(r, 1);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = (*b != 0);
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_u8(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_u8_args.r;
    uint8_t *out = Thread.spinel_get_u8_args.out;

    const uint8_t *b = take(r, 1);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = b[0];
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_i8(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_i8_args.r;
    int8_t *out = Thread.spinel_get_i8_args.out;

    const uint8_t *b = take(r, 1);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = (int8_t)b[0];
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_u16(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_u16_args.r;
    uint16_t *out = Thread.spinel_get_u16_args.out;

    const uint8_t *b = take(r, 2);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = (uint16_t)(b[0] | (b[1] << 8));
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_i16(uint8_t *restrict work)
{
    SpinelReader *r = Thread.spinel_get_i16_args.r;
    int16_t *out = Thread.spinel_get_i16_args.out;

    uint16_t v = 0;
    Thread.spinel_get_u16_args.r = r;
    Thread.spinel_get_u16_args.out = &v;
    thread_spinel_get_u16(work);
    if (!Thread.ok)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = (int16_t)v;
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_u32(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_u32_args.r;
    uint32_t *out = Thread.spinel_get_u32_args.out;

    const uint8_t *b = take(r, 4);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_i32(uint8_t *restrict work)
{
    SpinelReader *r = Thread.spinel_get_i32_args.r;
    int32_t *out = Thread.spinel_get_i32_args.out;

    uint32_t v = 0;
    Thread.spinel_get_u32_args.r = r;
    Thread.spinel_get_u32_args.out = &v;
    thread_spinel_get_u32(work);
    if (!Thread.ok)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = (int32_t)v;
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_uint(uint8_t *restrict work)
{
    SpinelReader *r = Thread.spinel_get_uint_args.r;
    uint32_t *out = Thread.spinel_get_uint_args.out;

    if (!r || r->err)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint32_t v = 0;
    Thread.spinel_unpack_uint_args.raw = r->buf + r->off;
    Thread.spinel_unpack_uint_args.len = (uint8_t)((r->len - r->off) > 255 ? 255 : (r->len - r->off));
    Thread.spinel_unpack_uint_args.value = &v;
    thread_spinel_unpack_uint(work);
    int n = Thread.n;
    if (n <= 0)
    {
        r->err = PROTO_TRUE;
        Thread.ok = PROTO_FALSE;
        return;
    }
    r->off = (uint16_t)(r->off + n);
    if (out)
    {
        *out = v;
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_eui64(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_eui64_args.r;
    const uint8_t **out8 = Thread.spinel_get_eui64_args.out8;

    const uint8_t *b = take(r, 8);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out8)
    {
        *out8 = b;
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_ipv6(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_ipv6_args.r;
    const uint8_t **out16 = Thread.spinel_get_ipv6_args.out16;

    const uint8_t *b = take(r, 16);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out16)
    {
        *out16 = b;
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_utf8(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_utf8_args.r;
    const char **out = Thread.spinel_get_utf8_args.out;
    uint16_t *out_len = Thread.spinel_get_utf8_args.out_len;

    if (!r || r->err)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint16_t i = r->off;
    while (i < r->len && r->buf[i] != 0)
    {
        i++;
    }
    if (i >= r->len) // no NUL terminator in the value
    {
        r->err = PROTO_TRUE;
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = (const char *)(r->buf + r->off);
    }
    if (out_len)
    {
        *out_len = (uint16_t)(i - r->off);
    }
    r->off = (uint16_t)(i + 1); // consume the string and its NUL
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_data(uint8_t *restrict work)
{
    (void)work;
    SpinelReader *r = Thread.spinel_get_data_args.r;
    const uint8_t **out = Thread.spinel_get_data_args.out;
    uint16_t *out_len = Thread.spinel_get_data_args.out_len;

    if (!r || r->err)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = r->buf + r->off;
    }
    if (out_len)
    {
        *out_len = (uint16_t)(r->len - r->off);
    }
    r->off = r->len;
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_get_data_wlen(uint8_t *restrict work)
{
    SpinelReader *r = Thread.spinel_get_data_wlen_args.r;
    const uint8_t **out = Thread.spinel_get_data_wlen_args.out;
    uint16_t *out_len = Thread.spinel_get_data_wlen_args.out_len;

    uint16_t n = 0;
    Thread.spinel_get_u16_args.r = r;
    Thread.spinel_get_u16_args.out = &n;
    thread_spinel_get_u16(work);
    if (!Thread.ok)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = take(r, n);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = b;
    }
    if (out_len)
    {
        *out_len = n;
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_reader_ok(uint8_t *restrict work)
{
    (void)work;
    const SpinelReader *r = Thread.spinel_reader_ok_args.r;

    Thread.ok = r && !r->err;
}

static void thread_spinel_writer_init(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_writer_init_args.w;
    uint8_t *out = Thread.spinel_writer_init_args.out;
    uint16_t cap = Thread.spinel_writer_init_args.cap;

    if (!w)
    {
        return;
    }
    w->buf = out;
    w->cap = out ? cap : 0;
    w->off = 0;
    w->err = (out == NULL && cap > 0);
}

// Reserve n bytes for writing; return the write pointer or nullptr (latching err) if no room.
static uint8_t *room(SpinelWriter *w, uint16_t n)
{
    if (!w || w->err || (uint32_t)w->off + n > w->cap)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        return NULL;
    }
    uint8_t *at = w->buf + w->off;
    w->off = (uint16_t)(w->off + n);
    return at;
}

static void thread_spinel_put_bool(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_bool_args.w;
    proto_bool v = Thread.spinel_put_bool_args.v;

    uint8_t *b = room(w, 1);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    b[0] = v ? 1 : 0;
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_u8(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_u8_args.w;
    uint8_t v = Thread.spinel_put_u8_args.v;

    uint8_t *b = room(w, 1);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    b[0] = v;
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_i8(uint8_t *restrict work)
{
    SpinelWriter *w = Thread.spinel_put_i8_args.w;
    int8_t v = Thread.spinel_put_i8_args.v;

    Thread.spinel_put_u8_args.w = w;
    Thread.spinel_put_u8_args.v = (uint8_t)v;
    thread_spinel_put_u8(work);
}

static void thread_spinel_put_u16(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_u16_args.w;
    uint16_t v = Thread.spinel_put_u16_args.v;

    uint8_t *b = room(w, 2);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)(v >> 8);
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_i16(uint8_t *restrict work)
{
    SpinelWriter *w = Thread.spinel_put_i16_args.w;
    int16_t v = Thread.spinel_put_i16_args.v;

    Thread.spinel_put_u16_args.w = w;
    Thread.spinel_put_u16_args.v = (uint16_t)v;
    thread_spinel_put_u16(work);
}

static void thread_spinel_put_u32(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_u32_args.w;
    uint32_t v = Thread.spinel_put_u32_args.v;

    uint8_t *b = room(w, 4);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_i32(uint8_t *restrict work)
{
    SpinelWriter *w = Thread.spinel_put_i32_args.w;
    int32_t v = Thread.spinel_put_i32_args.v;

    Thread.spinel_put_u32_args.w = w;
    Thread.spinel_put_u32_args.v = (uint32_t)v;
    thread_spinel_put_u32(work);
}

static void thread_spinel_put_uint(uint8_t *restrict work)
{
    SpinelWriter *w = Thread.spinel_put_uint_args.w;
    uint32_t v = Thread.spinel_put_uint_args.v;

    if (!w || w->err)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint8_t tmp[5];
    Thread.spinel_pack_uint_args.value = v;
    Thread.spinel_pack_uint_args.out = tmp;
    Thread.spinel_pack_uint_args.cap = sizeof(tmp);
    thread_spinel_pack_uint(work);
    uint8_t n = Thread.u8;
    if (n == 0)
    {
        w->err = PROTO_TRUE;
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint8_t *b = room(w, n);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        b[i] = tmp[i];
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_eui64(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_eui64_args.w;
    const uint8_t *v8 = Thread.spinel_put_eui64_args.v8;

    if (!v8)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint8_t *b = room(w, 8);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < 8; i++)
    {
        b[i] = v8[i];
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_ipv6(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_ipv6_args.w;
    const uint8_t *v16 = Thread.spinel_put_ipv6_args.v16;

    if (!v16)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint8_t *b = room(w, 16);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < 16; i++)
    {
        b[i] = v16[i];
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_utf8(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_utf8_args.w;
    const char *s = Thread.spinel_put_utf8_args.s;

    if (!s)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint16_t n = 0;
    while (s[n] != 0)
    {
        n++;
    }
    uint8_t *b = room(w, (uint16_t)(n + 1)); // include the NUL
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    for (uint16_t i = 0; i <= n; i++)
    {
        b[i] = (uint8_t)s[i];
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_data(uint8_t *restrict work)
{
    (void)work;
    SpinelWriter *w = Thread.spinel_put_data_args.w;
    const uint8_t *d = Thread.spinel_put_data_args.d;
    uint16_t n = Thread.spinel_put_data_args.n;

    if (d == NULL && n > 0)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        Thread.ok = PROTO_FALSE;
        return;
    }
    uint8_t *b = room(w, n);
    if (!b)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        b[i] = d[i];
    }
    Thread.ok = PROTO_TRUE;
}

static void thread_spinel_put_data_wlen(uint8_t *restrict work)
{
    SpinelWriter *w = Thread.spinel_put_data_wlen_args.w;
    const uint8_t *d = Thread.spinel_put_data_wlen_args.d;
    uint16_t n = Thread.spinel_put_data_wlen_args.n;

    Thread.spinel_put_u16_args.w = w;
    Thread.spinel_put_u16_args.v = n;
    thread_spinel_put_u16(work);
    if (!Thread.ok)
    {
        Thread.ok = PROTO_FALSE;
        return;
    }
    Thread.spinel_put_data_args.w = w;
    Thread.spinel_put_data_args.d = d;
    Thread.spinel_put_data_args.n = n;
    thread_spinel_put_data(work);
}

static void thread_spinel_writer_len(uint8_t *restrict work)
{
    (void)work;
    const SpinelWriter *w = Thread.spinel_writer_len_args.w;

    if (!w || w->err)
    {
        Thread.value = 0;
        return;
    }
    Thread.value = w->off;
}

// --- Property registry ------------------------------------------------------------------

static const SpinelPropInfo k_props[] = {
    {SPINEL_PROP_LAST_STATUS, "LAST_STATUS", 'i'},
    {SPINEL_PROP_PROTOCOL_VERSION, "PROTOCOL_VERSION", 'i'},
    {SPINEL_PROP_NCP_VERSION, "NCP_VERSION", 'U'},
    {SPINEL_PROP_INTERFACE_TYPE, "INTERFACE_TYPE", 'i'},
    {SPINEL_PROP_VENDOR_ID, "VENDOR_ID", 'i'},
    {SPINEL_PROP_CAPS, "CAPS", 'i'},
    {SPINEL_PROP_INTERFACE_COUNT, "INTERFACE_COUNT", 'C'},
    {SPINEL_PROP_HWADDR, "HWADDR", 'E'},
    {SPINEL_PROP_LOCK, "LOCK", 'b'},
    {SPINEL_PROP_PHY_ENABLED, "PHY_ENABLED", 'b'},
    {SPINEL_PROP_PHY_CHAN, "PHY_CHAN", 'C'},
    {SPINEL_PROP_PHY_CHAN_SUPPORTED, "PHY_CHAN_SUPPORTED", 'C'},
    {SPINEL_PROP_PHY_FREQ, "PHY_FREQ", 'L'},
    {SPINEL_PROP_PHY_TX_POWER, "PHY_TX_POWER", 'c'},
    {SPINEL_PROP_PHY_RSSI, "PHY_RSSI", 'c'},
    {SPINEL_PROP_MAC_SCAN_STATE, "MAC_SCAN_STATE", 'C'},
    {SPINEL_PROP_MAC_SCAN_MASK, "MAC_SCAN_MASK", 'C'},
    {SPINEL_PROP_MAC_SCAN_PERIOD, "MAC_SCAN_PERIOD", 'S'},
    {SPINEL_PROP_MAC_15_4_LADDR, "MAC_15_4_LADDR", 'E'},
    {SPINEL_PROP_MAC_15_4_SADDR, "MAC_15_4_SADDR", 'S'},
    {SPINEL_PROP_MAC_15_4_PANID, "MAC_15_4_PANID", 'S'},
    {SPINEL_PROP_NET_SAVED, "NET_SAVED", 'b'},
    {SPINEL_PROP_NET_IF_UP, "NET_IF_UP", 'b'},
    {SPINEL_PROP_NET_STACK_UP, "NET_STACK_UP", 'b'},
    {SPINEL_PROP_NET_ROLE, "NET_ROLE", 'C'},
    {SPINEL_PROP_NET_NETWORK_NAME, "NET_NETWORK_NAME", 'U'},
    {SPINEL_PROP_NET_XPANID, "NET_XPANID", 'D'},
    {SPINEL_PROP_NET_NETWORK_KEY, "NET_NETWORK_KEY", 'D'},
    {SPINEL_PROP_IPV6_LL_ADDR, "IPV6_LL_ADDR", '6'},
    {SPINEL_PROP_IPV6_ML_ADDR, "IPV6_ML_ADDR", '6'},
    {SPINEL_PROP_STREAM_DEBUG, "STREAM_DEBUG", 'U'},
    {SPINEL_PROP_STREAM_RAW, "STREAM_RAW", 'd'},
    {SPINEL_PROP_STREAM_NET, "STREAM_NET", 'd'},
};

typedef struct
{
    uint32_t code;
    const char *name;
} StatusName;
static const StatusName k_status[] = {
    {SPINEL_STATUS_OK, "OK"},
    {SPINEL_STATUS_FAILURE, "FAILURE"},
    {SPINEL_STATUS_UNIMPLEMENTED, "UNIMPLEMENTED"},
    {SPINEL_STATUS_INVALID_ARGUMENT, "INVALID_ARGUMENT"},
    {SPINEL_STATUS_INVALID_STATE, "INVALID_STATE"},
    {SPINEL_STATUS_INVALID_COMMAND, "INVALID_COMMAND"},
    {SPINEL_STATUS_INVALID_INTERFACE, "INVALID_INTERFACE"},
    {SPINEL_STATUS_INTERNAL_ERROR, "INTERNAL_ERROR"},
    {SPINEL_STATUS_SECURITY_ERROR, "SECURITY_ERROR"},
    {SPINEL_STATUS_PARSE_ERROR, "PARSE_ERROR"},
    {SPINEL_STATUS_IN_PROGRESS, "IN_PROGRESS"},
    {SPINEL_STATUS_NOMEM, "NOMEM"},
    {SPINEL_STATUS_BUSY, "BUSY"},
    {SPINEL_STATUS_PROP_NOT_FOUND, "PROP_NOT_FOUND"},
    {SPINEL_STATUS_DROPPED, "DROPPED"},
    {SPINEL_STATUS_EMPTY, "EMPTY"},
};

static void thread_spinel_prop_lookup(uint8_t *restrict work)
{
    (void)work;
    uint32_t id = Thread.spinel_prop_lookup_args.id;

    for (uint16_t i = 0; i < sizeof(k_props) / sizeof(k_props[0]); i++)
    {
        if (k_props[i].id == id)
        {
            Thread.ptr = &k_props[i];
            return;
        }
    }
    Thread.ptr = NULL;
}

static void thread_spinel_prop_name(uint8_t *restrict work)
{
    uint32_t id = Thread.spinel_prop_name_args.id;

    Thread.spinel_prop_lookup_args.id = id;
    thread_spinel_prop_lookup(work);
    const SpinelPropInfo *e = Thread.ptr;
    Thread.text = e ? e->name : "UNKNOWN";
}

static void thread_spinel_status_name(uint8_t *restrict work)
{
    (void)work;
    uint32_t status = Thread.spinel_status_name_args.status;

    for (uint16_t i = 0; i < sizeof(k_status) / sizeof(k_status[0]); i++)
    {
        if (k_status[i].code == status)
        {
            Thread.text = k_status[i].name;
            return;
        }
    }
    if (status >= SPINEL_STATUS_RESET_POWER_ON && status < SPINEL_STATUS_RESET_END)
    {
        Thread.text = "RESET";
        return;
    }
    Thread.text = "UNKNOWN";
}

static void thread_spinel_fcs(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Thread.spinel_fcs_args.buf;
    uint16_t len = Thread.spinel_fcs_args.len;

    // The HDLC-lite FCS is CRC-16/X-25 (reflected poly 0x8408, init 0xFFFF, xorout 0xFFFF).
    Crc.args.params = &PROTOCORE_CRC16_X25;
    Crc.args.data = buf;
    Crc.args.len = len;
    Crc.compute(crc_work);
    Thread.value = (uint16_t)Crc.value;
}

static void thread_spinel_frame_encode(uint8_t *restrict work)
{
    const uint8_t *payload = Thread.spinel_frame_encode_args.payload;
    uint16_t len = Thread.spinel_frame_encode_args.len;
    uint8_t *out = Thread.spinel_frame_encode_args.out;
    uint16_t cap = Thread.spinel_frame_encode_args.cap;

    if (!out || len > PROTOCORE_THREAD_MAX_DATA || (payload == NULL && len > 0))
    {
        Thread.value = 0;
        return;
    }
    Thread.spinel_fcs_args.buf = payload;
    Thread.spinel_fcs_args.len = len;
    thread_spinel_fcs(work);
    uint16_t fcs = Thread.value;
    uint16_t p = 0;
    for (uint16_t i = 0; i < len; i++)
    {
        if (!put_stuffed(out, &p, cap, payload[i]))
        {
            Thread.value = 0;
            return;
        }
    }
    if (!put_stuffed(out, &p, cap, (uint8_t)(fcs & 0xFF)) || // FCS low byte first
        !put_stuffed(out, &p, cap, (uint8_t)(fcs >> 8)))
    {
        Thread.value = 0;
        return;
    }
    if (p + 1 > cap)
    {
        Thread.value = 0;
        return;
    }
    out[p++] = HDLC_FLAG;
    Thread.value = p;
}

static void thread_spinel_frame_decode(uint8_t *restrict work)
{
    const uint8_t *raw = Thread.spinel_frame_decode_args.raw;
    uint16_t len = Thread.spinel_frame_decode_args.len;
    uint8_t *payload = Thread.spinel_frame_decode_args.payload;
    uint16_t pay_cap = Thread.spinel_frame_decode_args.pay_cap;
    uint16_t *pay_len = Thread.spinel_frame_decode_args.pay_len;

    if (!raw)
    {
        Thread.n = 0;
        return;
    }
    uint16_t flag = 0;
    while (flag < len && raw[flag] != HDLC_FLAG)
    {
        flag++;
    }
    if (flag >= len)
    {
        Thread.n = 0; // no complete frame yet
        return;
    }

    // Remove the byte-stuffing from raw[0, flag) into a scratch: payload + FCS(2).
    uint8_t un[PROTOCORE_THREAD_MAX_DATA + 2];
    uint16_t n = 0;
    for (uint16_t i = 0; i < flag; i++)
    {
        uint8_t b = raw[i];
        if (b == HDLC_ESCAPE)
        {
            if (++i >= flag)
            {
                Thread.n = -1; // dangling escape
                return;
            }
            b = (uint8_t)(raw[i] ^ 0x20);
        }
        if (n >= sizeof(un))
        {
            Thread.n = -1;
            return;
        }
        un[n++] = b;
    }
    if (n < 2)
    {
        Thread.n = -1; // need at least the FCS
        return;
    }
    uint16_t plen = (uint16_t)(n - 2);
    Thread.spinel_fcs_args.buf = un;
    Thread.spinel_fcs_args.len = plen;
    thread_spinel_fcs(work);
    uint16_t fcs = Thread.value;
    if ((uint16_t)(un[plen] | (un[plen + 1] << 8)) != fcs)
    {
        Thread.n = -1; // FCS mismatch (transmitted low byte first)
        return;
    }
    if (plen > pay_cap)
    {
        Thread.n = -1;
        return;
    }
    for (uint16_t i = 0; i < plen; i++)
    {
        payload[i] = un[i];
    }
    if (pay_len)
    {
        *pay_len = plen;
    }
    Thread.n = (int)(flag + 1);
}

ThreadNs Thread = {.spinel_fcs = thread_spinel_fcs,
                   .spinel_pack_uint = thread_spinel_pack_uint,
                   .spinel_unpack_uint = thread_spinel_unpack_uint,
                   .spinel_command_build = thread_spinel_command_build,
                   .spinel_command_parse = thread_spinel_command_parse,
                   .spinel_reader_init = thread_spinel_reader_init,
                   .spinel_get_bool = thread_spinel_get_bool,
                   .spinel_get_u8 = thread_spinel_get_u8,
                   .spinel_get_i8 = thread_spinel_get_i8,
                   .spinel_get_u16 = thread_spinel_get_u16,
                   .spinel_get_i16 = thread_spinel_get_i16,
                   .spinel_get_u32 = thread_spinel_get_u32,
                   .spinel_get_i32 = thread_spinel_get_i32,
                   .spinel_get_uint = thread_spinel_get_uint,
                   .spinel_get_eui64 = thread_spinel_get_eui64,
                   .spinel_get_ipv6 = thread_spinel_get_ipv6,
                   .spinel_get_utf8 = thread_spinel_get_utf8,
                   .spinel_get_data = thread_spinel_get_data,
                   .spinel_get_data_wlen = thread_spinel_get_data_wlen,
                   .spinel_reader_ok = thread_spinel_reader_ok,
                   .spinel_writer_init = thread_spinel_writer_init,
                   .spinel_put_bool = thread_spinel_put_bool,
                   .spinel_put_u8 = thread_spinel_put_u8,
                   .spinel_put_i8 = thread_spinel_put_i8,
                   .spinel_put_u16 = thread_spinel_put_u16,
                   .spinel_put_i16 = thread_spinel_put_i16,
                   .spinel_put_u32 = thread_spinel_put_u32,
                   .spinel_put_i32 = thread_spinel_put_i32,
                   .spinel_put_uint = thread_spinel_put_uint,
                   .spinel_put_eui64 = thread_spinel_put_eui64,
                   .spinel_put_ipv6 = thread_spinel_put_ipv6,
                   .spinel_put_utf8 = thread_spinel_put_utf8,
                   .spinel_put_data = thread_spinel_put_data,
                   .spinel_put_data_wlen = thread_spinel_put_data_wlen,
                   .spinel_writer_len = thread_spinel_writer_len,
                   .spinel_prop_lookup = thread_spinel_prop_lookup,
                   .spinel_prop_name = thread_spinel_prop_name,
                   .spinel_status_name = thread_spinel_status_name,
                   .spinel_frame_encode = thread_spinel_frame_encode,
                   .spinel_frame_decode = thread_spinel_frame_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_THREAD
