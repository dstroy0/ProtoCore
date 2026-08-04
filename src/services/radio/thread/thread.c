// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file thread.c
 * @brief Thread spinel / HDLC-lite framing codec - implementation.
 *
 * HDLC-lite: [payload | FCS(lo,hi)] byte-stuffed and Flag-terminated. The FCS is CRC-16/X-25
 * (poly 0x1021 reflected = 0x8408, init 0xFFFF, reflected, final XOR 0xFFFF); the reserved
 * bytes 0x7E / 0x7D / 0x11 / 0x13 are escaped as 0x7D, (byte XOR 0x20).
 */

#include "services/radio/thread/thread.h"
#include "shared_primitives/crc.h" // PC_CRC16_X25

#if PC_ENABLE_THREAD

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

uint8_t pc_spinel_pack_uint(uint32_t value, uint8_t *out, uint8_t cap)
{
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

int pc_spinel_unpack_uint(const uint8_t *raw, uint8_t len, uint32_t *value)
{
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
            return -1; // does not fit a uint32
        }
    }
    return 0; // truncated - need more bytes
}

uint16_t pc_spinel_command_build(uint8_t header, uint32_t cmd, uint32_t prop, const uint8_t *value, uint16_t value_len,
                                 uint8_t *out, uint16_t cap)
{
    if (!out || cap < 1 || (value == NULL && value_len > 0))
    {
        return 0;
    }
    uint16_t p = 0;
    out[p++] = header;
    uint8_t n = pc_spinel_pack_uint(cmd, out + p, (uint8_t)(cap - p));
    if (n == 0)
    {
        return 0;
    }
    p += n;
    n = pc_spinel_pack_uint(prop, out + p, (uint8_t)(cap > p ? cap - p : 0));
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

int pc_spinel_command_parse(const uint8_t *payload, uint16_t len, uint8_t *header, uint32_t *cmd, uint32_t *prop,
                            const uint8_t **value, uint16_t *value_len)
{
    if (!payload || len < 1)
    {
        return -1;
    }
    uint16_t p = 0;
    uint8_t h = payload[p++];
    uint32_t c = 0;
    uint32_t pr = 0;
    int n = pc_spinel_unpack_uint(payload + p, (uint8_t)((len - p) > 255 ? 255 : (len - p)), &c);
    if (n <= 0)
    {
        return -1;
    }
    p += (uint16_t)n;
    n = pc_spinel_unpack_uint(payload + p, (uint8_t)((len - p) > 255 ? 255 : (len - p)), &pr);
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

void pc_spinel_reader_init(SpinelReader *r, const uint8_t *value, uint16_t len)
{
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

proto_bool pc_spinel_get_bool(SpinelReader *r, proto_bool *out)
{
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

proto_bool pc_spinel_get_u8(SpinelReader *r, uint8_t *out)
{
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

proto_bool pc_spinel_get_i8(SpinelReader *r, int8_t *out)
{
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

proto_bool pc_spinel_get_u16(SpinelReader *r, uint16_t *out)
{
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

proto_bool pc_spinel_get_i16(SpinelReader *r, int16_t *out)
{
    uint16_t v = 0;
    if (!pc_spinel_get_u16(r, &v))
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (int16_t)v;
    }
    return PROTO_TRUE;
}

proto_bool pc_spinel_get_u32(SpinelReader *r, uint32_t *out)
{
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

proto_bool pc_spinel_get_i32(SpinelReader *r, int32_t *out)
{
    uint32_t v = 0;
    if (!pc_spinel_get_u32(r, &v))
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (int32_t)v;
    }
    return PROTO_TRUE;
}

proto_bool pc_spinel_get_uint(SpinelReader *r, uint32_t *out)
{
    if (!r || r->err)
    {
        return PROTO_FALSE;
    }
    uint32_t v = 0;
    int n = pc_spinel_unpack_uint(r->buf + r->off, (uint8_t)((r->len - r->off) > 255 ? 255 : (r->len - r->off)), &v);
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

proto_bool pc_spinel_get_eui64(SpinelReader *r, const uint8_t **out8)
{
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

proto_bool pc_spinel_get_ipv6(SpinelReader *r, const uint8_t **out16)
{
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

proto_bool pc_spinel_get_utf8(SpinelReader *r, const char **out, uint16_t *out_len)
{
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

proto_bool pc_spinel_get_data(SpinelReader *r, const uint8_t **out, uint16_t *out_len)
{
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

proto_bool pc_spinel_get_data_wlen(SpinelReader *r, const uint8_t **out, uint16_t *out_len)
{
    uint16_t n = 0;
    if (!pc_spinel_get_u16(r, &n))
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

proto_bool pc_spinel_reader_ok(const SpinelReader *r)
{
    return r && !r->err;
}

void pc_spinel_writer_init(SpinelWriter *w, uint8_t *out, uint16_t cap)
{
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

proto_bool pc_spinel_put_bool(SpinelWriter *w, proto_bool v)
{
    uint8_t *b = room(w, 1);
    if (!b)
    {
        return PROTO_FALSE;
    }
    b[0] = v ? 1 : 0;
    return PROTO_TRUE;
}

proto_bool pc_spinel_put_u8(SpinelWriter *w, uint8_t v)
{
    uint8_t *b = room(w, 1);
    if (!b)
    {
        return PROTO_FALSE;
    }
    b[0] = v;
    return PROTO_TRUE;
}

proto_bool pc_spinel_put_i8(SpinelWriter *w, int8_t v)
{
    return pc_spinel_put_u8(w, (uint8_t)v);
}

proto_bool pc_spinel_put_u16(SpinelWriter *w, uint16_t v)
{
    uint8_t *b = room(w, 2);
    if (!b)
    {
        return PROTO_FALSE;
    }
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)(v >> 8);
    return PROTO_TRUE;
}

proto_bool pc_spinel_put_i16(SpinelWriter *w, int16_t v)
{
    return pc_spinel_put_u16(w, (uint16_t)v);
}

proto_bool pc_spinel_put_u32(SpinelWriter *w, uint32_t v)
{
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

proto_bool pc_spinel_put_i32(SpinelWriter *w, int32_t v)
{
    return pc_spinel_put_u32(w, (uint32_t)v);
}

proto_bool pc_spinel_put_uint(SpinelWriter *w, uint32_t v)
{
    if (!w || w->err)
    {
        return PROTO_FALSE;
    }
    uint8_t tmp[5];
    uint8_t n = pc_spinel_pack_uint(v, tmp, sizeof(tmp));
    // uint32 (needs at most ceil(32/7) = 5 bytes of 7-bit groups), so pack_uint can never return 0 here.
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

proto_bool pc_spinel_put_eui64(SpinelWriter *w, const uint8_t *v8)
{
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

proto_bool pc_spinel_put_ipv6(SpinelWriter *w, const uint8_t *v16)
{
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

proto_bool pc_spinel_put_utf8(SpinelWriter *w, const char *s)
{
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

proto_bool pc_spinel_put_data(SpinelWriter *w, const uint8_t *d, uint16_t n)
{
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

proto_bool pc_spinel_put_data_wlen(SpinelWriter *w, const uint8_t *d, uint16_t n)
{
    if (!pc_spinel_put_u16(w, n))
    {
        return PROTO_FALSE;
    }
    return pc_spinel_put_data(w, d, n);
}

uint16_t pc_spinel_writer_len(const SpinelWriter *w)
{
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

const SpinelPropInfo *pc_spinel_prop_lookup(uint32_t id)
{
    for (uint16_t i = 0; i < sizeof(k_props) / sizeof(k_props[0]); i++)
    {
        if (k_props[i].id == id)
        {
            return &k_props[i];
        }
    }
    return NULL;
}

const char *pc_spinel_prop_name(uint32_t id)
{
    const SpinelPropInfo *e = pc_spinel_prop_lookup(id);
    return e ? e->name : "UNKNOWN";
}

const char *pc_spinel_status_name(uint32_t status)
{
    for (uint16_t i = 0; i < sizeof(k_status) / sizeof(k_status[0]); i++)
    {
        if (k_status[i].code == status)
        {
            return k_status[i].name;
        }
    }
    if (status >= SPINEL_STATUS_RESET_POWER_ON && status <= SPINEL_STATUS_RESET_POWER_ON + 7)
    {
        return "RESET";
    }
    return "UNKNOWN";
}

uint16_t pc_spinel_fcs(const uint8_t *buf, uint16_t len)
{
    // The HDLC-lite FCS is CRC-16/X-25 (reflected poly 0x8408, init 0xFFFF, xorout 0xFFFF).
    // test_crc diffs the shared engine against the loop that used to live here over every length
    // 0..64, and asserts the cataloge check value 0x906E that this suite already pinned.
    return (uint16_t)pc_crc(&PC_CRC16_X25, buf, len);
}

uint16_t pc_spinel_frame_encode(const uint8_t *payload, uint16_t len, uint8_t *out, uint16_t cap)
{
    if (!out || len > PC_THREAD_MAX_DATA || (payload == NULL && len > 0))
    {
        return 0;
    }
    uint16_t fcs = pc_spinel_fcs(payload, len);
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

int pc_spinel_frame_decode(const uint8_t *raw, uint16_t len, uint8_t *payload, uint16_t pay_cap, uint16_t *pay_len)
{
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
        return 0; // no complete frame yet
    }

    // Remove the byte-stuffing from raw[0, flag) into a scratch: payload + FCS(2).
    uint8_t un[PC_THREAD_MAX_DATA + 2];
    uint16_t n = 0;
    for (uint16_t i = 0; i < flag; i++)
    {
        uint8_t b = raw[i];
        if (b == HDLC_ESCAPE)
        {
            if (++i >= flag)
            {
                return -1; // dangling escape
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
        return -1; // need at least the FCS
    }
    uint16_t plen = (uint16_t)(n - 2);
    uint16_t fcs = pc_spinel_fcs(un, plen);
    if ((uint16_t)(un[plen] | (un[plen + 1] << 8)) != fcs)
    {
        return -1; // FCS mismatch (transmitted low byte first)
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

#endif // PC_ENABLE_THREAD
