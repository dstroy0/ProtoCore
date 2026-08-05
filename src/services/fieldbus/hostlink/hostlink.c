// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hostlink.c
 * @brief Omron Host Link (C-mode) frame builder + parser (pure, host-tested).
 */

#include "services/fieldbus/hostlink/hostlink.h"

#if PC_ENABLE_HOSTLINK

uint8_t pc_hostlink_fcs(const char *data, size_t len)
{
    uint8_t f = 0;
    for (size_t i = 0; i < len; i++)
    {
        f ^= (uint8_t)data[i];
    }
    return f;
}

static char hex_digit(uint8_t v)
{
    return (char)(v < 10 ? '0' + v : 'A' + (v - 10));
}

// Parse one hex digit (0-9 A-F a-f) into 0..15, or -1 if invalid.
static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

size_t pc_hostlink_build(char *buf, size_t cap, uint8_t node, const char *header_code, const char *text,
                         size_t text_len)
{
    if (!buf || !header_code || node > 99 || (text_len && !text))
    {
        return 0;
    }
    if (header_code[0] == '\0' || header_code[1] == '\0') // need exactly 2 header characters
    {
        return 0;
    }
    size_t total = 1 + 2 + 2 + text_len + 2 + 2; // @ + UU + XX + text + FCS + *CR
    if (total >= cap)                            // need room for the frame + a NUL terminator
    {
        return 0;
    }

    size_t p = 0;
    buf[p++] = '@';
    buf[p++] = (char)('0' + node / 10);
    buf[p++] = (char)('0' + node % 10);
    buf[p++] = header_code[0];
    buf[p++] = header_code[1];
    if (text_len)
    {
        memcpy(buf + p, text, text_len);
        p += text_len;
    }
    uint8_t f = pc_hostlink_fcs(buf, p); // XOR over '@' .. end of text
    buf[p++] = hex_digit((uint8_t)(f >> 4));
    buf[p++] = hex_digit((uint8_t)(f & 0x0F));
    buf[p++] = '*';
    buf[p++] = '\r';
    buf[p] = '\0'; // NUL-terminate so callers may treat the ASCII frame as a string (matches pc_sdi12_build)
    return p;
}

proto_bool pc_hostlink_parse(const char *buf, size_t len, HostlinkFrame *out)
{
    // minimum: @ UU XX FF * CR = 1 + 2 + 2 + 2 + 1 + 1 = 9
    if (!buf || !out || len < 9)
    {
        return PROTO_FALSE;
    }
    if (buf[0] != '@' || buf[len - 1] != '\r' || buf[len - 2] != '*')
    {
        return PROTO_FALSE;
    }
    if (buf[1] < '0' || buf[1] > '9' || buf[2] < '0' || buf[2] > '9')
    {
        return PROTO_FALSE;
    }

    size_t fcs_pos = len - 4; // the two FCS chars precede '*' CR
    int hi = hex_val(buf[fcs_pos]);
    int lo = hex_val(buf[fcs_pos + 1]);
    if (hi < 0 || lo < 0)
    {
        return PROTO_FALSE;
    }
    uint8_t got = (uint8_t)((hi << 4) | lo);
    if (pc_hostlink_fcs(buf, fcs_pos) != got) // XOR over '@' .. last text char
    {
        return PROTO_FALSE;
    }

    out->node = (uint8_t)((buf[1] - '0') * 10 + (buf[2] - '0'));
    out->header_code[0] = buf[3];
    out->header_code[1] = buf[4];
    out->header_code[2] = '\0';
    out->text = buf + 5;
    out->text_len = fcs_pos - 5;
    return PROTO_TRUE;
}

proto_bool pc_hostlink_end_code(const HostlinkFrame *f, uint8_t *code)
{
    if (!f || f->text_len < 2)
    {
        return PROTO_FALSE;
    }
    int hi = hex_val(f->text[0]);
    int lo = hex_val(f->text[1]);
    if (hi < 0 || lo < 0)
    {
        return PROTO_FALSE;
    }
    if (code)
    {
        *code = (uint8_t)((hi << 4) | lo);
    }
    return PROTO_TRUE;
}

size_t pc_hostlink_build_read(char *buf, size_t cap, uint8_t node, uint16_t address, uint16_t count)
{
    if (address > 9999 || count > 9999 || count == 0)
    {
        return 0;
    }
    char text[8]; // 4-digit beginning word address + 4-digit word count, zero-padded decimal
    text[0] = (char)('0' + (address / 1000) % 10);
    text[1] = (char)('0' + (address / 100) % 10);
    text[2] = (char)('0' + (address / 10) % 10);
    text[3] = (char)('0' + address % 10);
    text[4] = (char)('0' + (count / 1000) % 10);
    text[5] = (char)('0' + (count / 100) % 10);
    text[6] = (char)('0' + (count / 10) % 10);
    text[7] = (char)('0' + count % 10);
    return pc_hostlink_build(buf, cap, node, "RD", text, sizeof(text));
}

proto_bool pc_hostlink_read_word(const HostlinkFrame *f, size_t index, uint16_t *out)
{
    if (!f || !out)
    {
        return PROTO_FALSE;
    }
    // A read response's text is the 2-char end code followed by 4-hex-char word values.
    size_t pos = 2 + index * 4;
    if (f->text_len < pos + 4)
    {
        return PROTO_FALSE;
    }
    int a = hex_val(f->text[pos]), b = hex_val(f->text[pos + 1]);
    int c = hex_val(f->text[pos + 2]), d = hex_val(f->text[pos + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0)
    {
        return PROTO_FALSE;
    }
    *out = (uint16_t)((a << 12) | (b << 8) | (c << 4) | d);
    return PROTO_TRUE;
}

size_t pc_hostlink_build_write(char *buf, size_t cap, uint8_t node, uint16_t address, const uint16_t *words,
                               size_t word_count)
{
    if (!buf || node > 99 || address > 9999 || word_count == 0 || !words)
    {
        return 0;
    }
    if (word_count > cap / 4) // fail closed before word_count*4 could overflow / exceed the buffer
    {
        return 0;
    }
    // @ + UU + WR + addr(4) + word_count*4 hex + FCS(2) + *CR, plus a NUL terminator.
    size_t total = 1 + 2 + 2 + 4 + word_count * 4 + 2 + 2;
    if (total >= cap)
    {
        return 0;
    }

    size_t p = 0;
    buf[p++] = '@';
    buf[p++] = (char)('0' + node / 10);
    buf[p++] = (char)('0' + node % 10);
    buf[p++] = 'W';
    buf[p++] = 'R';
    buf[p++] = (char)('0' + (address / 1000) % 10);
    buf[p++] = (char)('0' + (address / 100) % 10);
    buf[p++] = (char)('0' + (address / 10) % 10);
    buf[p++] = (char)('0' + address % 10);
    for (size_t i = 0; i < word_count; i++)
    {
        uint16_t w = words[i];
        buf[p++] = hex_digit((uint8_t)((w >> 12) & 0x0F));
        buf[p++] = hex_digit((uint8_t)((w >> 8) & 0x0F));
        buf[p++] = hex_digit((uint8_t)((w >> 4) & 0x0F));
        buf[p++] = hex_digit((uint8_t)(w & 0x0F));
    }
    uint8_t f = pc_hostlink_fcs(buf, p); // XOR over '@' .. last data char
    buf[p++] = hex_digit((uint8_t)(f >> 4));
    buf[p++] = hex_digit((uint8_t)(f & 0x0F));
    buf[p++] = '*';
    buf[p++] = '\r';
    buf[p] = '\0'; // NUL-terminate so callers may treat the ASCII frame as a string
    return p;
}

#endif // PC_ENABLE_HOSTLINK
