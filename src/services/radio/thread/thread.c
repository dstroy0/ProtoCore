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

#include "protocore_config.h" // the entry point: the widths

#include "services/radio/thread/thread.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_X25

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

uint8_t protocore_thread_spinel_pack_uint(uint8_t *work, uint32_t value, uint8_t *out, uint8_t cap)
{
    (void)work;

    if (!out)
    {
        return 0;
    }
    uint8_t n = 0;
    do
    {
        if (n >= cap)
        {
            return 0;
        }
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value)
        {
            byte |= 0x80; // more bytes follow
        }
        out[n++] = byte;
    } while (value);
    return n;
}

int protocore_thread_spinel_unpack_uint(uint8_t *work, const uint8_t *raw, uint8_t len, uint32_t *value)
{
    int n_result = 0;
    (void)work;

    if (!raw)
    {
        return 0;
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
            return n + 1;
        }
        shift += 7;
        if (shift >= 32)
        {
            n_result = -1; // does not fit a uint32
            return n_result;
        }
    }
    n_result = 0; // truncated - need more bytes
    return n_result;
}

uint16_t protocore_thread_spinel_command_build(uint8_t *work, uint8_t header, uint32_t cmd, uint32_t prop,
                                               const uint8_t *value, uint16_t value_len, uint8_t *out, uint16_t cap)
{
    if (!out || cap < 1 || (value == NULL && value_len > 0))
    {
        return 0;
    }
    uint16_t p = 0;
    out[p++] = header;
    uint8_t thread_u8 = Thread.spinel_pack_uint(work, cmd, out + p, (uint8_t)(cap - p));
    uint8_t n = thread_u8;
    if (n == 0)
    {
        return 0;
    }
    p += n;
    uint8_t thread_u82 = Thread.spinel_pack_uint(work, prop, out + p, (uint8_t)(cap > p ? cap - p : 0));
    n = thread_u82;
    if (n == 0)
    {
        return 0;
    }
    p += n;
    if ((uint32_t)p + value_len > cap)
    {
        return 0;
    }
    for (uint16_t i = 0; i < value_len; i++)
    {
        out[p + i] = value[i];
    }
    return (uint16_t)(p + value_len);
}

int protocore_thread_spinel_command_parse(uint8_t *work, const uint8_t *payload, uint16_t len, uint8_t *header,
                                          uint32_t *cmd, uint32_t *prop, const uint8_t **value, uint16_t *value_len)
{
    if (!payload || len < 1)
    {
        return -1;
    }
    uint16_t p = 0;
    uint8_t h = payload[p++];
    uint32_t c = 0;
    uint32_t pr = 0;
    int thread_n = Thread.spinel_unpack_uint(work, payload + p, (uint8_t)((len - p) > 255 ? 255 : (len - p)), &c);
    int n = thread_n;
    if (n <= 0)
    {
        return -1;
    }
    p += (uint16_t)n;
    int thread_n2 = Thread.spinel_unpack_uint(work, payload + p, (uint8_t)((len - p) > 255 ? 255 : (len - p)), &pr);
    n = thread_n2;
    if (n <= 0)
    {
        return -1;
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
    return (int)p;
}

// --- Spinel value semantics -------------------------------------------------------------

void protocore_thread_spinel_reader_init(uint8_t *work, SpinelReader *r, const uint8_t *value, uint16_t len)
{
    (void)work;

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

proto_bool protocore_thread_spinel_get_bool(uint8_t *work, SpinelReader *r, proto_bool *out)
{
    (void)work;

    const uint8_t *b = take(r, 1);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (*b != 0);
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_u8(uint8_t *work, SpinelReader *r, uint8_t *out)
{
    (void)work;

    const uint8_t *b = take(r, 1);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = b[0];
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_i8(uint8_t *work, SpinelReader *r, int8_t *out)
{
    (void)work;

    const uint8_t *b = take(r, 1);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (int8_t)b[0];
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_u16(uint8_t *work, SpinelReader *r, uint16_t *out)
{
    (void)work;

    const uint8_t *b = take(r, 2);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (uint16_t)(b[0] | (b[1] << 8));
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_i16(uint8_t *work, SpinelReader *r, int16_t *out)
{
    uint16_t v = 0;
    proto_bool thread_ok = Thread.spinel_get_u16(work, r, &v);
    if (!thread_ok)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (int16_t)v;
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_u32(uint8_t *work, SpinelReader *r, uint32_t *out)
{
    (void)work;

    const uint8_t *b = take(r, 4);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_i32(uint8_t *work, SpinelReader *r, int32_t *out)
{
    uint32_t v = 0;
    proto_bool thread_ok = Thread.spinel_get_u32(work, r, &v);
    if (!thread_ok)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (int32_t)v;
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_uint(uint8_t *work, SpinelReader *r, uint32_t *out)
{
    if (!r || r->err)
    {
        return PROTO_FALSE;
    }
    uint32_t v = 0;
    int thread_n = Thread.spinel_unpack_uint(work, r->buf + r->off,
                                             (uint8_t)((r->len - r->off) > 255 ? 255 : (r->len - r->off)), &v);
    int n = thread_n;
    if (n <= 0)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    r->off = (uint16_t)(r->off + n);
    if (out)
    {
        *out = v;
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_eui64(uint8_t *work, SpinelReader *r, const uint8_t **out8)
{
    (void)work;

    const uint8_t *b = take(r, 8);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out8)
    {
        *out8 = b;
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_ipv6(uint8_t *work, SpinelReader *r, const uint8_t **out16)
{
    (void)work;

    const uint8_t *b = take(r, 16);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out16)
    {
        *out16 = b;
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_utf8(uint8_t *work, SpinelReader *r, const char **out, uint16_t *out_len)
{
    (void)work;

    if (!r || r->err)
    {
        return PROTO_FALSE;
    }
    uint16_t i = r->off;
    while (i < r->len && r->buf[i] != 0)
    {
        i++;
    }
    if (i >= r->len) // no NUL terminator in the value
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
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
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_data(uint8_t *work, SpinelReader *r, const uint8_t **out, uint16_t *out_len)
{
    (void)work;

    if (!r || r->err)
    {
        return PROTO_FALSE;
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
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_get_data_wlen(uint8_t *work, SpinelReader *r, const uint8_t **out, uint16_t *out_len)
{
    uint16_t n = 0;
    proto_bool thread_ok = Thread.spinel_get_u16(work, r, &n);
    if (!thread_ok)
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = take(r, n);
    if (!b)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = b;
    }
    if (out_len)
    {
        *out_len = n;
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_reader_ok(uint8_t *work, const SpinelReader *r)
{
    (void)work;

    return r && !r->err;
}

void protocore_thread_spinel_writer_init(uint8_t *work, SpinelWriter *w, uint8_t *out, uint16_t cap)
{
    (void)work;

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

proto_bool protocore_thread_spinel_put_bool(uint8_t *work, SpinelWriter *w, proto_bool v)
{
    (void)work;

    uint8_t *b = room(w, 1);
    if (!b)
    {
        return PROTO_FALSE;
    }
    b[0] = v ? 1 : 0;
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_put_u8(uint8_t *work, SpinelWriter *w, uint8_t v)
{
    (void)work;

    uint8_t *b = room(w, 1);
    if (!b)
    {
        return PROTO_FALSE;
    }
    b[0] = v;
    return PROTO_TRUE;
}

void protocore_thread_spinel_put_i8(uint8_t *work, SpinelWriter *w, int8_t v)
{

    Thread.spinel_put_u8(work, w, (uint8_t)v);
}

proto_bool protocore_thread_spinel_put_u16(uint8_t *work, SpinelWriter *w, uint16_t v)
{
    (void)work;

    uint8_t *b = room(w, 2);
    if (!b)
    {
        return PROTO_FALSE;
    }
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)(v >> 8);
    return PROTO_TRUE;
}

void protocore_thread_spinel_put_i16(uint8_t *work, SpinelWriter *w, int16_t v)
{

    Thread.spinel_put_u16(work, w, (uint16_t)v);
}

proto_bool protocore_thread_spinel_put_u32(uint8_t *work, SpinelWriter *w, uint32_t v)
{
    (void)work;

    uint8_t *b = room(w, 4);
    if (!b)
    {
        return PROTO_FALSE;
    }
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    return PROTO_TRUE;
}

void protocore_thread_spinel_put_i32(uint8_t *work, SpinelWriter *w, int32_t v)
{

    Thread.spinel_put_u32(work, w, (uint32_t)v);
}

proto_bool protocore_thread_spinel_put_uint(uint8_t *work, SpinelWriter *w, uint32_t v)
{
    if (!w || w->err)
    {
        return PROTO_FALSE;
    }
    uint8_t tmp[5];
    uint8_t thread_u8 = Thread.spinel_pack_uint(work, v, tmp, sizeof(tmp));
    uint8_t n = thread_u8;
    if (n == 0)
    {
        w->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t *b = room(w, n);
    if (!b)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        b[i] = tmp[i];
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_put_eui64(uint8_t *work, SpinelWriter *w, const uint8_t *v8)
{
    (void)work;

    if (!v8)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        return PROTO_FALSE;
    }
    uint8_t *b = room(w, 8);
    if (!b)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < 8; i++)
    {
        b[i] = v8[i];
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_put_ipv6(uint8_t *work, SpinelWriter *w, const uint8_t *v16)
{
    (void)work;

    if (!v16)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        return PROTO_FALSE;
    }
    uint8_t *b = room(w, 16);
    if (!b)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < 16; i++)
    {
        b[i] = v16[i];
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_put_utf8(uint8_t *work, SpinelWriter *w, const char *s)
{
    (void)work;

    if (!s)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        return PROTO_FALSE;
    }
    uint16_t n = 0;
    while (s[n] != 0)
    {
        n++;
    }
    uint8_t *b = room(w, (uint16_t)(n + 1)); // include the NUL
    if (!b)
    {
        return PROTO_FALSE;
    }
    for (uint16_t i = 0; i <= n; i++)
    {
        b[i] = (uint8_t)s[i];
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_put_data(uint8_t *work, SpinelWriter *w, const uint8_t *d, uint16_t n)
{
    (void)work;

    if (d == NULL && n > 0)
    {
        if (w)
        {
            w->err = PROTO_TRUE;
        }
        return PROTO_FALSE;
    }
    uint8_t *b = room(w, n);
    if (!b)
    {
        return PROTO_FALSE;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        b[i] = d[i];
    }
    return PROTO_TRUE;
}

proto_bool protocore_thread_spinel_put_data_wlen(uint8_t *work, SpinelWriter *w, const uint8_t *d, uint16_t n)
{
    proto_bool thread_ok = Thread.spinel_put_u16(work, w, n);
    if (!thread_ok)
    {
        return PROTO_FALSE;
    }
    Thread.spinel_put_data(work, w, d, n);
    return PROTO_FALSE;
}

uint16_t protocore_thread_spinel_writer_len(uint8_t *work, const SpinelWriter *w)
{
    (void)work;

    if (!w || w->err)
    {
        return 0;
    }
    return w->off;
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

const SpinelPropInfo *protocore_thread_spinel_prop_lookup(uint8_t *work, uint32_t id)
{
    (void)work;

    for (uint16_t i = 0; i < sizeof(k_props) / sizeof(k_props[0]); i++)
    {
        if (k_props[i].id == id)
        {
            return &k_props[i];
        }
    }
    return NULL;
}

const char *protocore_thread_spinel_prop_name(uint8_t *work, uint32_t id)
{

    const SpinelPropInfo *thread_ptr = Thread.spinel_prop_lookup(work, id);
    const SpinelPropInfo *e = thread_ptr;
    return e ? e->name : "UNKNOWN";
}

const char *protocore_thread_spinel_status_name(uint8_t *work, uint32_t status)
{
    (void)work;

    for (uint16_t i = 0; i < sizeof(k_status) / sizeof(k_status[0]); i++)
    {
        if (k_status[i].code == status)
        {
            return k_status[i].name;
        }
    }
    if (status >= SPINEL_STATUS_RESET_POWER_ON && status < SPINEL_STATUS_RESET_END)
    {
        return "RESET";
    }
    return "UNKNOWN";
}

uint16_t protocore_thread_spinel_fcs(uint8_t *work, const uint8_t *buf, uint16_t len)
{
    // The HDLC-lite FCS is CRC-16/X-25 (reflected poly 0x8408, init 0xFFFF, xorout 0xFFFF).
    CrcV.args.params = &PROTOCORE_CRC16_X25;
    CrcV.args.data = buf;
    CrcV.args.len = len;
    Crc.compute(work);
    return (uint16_t)CrcV.value;
}

uint16_t protocore_thread_spinel_frame_encode(uint8_t *work, const uint8_t *payload, uint16_t len, uint8_t *out,
                                              uint16_t cap)
{
    if (!out || len > PROTOCORE_THREAD_MAX_DATA || (payload == NULL && len > 0))
    {
        return 0;
    }
    uint16_t thread_value = Thread.spinel_fcs(work, payload, len);
    uint16_t fcs = thread_value;
    uint16_t p = 0;
    for (uint16_t i = 0; i < len; i++)
    {
        if (!put_stuffed(out, &p, cap, payload[i]))
        {
            return 0;
        }
    }
    if (!put_stuffed(out, &p, cap, (uint8_t)(fcs & 0xFF)) || // FCS low byte first
        !put_stuffed(out, &p, cap, (uint8_t)(fcs >> 8)))
    {
        return 0;
    }
    if (p + 1 > cap)
    {
        return 0;
    }
    out[p++] = HDLC_FLAG;
    return p;
}

int protocore_thread_spinel_frame_decode(uint8_t *work, const uint8_t *raw, uint16_t len, uint8_t *payload,
                                         uint16_t pay_cap, uint16_t *pay_len)
{
    int n_result = 0;
    if (!raw)
    {
        return 0;
    }
    uint16_t flag = 0;
    while (flag < len && raw[flag] != HDLC_FLAG)
    {
        flag++;
    }
    if (flag >= len)
    {
        n_result = 0; // no complete frame yet
        return n_result;
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
                n_result = -1; // dangling escape
                return n_result;
            }
            b = (uint8_t)(raw[i] ^ 0x20);
        }
        if (n >= sizeof(un))
        {
            return -1;
        }
        un[n++] = b;
    }
    if (n < 2)
    {
        n_result = -1; // need at least the FCS
        return n_result;
    }
    uint16_t plen = (uint16_t)(n - 2);
    uint16_t thread_value = Thread.spinel_fcs(work, un, plen);
    uint16_t fcs = thread_value;
    if ((uint16_t)(un[plen] | (un[plen + 1] << 8)) != fcs)
    {
        n_result = -1; // FCS mismatch (transmitted low byte first)
        return n_result;
    }
    if (plen > pay_cap)
    {
        return -1;
    }
    for (uint16_t i = 0; i < plen; i++)
    {
        payload[i] = un[i];
    }
    if (pay_len)
    {
        *pay_len = plen;
    }
    return (int)(flag + 1);
}
