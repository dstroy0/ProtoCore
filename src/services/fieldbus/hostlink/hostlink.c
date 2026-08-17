// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hostlink.c
 * @brief Omron Host Link (C-mode) frame builder + parser (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HOSTLINK

#include "mmgr/protomem.h"
#include "services/fieldbus/hostlink/hostlink.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void hostlink_build(uint8_t *restrict work);
static void hostlink_fcs(uint8_t *restrict work);

static void hostlink_fcs(uint8_t *restrict work)
{
    (void)work;
    const char *data = Hostlink.fcs_args.data;
    size_t len = Hostlink.fcs_args.len;

    uint8_t f = 0;
    for (size_t i = 0; i < len; i++)
    {
        f ^= (uint8_t)data[i];
    }
    Hostlink.value = f;
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

static void hostlink_build(uint8_t *restrict work)
{
    char *buf = Hostlink.build_args.buf;
    size_t cap = Hostlink.build_args.cap;
    uint8_t node = Hostlink.build_args.node;
    const char *header_code = Hostlink.build_args.header_code;
    const char *text = Hostlink.build_args.text;
    size_t text_len = Hostlink.build_args.text_len;

    if (!buf || !header_code || node > 99 || (text_len && !text))
    {
        Hostlink.n = 0;
        return;
    }
    if (header_code[0] == '\0' || header_code[1] == '\0') // need exactly 2 header characters
    {
        Hostlink.n = 0;
        return;
    }
    size_t total = 1 + 2 + 2 + text_len + 2 + 2; // @ + UU + XX + text + FCS + *CR
    if (total >= cap)                            // need room for the frame + a NUL terminator
    {
        Hostlink.n = 0;
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
    Hostlink.fcs_args.data = buf;
    Hostlink.fcs_args.len = p;
    hostlink_fcs(work);
    uint8_t f = Hostlink.value; // XOR over '@' .. end of text
    buf[p++] = hex_digit((uint8_t)(f >> 4));
    buf[p++] = hex_digit((uint8_t)(f & 0x0F));
    buf[p++] = '*';
    buf[p++] = '\r';
    buf[p] = '\0'; // NUL-terminate so callers may treat the ASCII frame as a string (matches protocore_sdi12_build)
    Hostlink.n = p;
}

static void hostlink_parse(uint8_t *restrict work)
{
    const char *buf = Hostlink.parse_args.buf;
    size_t len = Hostlink.parse_args.len;
    HostlinkFrame *out = Hostlink.parse_args.out;

    // minimum: @ UU XX FF * CR = 1 + 2 + 2 + 2 + 1 + 1 = 9
    if (!buf || !out || len < 9)
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != '@' || buf[len - 1] != '\r' || buf[len - 2] != '*')
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    if (buf[1] < '0' || buf[1] > '9' || buf[2] < '0' || buf[2] > '9')
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }

    size_t fcs_pos = len - 4; // the two FCS chars precede '*' CR
    int hi = hex_val(buf[fcs_pos]);
    int lo = hex_val(buf[fcs_pos + 1]);
    if (hi < 0 || lo < 0)
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    uint8_t got = (uint8_t)((hi << 4) | lo);
    Hostlink.fcs_args.data = buf;
    Hostlink.fcs_args.len = fcs_pos;
    hostlink_fcs(work);
    if (Hostlink.value != got) // XOR over '@' .. last text char
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }

    out->node = (uint8_t)((buf[1] - '0') * 10 + (buf[2] - '0'));
    out->header_code[0] = buf[3];
    out->header_code[1] = buf[4];
    out->header_code[2] = '\0';
    out->text = buf + 5;
    out->text_len = fcs_pos - 5;
    Hostlink.ok = PROTO_TRUE;
}

static void hostlink_end_code(uint8_t *restrict work)
{
    (void)work;
    const HostlinkFrame *f = Hostlink.end_code_args.f;
    uint8_t *code = Hostlink.end_code_args.code;

    if (!f || f->text_len < 2)
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    int hi = hex_val(f->text[0]);
    int lo = hex_val(f->text[1]);
    if (hi < 0 || lo < 0)
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    if (code)
    {
        *code = (uint8_t)((hi << 4) | lo);
    }
    Hostlink.ok = PROTO_TRUE;
}

static void hostlink_build_read(uint8_t *restrict work)
{
    char *buf = Hostlink.build_read_args.buf;
    size_t cap = Hostlink.build_read_args.cap;
    uint8_t node = Hostlink.build_read_args.node;
    uint16_t address = Hostlink.build_read_args.address;
    uint16_t count = Hostlink.build_read_args.count;

    if (address > 9999 || count > 9999 || count == 0)
    {
        Hostlink.n = 0;
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
    Hostlink.build_args.buf = buf;
    Hostlink.build_args.cap = cap;
    Hostlink.build_args.node = node;
    Hostlink.build_args.header_code = "RD";
    Hostlink.build_args.text = text;
    Hostlink.build_args.text_len = sizeof(text);
    hostlink_build(work);
}

static void hostlink_read_word(uint8_t *restrict work)
{
    (void)work;
    const HostlinkFrame *f = Hostlink.read_word_args.f;
    size_t index = Hostlink.read_word_args.index;
    uint16_t *out = Hostlink.read_word_args.out;

    if (!f || !out)
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    // A read response's text is the 2-char end code followed by 4-hex-char word values.
    size_t pos = 2 + index * 4;
    if (f->text_len < pos + 4)
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    int a = hex_val(f->text[pos]), b = hex_val(f->text[pos + 1]);
    int c = hex_val(f->text[pos + 2]), d = hex_val(f->text[pos + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0)
    {
        Hostlink.ok = PROTO_FALSE;
        return;
    }
    *out = (uint16_t)((a << 12) | (b << 8) | (c << 4) | d);
    Hostlink.ok = PROTO_TRUE;
}

static void hostlink_build_write(uint8_t *restrict work)
{
    char *buf = Hostlink.build_write_args.buf;
    size_t cap = Hostlink.build_write_args.cap;
    uint8_t node = Hostlink.build_write_args.node;
    uint16_t address = Hostlink.build_write_args.address;
    const uint16_t *words = Hostlink.build_write_args.words;
    size_t word_count = Hostlink.build_write_args.word_count;

    if (!buf || node > 99 || address > 9999 || word_count == 0 || !words)
    {
        Hostlink.n = 0;
        return;
    }
    if (word_count > cap / 4) // fail closed before word_count*4 could overflow / exceed the buffer
    {
        Hostlink.n = 0;
        return;
    }
    // @ + UU + WR + addr(4) + word_count*4 hex + FCS(2) + *CR, plus a NUL terminator.
    size_t total = 1 + 2 + 2 + 4 + word_count * 4 + 2 + 2;
    if (total >= cap)
    {
        Hostlink.n = 0;
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
    Hostlink.fcs_args.data = buf;
    Hostlink.fcs_args.len = p;
    hostlink_fcs(work);
    uint8_t f = Hostlink.value; // XOR over '@' .. last data char
    buf[p++] = hex_digit((uint8_t)(f >> 4));
    buf[p++] = hex_digit((uint8_t)(f & 0x0F));
    buf[p++] = '*';
    buf[p++] = '\r';
    buf[p] = '\0'; // NUL-terminate so callers may treat the ASCII frame as a string
    Hostlink.n = p;
}

HostlinkNs Hostlink = {.fcs = hostlink_fcs,
                       .build = hostlink_build,
                       .parse = hostlink_parse,
                       .end_code = hostlink_end_code,
                       .build_read = hostlink_build_read,
                       .read_word = hostlink_read_word,
                       .build_write = hostlink_build_write};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HOSTLINK
