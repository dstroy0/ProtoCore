// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sdi12.c
 * @brief SDI-12 command / response codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SDI12

#include "mmgr/protomem.h"
#include "server/peripherals/sdi12/sdi12.h"

#include "mmgr/protostr.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_ARC

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void sdi12_build(uint8_t *restrict work);
static void sdi12_crc16(uint8_t *restrict work);
static void sdi12_crc_encode(uint8_t *restrict work);

static void sdi12_build(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_args.buf;
    size_t cap = Sdi12.build_args.cap;
    char addr = Sdi12.build_args.addr;
    const char *body = Sdi12.build_args.body;

    if (!buf || !body)
    {
        Sdi12.n = 0;
        return;
    }
    size_t blen = str.len(body, cap);
    size_t n = 1 + blen + 1; // addr + body + '!'
    if (cap < n + 1)         // + room for the NUL terminator
    {
        Sdi12.n = 0;
        return;
    }
    buf[0] = addr;
    mem.cpy(buf + 1, body, blen);
    buf[1 + blen] = '!';
    buf[n] = '\0';
    Sdi12.n = n;
}

static void sdi12_build_ack(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_ack_args.buf;
    size_t cap = Sdi12.build_ack_args.cap;
    char addr = Sdi12.build_ack_args.addr;

    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = "";
    sdi12_build(work);
}

static void sdi12_build_identify(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_identify_args.buf;
    size_t cap = Sdi12.build_identify_args.cap;
    char addr = Sdi12.build_identify_args.addr;

    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = "I";
    sdi12_build(work);
}

static void sdi12_build_measure(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_measure_args.buf;
    size_t cap = Sdi12.build_measure_args.cap;
    char addr = Sdi12.build_measure_args.addr;
    proto_bool with_crc = Sdi12.build_measure_args.with_crc;

    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = with_crc ? "MC" : "M";
    sdi12_build(work);
}

static void sdi12_build_concurrent(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_concurrent_args.buf;
    size_t cap = Sdi12.build_concurrent_args.cap;
    char addr = Sdi12.build_concurrent_args.addr;
    proto_bool with_crc = Sdi12.build_concurrent_args.with_crc;

    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = with_crc ? "CC" : "C";
    sdi12_build(work);
}

static void sdi12_build_measure_additional(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_measure_additional_args.buf;
    size_t cap = Sdi12.build_measure_additional_args.cap;
    char addr = Sdi12.build_measure_additional_args.addr;
    uint8_t m_index = Sdi12.build_measure_additional_args.m_index;
    proto_bool with_crc = Sdi12.build_measure_additional_args.with_crc;

    if (m_index < 1 || m_index > 9)
    {
        Sdi12.n = 0;
        return;
    }
    char body[4];
    size_t b = 0;
    body[b++] = 'M';
    if (with_crc)
    {
        body[b++] = 'C';
    }
    body[b++] = (char)('0' + m_index);
    body[b] = '\0';
    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = body;
    sdi12_build(work);
}

static void sdi12_build_concurrent_additional(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_concurrent_additional_args.buf;
    size_t cap = Sdi12.build_concurrent_additional_args.cap;
    char addr = Sdi12.build_concurrent_additional_args.addr;
    uint8_t c_index = Sdi12.build_concurrent_additional_args.c_index;
    proto_bool with_crc = Sdi12.build_concurrent_additional_args.with_crc;

    if (c_index < 1 || c_index > 9)
    {
        Sdi12.n = 0;
        return;
    }
    char body[4];
    size_t b = 0;
    body[b++] = 'C';
    if (with_crc)
    {
        body[b++] = 'C';
    }
    body[b++] = (char)('0' + c_index);
    body[b] = '\0';
    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = body;
    sdi12_build(work);
}

static void sdi12_build_continuous(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_continuous_args.buf;
    size_t cap = Sdi12.build_continuous_args.cap;
    char addr = Sdi12.build_continuous_args.addr;
    uint8_t r_index = Sdi12.build_continuous_args.r_index;
    proto_bool with_crc = Sdi12.build_continuous_args.with_crc;

    if (r_index > 9)
    {
        Sdi12.n = 0;
        return;
    }
    char body[4];
    size_t b = 0;
    body[b++] = 'R';
    if (with_crc)
    {
        body[b++] = 'C';
    }
    body[b++] = (char)('0' + r_index);
    body[b] = '\0';
    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = body;
    sdi12_build(work);
}

static void sdi12_build_verify(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_verify_args.buf;
    size_t cap = Sdi12.build_verify_args.cap;
    char addr = Sdi12.build_verify_args.addr;

    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = "V";
    sdi12_build(work);
}

static void sdi12_build_data(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_data_args.buf;
    size_t cap = Sdi12.build_data_args.cap;
    char addr = Sdi12.build_data_args.addr;
    uint8_t d_index = Sdi12.build_data_args.d_index;

    if (d_index > 9)
    {
        Sdi12.n = 0;
        return;
    }
    char body[3] = {'D', (char)('0' + d_index), '\0'};
    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = body;
    sdi12_build(work);
}

static void sdi12_build_change_address(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_change_address_args.buf;
    size_t cap = Sdi12.build_change_address_args.cap;
    char addr = Sdi12.build_change_address_args.addr;
    char new_addr = Sdi12.build_change_address_args.new_addr;

    char body[3] = {'A', new_addr, '\0'};
    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = addr;
    Sdi12.build_args.body = body;
    sdi12_build(work);
}

static void sdi12_build_query_address(uint8_t *restrict work)
{
    (void)work;
    char *buf = Sdi12.build_query_address_args.buf;
    size_t cap = Sdi12.build_query_address_args.cap;

    Sdi12.build_args.buf = buf;
    Sdi12.build_args.cap = cap;
    Sdi12.build_args.addr = '?';
    Sdi12.build_args.body = "";
    sdi12_build(work);
}

static void sdi12_parse_measure(uint8_t *restrict work)
{
    (void)work;
    const char *resp = Sdi12.parse_measure_args.resp;
    size_t len = Sdi12.parse_measure_args.len;
    char *addr = Sdi12.parse_measure_args.addr;
    uint16_t *ready_sec = Sdi12.parse_measure_args.ready_sec;
    uint8_t *num_values = Sdi12.parse_measure_args.num_values;

    if (!resp || len < 5) // a<ttt><n> is at least 5 octets
    {
        Sdi12.ok = PROTO_FALSE;
        return;
    }
    for (int i = 1; i <= 3; i++)
    {
        if (!str.digit(resp[i]))
        {
            Sdi12.ok = PROTO_FALSE;
            return;
        }
    }
    if (!str.digit(resp[4]))
    {
        Sdi12.ok = PROTO_FALSE;
        return;
    }
    if (addr)
    {
        *addr = resp[0];
    }
    if (ready_sec)
    {
        *ready_sec = (uint16_t)((resp[1] - '0') * 100 + (resp[2] - '0') * 10 + (resp[3] - '0'));
    }
    // The value count is the remaining digits (1 digit for aM!, 2 for aC!).
    uint16_t count = 0;
    for (size_t i = 4; i < len && str.digit(resp[i]); i++)
    {
        count = (uint16_t)(count * 10 + (resp[i] - '0'));
    }
    if (num_values)
    {
        *num_values = (uint8_t)count;
    }
    Sdi12.ok = PROTO_TRUE;
}

static void sdi12_parse_values(uint8_t *restrict work)
{
    (void)work;
    const char *resp = Sdi12.parse_values_args.resp;
    size_t len = Sdi12.parse_values_args.len;
    float *out = Sdi12.parse_values_args.out;
    size_t max = Sdi12.parse_values_args.max;
    size_t *n = Sdi12.parse_values_args.n;

    if (!resp || !out || !n)
    {
        Sdi12.ok = PROTO_FALSE;
        return;
    }
    size_t cnt = 0;
    size_t i = 1; // skip the leading address
    while (i < len && cnt < max)
    {
        char c = resp[i];
        if (c == '\r' || c == '\n')
        {
            break;
        }
        if (c == '+' || c == '-')
        {
            const char *start = resp + i;
            const char *end = start;
            // str.to_float handles a leading '-'; for '+' parse the magnitude after the sign.
            float v = (c == '+') ? str.to_float(start + 1, &end) : str.to_float(start, &end);
            if (end == start || (c == '+' && end == start + 1)) // no digits consumed
            {
                i++;
                continue;
            }
            out[cnt++] = v;
            i = (size_t)(end - resp);
        }
        else
        {
            i++; // CRC octets / separators are skipped (they never begin with +/-)
        }
    }
    *n = cnt;
    Sdi12.ok = PROTO_TRUE;
}

static void sdi12_parse_identify(uint8_t *restrict work)
{
    (void)work;
    const char *resp = Sdi12.parse_identify_args.resp;
    size_t len = Sdi12.parse_identify_args.len;
    Sdi12Identity *out = Sdi12.parse_identify_args.out;

    if (!resp || !out || len < 20) // addr(1) + version(2) + vendor(8) + model(6) + sensor version(3)
    {
        Sdi12.ok = PROTO_FALSE;
        return;
    }
    out->addr = resp[0];
    mem.cpy(out->sdi_version, resp + 1, 2);
    out->sdi_version[2] = '\0';
    mem.cpy(out->vendor, resp + 3, 8);
    out->vendor[8] = '\0';
    mem.cpy(out->model, resp + 11, 6);
    out->model[6] = '\0';
    mem.cpy(out->sensor_version, resp + 17, 3);
    out->sensor_version[3] = '\0';
    Sdi12.ok = PROTO_TRUE;
}

static void sdi12_crc16(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = Sdi12.crc16_args.data;
    size_t len = Sdi12.crc16_args.len;

    // SDI-12 v1.3 uses the reflected CRC-16 (SDI12_CRC_POLY = 0xA001 = reflect(0x8005), init 0, no final XOR)
    // - cataloged as CRC-16/ARC.
    Crc.args.params = &PROTOCORE_CRC16_ARC;
    Crc.args.data = data;
    Crc.args.len = len;
    Crc.compute(Crc.internal);
    Sdi12.crc = (uint16_t)Crc.value;
}

static void sdi12_crc_encode(uint8_t *restrict work)
{
    (void)work;
    uint16_t crc = Sdi12.crc_encode_args.crc;
    char *out = Sdi12.crc_encode_args.out;

    out[0] = (char)(0x40u | (crc >> 12)); // top bits
    out[1] = (char)(0x40u | ((crc >> 6) & 0x3Fu));
    out[2] = (char)(0x40u | (crc & 0x3Fu));
}

static void sdi12_check_crc(uint8_t *restrict work)
{
    (void)work;
    const char *resp = Sdi12.check_crc_args.resp;
    size_t len = Sdi12.check_crc_args.len;

    if (!resp)
    {
        Sdi12.ok = PROTO_FALSE;
        return;
    }
    // Trim a trailing <CR><LF> if present.
    while (len > 0 && (resp[len - 1] == '\n' || resp[len - 1] == '\r'))
    {
        len--;
    }
    if (len < SDI12_CRC_CHARS + 1) // need at least 1 data octet + the 3 CRC octets
    {
        Sdi12.ok = PROTO_FALSE;
        return;
    }
    size_t data_len = len - SDI12_CRC_CHARS;
    char enc[SDI12_CRC_CHARS];
    // The checksum is captured before the encode runs: both report through the one namespace, so
    // nesting them would have the encode read its own outcome.
    Sdi12.crc16_args.data = (const uint8_t *)resp;
    Sdi12.crc16_args.len = data_len;
    sdi12_crc16(work);
    const uint16_t crc = Sdi12.crc;
    Sdi12.crc_encode_args.crc = crc;
    Sdi12.crc_encode_args.out = enc;
    sdi12_crc_encode(work);
    Sdi12.ok = mem.cmp(enc, resp + data_len, SDI12_CRC_CHARS) == 0;
}

Sdi12Ns Sdi12 = {.build = sdi12_build,
                 .build_ack = sdi12_build_ack,
                 .build_identify = sdi12_build_identify,
                 .build_measure = sdi12_build_measure,
                 .build_concurrent = sdi12_build_concurrent,
                 .build_measure_additional = sdi12_build_measure_additional,
                 .build_concurrent_additional = sdi12_build_concurrent_additional,
                 .build_continuous = sdi12_build_continuous,
                 .build_verify = sdi12_build_verify,
                 .build_data = sdi12_build_data,
                 .build_change_address = sdi12_build_change_address,
                 .build_query_address = sdi12_build_query_address,
                 .parse_measure = sdi12_parse_measure,
                 .parse_values = sdi12_parse_values,
                 .parse_identify = sdi12_parse_identify,
                 .crc16 = sdi12_crc16,
                 .crc_encode = sdi12_crc_encode,
                 .check_crc = sdi12_check_crc};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SDI12
