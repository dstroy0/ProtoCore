// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dnc.c
 * @brief CNC RS-232 DNC drip-feed codec implementation (see dnc.h).
 */

#include "dnc.h"

#if PC_ENABLE_DNC

// EIA RS-244 punched-tape code. One source of truth for both translation directions.
// Each EIA byte is the 8-track hole pattern (bit 0 = channel 1 .. bit 7 = channel 8) with
// odd parity in channel 5 (0x10): every entry has an odd number of set bits. The digit and
// letter values follow the standard zone (6+7 / 7 / 6) + BCD-digit + parity construction and
// are individually parity-checked by the host tests.
typedef struct
{
    char ascii;
    uint8_t eia;
} DncEiaPair;

static const DncEiaPair DNC_EIA_MAP[] = {
    // digits (BCD in channels 1-4; 0 is channel 6)
    {'0', 0x20},
    {'1', 0x01},
    {'2', 0x02},
    {'3', 0x13},
    {'4', 0x04},
    {'5', 0x15},
    {'6', 0x16},
    {'7', 0x07},
    {'8', 0x08},
    {'9', 0x19},
    // A-I (channels 6+7 zone + digit 1-9)
    {'A', 0x61},
    {'B', 0x62},
    {'C', 0x73},
    {'D', 0x64},
    {'E', 0x75},
    {'F', 0x76},
    {'G', 0x67},
    {'H', 0x68},
    {'I', 0x79},
    // J-R (channel 7 zone + digit 1-9)
    {'J', 0x51},
    {'K', 0x52},
    {'L', 0x43},
    {'M', 0x54},
    {'N', 0x45},
    {'O', 0x46},
    {'P', 0x57},
    {'Q', 0x58},
    {'R', 0x49},
    // S-Z (channel 6 zone + digit 2-9)
    {'S', 0x32},
    {'T', 0x23},
    {'U', 0x34},
    {'V', 0x25},
    {'W', 0x26},
    {'X', 0x37},
    {'Y', 0x38},
    {'Z', 0x29},
    // NC punctuation + controls (EIA has no lowercase, no ':' '(' ')')
    {' ', 0x10},
    {'.', 0x6B},
    {'-', 0x40},
    {'+', 0x70},
    {'/', 0x31},
    {'\t', 0x3E},
    // the '%' rewind-stop is EIA End-of-Record
    {'%', (uint8_t)DNC_EIA_EOR},
};

static const size_t DNC_EIA_MAP_LEN = sizeof(DNC_EIA_MAP) / sizeof(DNC_EIA_MAP[0]);

uint8_t pc_dnc_iso_to_eia(char c)
{
    for (size_t i = 0; i < DNC_EIA_MAP_LEN; i++)
    {
        if (DNC_EIA_MAP[i].ascii == c)
        {
            return DNC_EIA_MAP[i].eia;
        }
    }
    return 0xFF; // no EIA representation - fail closed
}

char pc_dnc_eia_to_iso(uint8_t b)
{
    for (size_t i = 0; i < DNC_EIA_MAP_LEN; i++)
    {
        if (DNC_EIA_MAP[i].eia == b)
        {
            return DNC_EIA_MAP[i].ascii;
        }
    }
    return 0; // unknown EIA code (e.g. blank / runout)
}

uint8_t pc_dnc_iso_add_parity(uint8_t ascii7)
{
    uint8_t v = ascii7 & 0x7F;
    uint8_t x = v;
    uint8_t p = 0;
    while (x) // p = 1 when v has an odd number of set bits
    {
        p ^= 1;
        x &= (uint8_t)(x - 1);
    }
    return (uint8_t)(v | (p ? 0x80 : 0x00)); // even parity: set bit 7 to make the total even
}

void pc_dnc_flow_init(DncFlow *f)
{
    f->paused = PROTO_FALSE;
}

proto_bool pc_dnc_flow_feed(DncFlow *f, uint8_t rx)
{
    if (rx == (uint8_t)DNC_XOFF)
    {
        f->paused = PROTO_TRUE;
        return PROTO_TRUE;
    }
    if (rx == (uint8_t)DNC_XON)
    {
        f->paused = PROTO_FALSE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// Append one End-of-Block to out[n..]; returns the new count or 0 on overflow.
static size_t dnc_put_eob(const DncCfg *cfg, uint8_t *out, size_t cap, size_t n)
{
    if (cfg->code == DNC_CODE_EIA)
    {
        if (n >= cap)
        {
            return 0;
        }
        out[n++] = (uint8_t)DNC_EIA_EOB;
        return n;
    }
    if (cfg->crlf)
    {
        uint8_t cr = cfg->even_parity ? pc_dnc_iso_add_parity(0x0D) : 0x0D;
        if (n >= cap)
        {
            return 0;
        }
        out[n++] = cr;
    }
    uint8_t lf = cfg->even_parity ? pc_dnc_iso_add_parity(0x0A) : 0x0A;
    if (n >= cap)
    {
        return 0;
    }
    out[n++] = lf;
    return n;
}

size_t pc_dnc_encode_block(const DncCfg *cfg, const char *line, size_t line_len, uint8_t *out, size_t out_cap)
{
    size_t n = 0;
    for (size_t i = 0; i < line_len; i++)
    {
        uint8_t b;
        if (cfg->code == DNC_CODE_EIA)
        {
            uint8_t e = pc_dnc_iso_to_eia(line[i]);
            if (e == 0xFF)
            {
                return 0; // non-representable character - fail closed
            }
            b = e;
        }
        else
        {
            b = (uint8_t)line[i] & 0x7F;
            if (cfg->even_parity)
            {
                b = pc_dnc_iso_add_parity(b);
            }
        }
        if (n >= out_cap)
        {
            return 0;
        }
        out[n++] = b;
    }
    return dnc_put_eob(cfg, out, out_cap, n);
}

size_t pc_dnc_encode_marker(const DncCfg *cfg, uint8_t *out, size_t out_cap)
{
    size_t n = 0;
    if (cfg->code == DNC_CODE_EIA)
    {
        if (n >= out_cap)
        {
            return 0;
        }
        out[n++] = (uint8_t)DNC_EIA_EOR;
    }
    else
    {
        uint8_t pct = cfg->even_parity ? pc_dnc_iso_add_parity(0x25) : 0x25;
        if (n >= out_cap)
        {
            return 0;
        }
        out[n++] = pct;
    }
    return dnc_put_eob(cfg, out, out_cap, n);
}

size_t pc_dnc_encode_leader(const DncCfg *cfg, uint8_t *out, size_t out_cap)
{
    uint16_t n = cfg->leader_len;
    if ((size_t)n > out_cap)
    {
        return 0;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        out[i] = 0x00; // NUL runout - skipped by the reader until the first '%'
    }
    return n;
}

void pc_dnc_decode_init(DncDecoder *d, DncCode code)
{
    d->code = code;
    d->len = 0;
    d->overflow = PROTO_FALSE;
    d->in_program = PROTO_FALSE;
    d->line_ready = PROTO_FALSE;
    d->line[0] = 0;
}

DncEvent pc_dnc_decode_feed(DncDecoder *d, uint8_t wire)
{
    // The line delivered by the previous call is now consumed; start fresh.
    if (d->line_ready)
    {
        d->line_ready = PROTO_FALSE;
        d->len = 0;
        d->line[0] = 0;
    }

    // Note: XON/XOFF are NOT filtered here. Flow control rides the reverse channel
    // (controller -> sender), handled by pc_dnc_flow_feed; this decodes the forward program
    // stream, where 0x13 is the EIA data character '3', not DC3.
    proto_bool is_eob = PROTO_FALSE;
    proto_bool is_marker = PROTO_FALSE;
    uint8_t ascii = 0;

    if (d->code == DNC_CODE_EIA)
    {
        if (wire == (uint8_t)DNC_EIA_EOB)
        {
            is_eob = PROTO_TRUE;
        }
        else if (wire == (uint8_t)DNC_EIA_EOR)
        {
            is_marker = PROTO_TRUE;
        }
        else
        {
            ascii = (uint8_t)pc_dnc_eia_to_iso(wire); // 0 for blank/runout/unknown -> ignored
        }
    }
    else
    {
        uint8_t v = wire & 0x7F; // strip even parity
        if (v == '\n')
        {
            is_eob = PROTO_TRUE;
        }
        else if (v == '%')
        {
            is_marker = PROTO_TRUE;
        }
        else if (v == '\r' || v == 0x00 || v == (uint8_t)DNC_EIA_DEL)
        {
            ascii = 0; // CR / NUL / DEL runout - ignored
        }
        else
        {
            ascii = v;
        }
    }

    if (is_marker)
    {
        d->len = 0; // a marker stands alone; discard any partial block
        d->overflow = PROTO_FALSE;
        d->line[0] = 0;
        if (!d->in_program)
        {
            d->in_program = PROTO_TRUE;
            return DNC_EV_PROG_START;
        }
        d->in_program = PROTO_FALSE;
        return DNC_EV_PROG_END;
    }

    if (is_eob)
    {
        if (d->overflow)
        {
            d->overflow = PROTO_FALSE;
            d->len = 0;
            d->line[0] = 0;
            return DNC_EV_OVERFLOW;
        }
        if (d->len == 0)
        {
            return DNC_EV_NONE; // blank block (leader / CR LF pair) - nothing to report
        }
        d->line[d->len] = 0;
        d->line_ready = PROTO_TRUE; // keep line/len valid for the caller until the next feed
        return DNC_EV_LINE;
    }

    if (ascii == 0)
    {
        return DNC_EV_NONE; // ignored character
    }

    if (d->overflow)
    {
        return DNC_EV_NONE; // dropping the rest of an over-long block until its EOB
    }
    if (d->len >= PC_DNC_LINE_MAX)
    {
        d->overflow = PROTO_TRUE;
        return DNC_EV_NONE;
    }
    d->line[d->len++] = (char)ascii;
    return DNC_EV_NONE;
}

#endif // PC_ENABLE_DNC
