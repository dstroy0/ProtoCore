// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sdi12.c
 * @brief SDI-12 command / response codec (pure, host-tested).
 */

#include "services/peripherals/sdi12/sdi12.h"

#if PC_ENABLE_SDI12

#include "shared_primitives/crc.h" // PC_CRC16_ARC
#include "shared_primitives/numparse.h"
#include <string.h>

size_t pc_sdi12_build(char *buf, size_t cap, char addr, const char *body)
{
    if (!buf || !body)
    {
        return 0;
    }
    size_t blen = strnlen(body, cap);
    size_t n = 1 + blen + 1; // addr + body + '!'
    if (cap < n + 1)         // + room for the NUL terminator
    {
        return 0;
    }
    buf[0] = addr;
    memcpy(buf + 1, body, blen);
    buf[1 + blen] = '!';
    buf[n] = '\0';
    return n;
}

size_t pc_sdi12_build_ack(char *buf, size_t cap, char addr)
{
    return pc_sdi12_build(buf, cap, addr, "");
}

size_t pc_sdi12_build_identify(char *buf, size_t cap, char addr)
{
    return pc_sdi12_build(buf, cap, addr, "I");
}

size_t pc_sdi12_build_measure(char *buf, size_t cap, char addr, proto_bool with_crc)
{
    return pc_sdi12_build(buf, cap, addr, with_crc ? "MC" : "M");
}

size_t pc_sdi12_build_concurrent(char *buf, size_t cap, char addr, proto_bool with_crc)
{
    return pc_sdi12_build(buf, cap, addr, with_crc ? "CC" : "C");
}

size_t pc_sdi12_build_measure_additional(char *buf, size_t cap, char addr, uint8_t m_index, proto_bool with_crc)
{
    if (m_index < 1 || m_index > 9)
    {
        return 0;
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
    return pc_sdi12_build(buf, cap, addr, body);
}

size_t pc_sdi12_build_concurrent_additional(char *buf, size_t cap, char addr, uint8_t c_index, proto_bool with_crc)
{
    if (c_index < 1 || c_index > 9)
    {
        return 0;
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
    return pc_sdi12_build(buf, cap, addr, body);
}

size_t pc_sdi12_build_continuous(char *buf, size_t cap, char addr, uint8_t r_index, proto_bool with_crc)
{
    if (r_index > 9)
    {
        return 0;
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
    return pc_sdi12_build(buf, cap, addr, body);
}

size_t pc_sdi12_build_verify(char *buf, size_t cap, char addr)
{
    return pc_sdi12_build(buf, cap, addr, "V");
}

size_t pc_sdi12_build_data(char *buf, size_t cap, char addr, uint8_t d_index)
{
    if (d_index > 9)
    {
        return 0;
    }
    char body[3] = {'D', (char)('0' + d_index), '\0'};
    return pc_sdi12_build(buf, cap, addr, body);
}

size_t pc_sdi12_build_change_address(char *buf, size_t cap, char addr, char new_addr)
{
    char body[3] = {'A', new_addr, '\0'};
    return pc_sdi12_build(buf, cap, addr, body);
}

size_t pc_sdi12_build_query_address(char *buf, size_t cap)
{
    return pc_sdi12_build(buf, cap, '?', "");
}

proto_bool pc_sdi12_parse_measure(const char *resp, size_t len, char *addr, uint16_t *ready_sec, uint8_t *num_values)
{
    if (!resp || len < 5) // a<ttt><n> is at least 5 octets
    {
        return PROTO_FALSE;
    }
    for (int i = 1; i <= 3; i++)
    {
        if (!pc_np_digit(resp[i]))
        {
            return PROTO_FALSE;
        }
    }
    if (!pc_np_digit(resp[4]))
    {
        return PROTO_FALSE;
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
    for (size_t i = 4; i < len && pc_np_digit(resp[i]); i++)
    {
        count = (uint16_t)(count * 10 + (resp[i] - '0'));
    }
    if (num_values)
    {
        *num_values = (uint8_t)count;
    }
    return PROTO_TRUE;
}

proto_bool pc_sdi12_parse_values(const char *resp, size_t len, float *out, size_t max, size_t *n)
{
    if (!resp || !out || !n)
    {
        return PROTO_FALSE;
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
            // pc_strtof handles a leading '-'; for '+' parse the magnitude after the sign.
            float v = (c == '+') ? pc_strtof(start + 1, &end) : pc_strtof(start, &end);
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
    return PROTO_TRUE;
}

proto_bool pc_sdi12_parse_identify(const char *resp, size_t len, Sdi12Identity *out)
{
    if (!resp || !out || len < 20) // addr(1) + version(2) + vendor(8) + model(6) + sensor version(3)
    {
        return PROTO_FALSE;
    }
    out->addr = resp[0];
    memcpy(out->sdi_version, resp + 1, 2);
    out->sdi_version[2] = '\0';
    memcpy(out->vendor, resp + 3, 8);
    out->vendor[8] = '\0';
    memcpy(out->model, resp + 11, 6);
    out->model[6] = '\0';
    memcpy(out->sensor_version, resp + 17, 3);
    out->sensor_version[3] = '\0';
    return PROTO_TRUE;
}

uint16_t pc_sdi12_crc16(const uint8_t *data, size_t len)
{
    // SDI-12 v1.3 uses the reflected CRC-16 (SDI12_CRC_POLY = 0xA001 = reflect(0x8005), init 0, no
    // final XOR) - cataloged as CRC-16/ARC. test_crc diffs the shared engine against the loop that
    // used to live here over every length 0..64, so this is byte-identical to it.
    return (uint16_t)pc_crc(&PC_CRC16_ARC, data, len);
}

void pc_sdi12_crc_encode(uint16_t crc, char out[SDI12_CRC_CHARS])
{
    out[0] = (char)(0x40u | (crc >> 12)); // top bits
    out[1] = (char)(0x40u | ((crc >> 6) & 0x3Fu));
    out[2] = (char)(0x40u | (crc & 0x3Fu));
}

proto_bool pc_sdi12_check_crc(const char *resp, size_t len)
{
    if (!resp)
    {
        return PROTO_FALSE;
    }
    // Trim a trailing <CR><LF> if present.
    while (len > 0 && (resp[len - 1] == '\n' || resp[len - 1] == '\r'))
    {
        len--;
    }
    if (len < SDI12_CRC_CHARS + 1) // need at least 1 data octet + the 3 CRC octets
    {
        return PROTO_FALSE;
    }
    size_t data_len = len - SDI12_CRC_CHARS;
    char enc[SDI12_CRC_CHARS];
    pc_sdi12_crc_encode(pc_sdi12_crc16((const uint8_t *)resp, data_len), enc);
    return memcmp(enc, resp + data_len, SDI12_CRC_CHARS) == 0;
}

#endif // PC_ENABLE_SDI12
