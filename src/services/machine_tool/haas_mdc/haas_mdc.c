// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file haas_mdc.c
 * @brief Haas Machine Data Collection (MDC) Q-command codec (pure, host-tested).
 */

#include "services/machine_tool/haas_mdc/haas_mdc.h"

#if PC_ENABLE_HAAS_MDC

#include "mmgr/membuild.h" // pc_sb frame builder

// Trim leading and trailing spaces from [s, s+len); updates s and len in place.
static void trim(const char **s, size_t *len)
{
    const char *p = *s;
    size_t n = *len;
    while (n && p[n - 1] == ' ')
    {
        n--;
    }
    while (n && *p == ' ')
    {
        p++;
        n--;
    }
    *s = p;
    *len = n;
}

// Compare a parsed field to a NUL-terminated literal without <string.h> / strlen.
static proto_bool field_is(const HaasMdcResp *r, size_t idx, const char *lit)
{
    if (idx >= r->n_fields)
    {
        return PROTO_FALSE;
    }
    const char *f = r->field[idx];
    size_t fl = r->field_len[idx];
    size_t i = 0;
    for (; i < fl; i++)
    {
        if (lit[i] == '\0' || f[i] != lit[i])
        {
            return PROTO_FALSE;
        }
    }
    return lit[i] == '\0'; // both ended together
}

// Hand-rolled unsigned decimal parse of a (space-trimmed) field; false unless all digits.
static proto_bool parse_u32(const char *s, size_t len, uint32_t *out)
{
    trim(&s, &len);
    if (len == 0)
    {
        return PROTO_FALSE;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return PROTO_FALSE;
        }
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    // The false half is unreachable: parse_u32() is static and both call sites (pc_haas_mdc_parse_status,
    // pc_haas_mdc_parse_macro) always pass the address of a local, never NULL.
    if (out) // GCOVR_EXCL_BR_LINE  out == nullptr half unreachable, see above
    {
        *out = v;
    }
    return PROTO_TRUE;
}

size_t pc_haas_mdc_build_q(char *buf, size_t cap, uint16_t qnum)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    pc_sb sb_q = {buf, cap, 0, PROTO_TRUE};
    pc_sb_lit(&sb_q, "?Q");
    pc_sb_u32(&sb_q, qnum);
    pc_sb_ch(&sb_q, '\r');
    return pc_sb_finish(&sb_q);
}

size_t pc_haas_mdc_build_var(char *buf, size_t cap, uint32_t var)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    pc_sb sb_var = {buf, cap, 0, PROTO_TRUE};
    pc_sb_lit(&sb_var, "?Q600 ");
    pc_sb_u32(&sb_var, var);
    pc_sb_ch(&sb_var, '\r');
    return pc_sb_finish(&sb_var);
}

proto_bool pc_haas_mdc_parse(const char *buf, size_t len, HaasMdcResp *out)
{
    if (!buf || !out)
    {
        return PROTO_FALSE;
    }
    out->n_fields = 0;

    // Locate the payload strictly between STX and the first following ETB (scan, never by offset).
    size_t stx = 0;
    proto_bool have_stx = PROTO_FALSE;
    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] == PC_HAAS_MDC_STX)
        {
            stx = i;
            have_stx = PROTO_TRUE;
            break;
        }
    }
    if (!have_stx)
    {
        return PROTO_FALSE;
    }
    size_t etb = 0;
    proto_bool have_etb = PROTO_FALSE;
    for (size_t i = stx + 1; i < len; i++)
    {
        if (buf[i] == PC_HAAS_MDC_ETB)
        {
            etb = i;
            have_etb = PROTO_TRUE;
            break;
        }
    }
    if (!have_etb)
    {
        return PROTO_FALSE;
    }

    const char *p = buf + stx + 1;
    size_t plen = etb - stx - 1;

    // Split the CSV payload; each field trimmed of surrounding spaces. Extra fields past the cap drop.
    size_t start = 0;
    for (size_t i = 0; i <= plen; i++)
    {
        if (i == plen || p[i] == ',')
        {
            const char *f = p + start;
            size_t fl = i - start;
            trim(&f, &fl);
            if (out->n_fields < PC_HAAS_MDC_MAX_FIELDS)
            {
                out->field[out->n_fields] = f;
                out->field_len[out->n_fields] = fl;
                out->n_fields++;
            }
            start = i + 1;
        }
    }
    return out->n_fields > 0;
}

proto_bool pc_haas_mdc_field(const HaasMdcResp *r, size_t idx, const char **p, size_t *l)
{
    if (!r || idx >= r->n_fields)
    {
        return PROTO_FALSE;
    }
    if (p)
    {
        *p = r->field[idx];
    }
    if (l)
    {
        *l = r->field_len[idx];
    }
    return PROTO_TRUE;
}

proto_bool pc_haas_mdc_value(const HaasMdcResp *r, const char **p, size_t *l)
{
    return pc_haas_mdc_field(r, 1, p, l);
}

proto_bool pc_haas_mdc_is_error(const HaasMdcResp *r)
{
    return r && field_is(r, 0, "UNKNOWN");
}

proto_bool pc_haas_mdc_parse_status(const HaasMdcResp *r, HaasMdcStatus *out)
{
    if (!r || !out)
    {
        return PROTO_FALSE;
    }
    out->busy = PROTO_FALSE;
    out->program = NULL;
    out->program_len = 0;
    out->status = NULL;
    out->status_len = 0;
    out->parts = 0;
    out->parts_valid = PROTO_FALSE;

    if (field_is(r, 0, "STATUS"))
    {
        // Busy collapse: `STATUS, BUSY`.
        out->busy = PROTO_TRUE;
        if (r->n_fields >= 2)
        {
            out->status = r->field[1];
            out->status_len = r->field_len[1];
        }
        return PROTO_TRUE;
    }
    if (field_is(r, 0, "PROGRAM") && r->n_fields >= 3)
    {
        // `PROGRAM, Oxxxxx, <status>, PARTS, n`.
        out->program = r->field[1];
        out->program_len = r->field_len[1];
        out->status = r->field[2];
        out->status_len = r->field_len[2];
        if (r->n_fields >= 5)
        {
            uint32_t n = 0;
            if (parse_u32(r->field[4], r->field_len[4], &n))
            {
                out->parts = n;
                out->parts_valid = PROTO_TRUE;
            }
        }
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

proto_bool pc_haas_mdc_parse_macro(const HaasMdcResp *r, uint32_t *var, const char **value, size_t *value_len)
{
    if (!r || r->n_fields < 3 || !field_is(r, 0, "MACRO"))
    {
        return PROTO_FALSE;
    }
    uint32_t v = 0;
    if (!parse_u32(r->field[1], r->field_len[1], &v))
    {
        return PROTO_FALSE;
    }
    if (var)
    {
        *var = v;
    }
    if (value)
    {
        *value = r->field[2];
    }
    if (value_len)
    {
        *value_len = r->field_len[2];
    }
    return PROTO_TRUE;
}

proto_bool pc_haas_mdc_dprnt_line(const char *buf, size_t len, const char **text, size_t *text_len)
{
    if (!buf || len == 0)
    {
        return PROTO_FALSE;
    }
    // A framed Q response carries an STX - not a DPRNT push.
    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] == PC_HAAS_MDC_STX)
        {
            return PROTO_FALSE;
        }
    }

    const char *p = buf;
    size_t n = len;
    // Strip a leading prompt / newline / POPEN (DC2).
    while (n && (*p == (char)PC_HAAS_MDC_PROMPT || *p == '\r' || *p == '\n' || *p == 0x12))
    {
        p++;
        n--;
    }
    // Strip trailing CR / LF / PCLOS (DC4). Interior spaces are preserved (a DPRNT `*` is a space).
    while (n && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == 0x14))
    {
        n--;
    }
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    if (text)
    {
        *text = p;
    }
    if (text_len)
    {
        *text_len = n;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_HAAS_MDC
