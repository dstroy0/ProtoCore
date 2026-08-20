// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hostlink.c
 * @brief Omron Host Link (C-mode) frame builder + parser (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HOSTLINK

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/hostlink/hostlink.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_hostlink_build(uint8_t *restrict work);
void protocore_hostlink_fcs(uint8_t *restrict work);

void protocore_hostlink_fcs(uint8_t *restrict work)
{
    (void)work;
    const char *data = HostlinkV.fcs_args.data;
    size_t len = HostlinkV.fcs_args.len;

    uint8_t f = 0;
    for (size_t i = 0; i < len; i++)
    {
        f ^= (uint8_t)data[i];
    }
    HostlinkV.value = f;
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

void protocore_hostlink_build(uint8_t *restrict work)
{
    char *buf = HostlinkV.build_args.buf;
    size_t cap = HostlinkV.build_args.cap;
    uint8_t node = HostlinkV.build_args.node;
    const char *header_code = HostlinkV.build_args.header_code;
    const char *text = HostlinkV.build_args.text;
    size_t text_len = HostlinkV.build_args.text_len;

    if (!buf || !header_code || node > 99 || (text_len && !text))
    {
        HostlinkV.n = 0;
        return;
    }
    if (header_code[0] == '\0' || header_code[1] == '\0') // need exactly 2 header characters
    {
        HostlinkV.n = 0;
        return;
    }
    size_t total = 1 + 2 + 2 + text_len + 2 + 2; // @ + UU + XX + text + FCS + *CR
    if (total >= cap)                            // need room for the frame + a NUL terminator
    {
        HostlinkV.n = 0;
        return;
    }

    size_t p = 0;
    buf[p++] = '@';
    buf[p++] = (char)('0' + node / 10);
    buf[p++] = (char)('0' + node % 10);
    buf[p++] = header_code[0];
    buf[p++] = header_code[1];
    if (text_len)
    {
        mem.cpy(buf + p, text, text_len);
        p += text_len;
    }
    HostlinkV.fcs_args.data = buf;
    HostlinkV.fcs_args.len = p;
    protocore_hostlink_fcs(work);
    uint8_t f = HostlinkV.value; // XOR over '@' .. end of text
    buf[p++] = hex_digit((uint8_t)(f >> 4));
    buf[p++] = hex_digit((uint8_t)(f & 0x0F));
    buf[p++] = '*';
    buf[p++] = '\r';
    buf[p] = '\0'; // NUL-terminate so callers may treat the ASCII frame as a string (matches protocore_sdi12_build)
    HostlinkV.n = p;
}

void protocore_hostlink_parse(uint8_t *restrict work)
{
    const char *buf = HostlinkV.parse_args.buf;
    size_t len = HostlinkV.parse_args.len;
    HostlinkFrame *out = HostlinkV.parse_args.out;

    // minimum: @ UU XX FF * CR = 1 + 2 + 2 + 2 + 1 + 1 = 9
    if (!buf || !out || len < 9)
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != '@' || buf[len - 1] != '\r' || buf[len - 2] != '*')
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    if (buf[1] < '0' || buf[1] > '9' || buf[2] < '0' || buf[2] > '9')
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }

    size_t fcs_pos = len - 4; // the two FCS chars precede '*' CR
    int hi = hex_val(buf[fcs_pos]);
    int lo = hex_val(buf[fcs_pos + 1]);
    if (hi < 0 || lo < 0)
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    uint8_t got = (uint8_t)((hi << 4) | lo);
    HostlinkV.fcs_args.data = buf;
    HostlinkV.fcs_args.len = fcs_pos;
    protocore_hostlink_fcs(work);
    if (HostlinkV.value != got) // XOR over '@' .. last text char
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }

    out->node = (uint8_t)((buf[1] - '0') * 10 + (buf[2] - '0'));
    out->header_code[0] = buf[3];
    out->header_code[1] = buf[4];
    out->header_code[2] = '\0';
    out->text = buf + 5;
    out->text_len = fcs_pos - 5;
    HostlinkV.ok = PROTO_TRUE;
}

void protocore_hostlink_end_code(uint8_t *restrict work)
{
    (void)work;
    const HostlinkFrame *f = HostlinkV.end_code_args.f;
    uint8_t *code = HostlinkV.end_code_args.code;

    if (!f || f->text_len < 2)
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    int hi = hex_val(f->text[0]);
    int lo = hex_val(f->text[1]);
    if (hi < 0 || lo < 0)
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    if (code)
    {
        *code = (uint8_t)((hi << 4) | lo);
    }
    HostlinkV.ok = PROTO_TRUE;
}

void protocore_hostlink_build_read(uint8_t *restrict work)
{
    char *buf = HostlinkV.build_read_args.buf;
    size_t cap = HostlinkV.build_read_args.cap;
    uint8_t node = HostlinkV.build_read_args.node;
    uint16_t address = HostlinkV.build_read_args.address;
    uint16_t count = HostlinkV.build_read_args.count;

    if (address > 9999 || count > 9999 || count == 0)
    {
        HostlinkV.n = 0;
        return;
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
    HostlinkV.build_args.buf = buf;
    HostlinkV.build_args.cap = cap;
    HostlinkV.build_args.node = node;
    HostlinkV.build_args.header_code = "RD";
    HostlinkV.build_args.text = text;
    HostlinkV.build_args.text_len = sizeof(text);
    protocore_hostlink_build(work);
}

void protocore_hostlink_read_word(uint8_t *restrict work)
{
    (void)work;
    const HostlinkFrame *f = HostlinkV.read_word_args.f;
    size_t index = HostlinkV.read_word_args.index;
    uint16_t *out = HostlinkV.read_word_args.out;

    if (!f || !out)
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    // A read response's text is the 2-char end code followed by 4-hex-char word values.
    size_t pos = 2 + index * 4;
    if (f->text_len < pos + 4)
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    int a = hex_val(f->text[pos]), b = hex_val(f->text[pos + 1]);
    int c = hex_val(f->text[pos + 2]), d = hex_val(f->text[pos + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0)
    {
        HostlinkV.ok = PROTO_FALSE;
        return;
    }
    *out = (uint16_t)((a << 12) | (b << 8) | (c << 4) | d);
    HostlinkV.ok = PROTO_TRUE;
}

void protocore_hostlink_build_write(uint8_t *restrict work)
{
    char *buf = HostlinkV.build_write_args.buf;
    size_t cap = HostlinkV.build_write_args.cap;
    uint8_t node = HostlinkV.build_write_args.node;
    uint16_t address = HostlinkV.build_write_args.address;
    const uint16_t *words = HostlinkV.build_write_args.words;
    size_t word_count = HostlinkV.build_write_args.word_count;

    if (!buf || node > 99 || address > 9999 || word_count == 0 || !words)
    {
        HostlinkV.n = 0;
        return;
    }
    if (word_count > cap / 4) // fail closed before word_count*4 could overflow / exceed the buffer
    {
        HostlinkV.n = 0;
        return;
    }
    // @ + UU + WR + addr(4) + word_count*4 hex + FCS(2) + *CR, plus a NUL terminator.
    size_t total = 1 + 2 + 2 + 4 + word_count * 4 + 2 + 2;
    if (total >= cap)
    {
        HostlinkV.n = 0;
        return;
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
    HostlinkV.fcs_args.data = buf;
    HostlinkV.fcs_args.len = p;
    protocore_hostlink_fcs(work);
    uint8_t f = HostlinkV.value; // XOR over '@' .. last data char
    buf[p++] = hex_digit((uint8_t)(f >> 4));
    buf[p++] = hex_digit((uint8_t)(f & 0x0F));
    buf[p++] = '*';
    buf[p++] = '\r';
    buf[p] = '\0'; // NUL-terminate so callers may treat the ASCII frame as a string
    HostlinkV.n = p;
}

/** @brief The operands and the outcome. */
HostlinkVars HostlinkV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HOSTLINK
