// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file haas_mdc.c
 * @brief Haas Machine Data Collection (MDC) Q-command codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HAAS_MDC

#include "services/machine_tool/haas_mdc/haas_mdc.h"

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder

PROTOCORE_BEGIN_DECLS

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
    // The false half is unreachable: parse_u32() is static and both call sites (protocore_haas_mdc_parse_status,
    // protocore_haas_mdc_parse_macro) always pass the address of a local, never NULL.
    if (out)
    {
        *out = v;
    }
    return PROTO_TRUE;
}

size_t protocore_haas_mdc_build_q(char *buf, size_t cap, uint16_t qnum)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    protocore_sb sb_q = {buf, cap, 0, PROTO_TRUE};
    protocore_sb_lit(&sb_q, "?Q");
    Sb.u32(&sb_q, qnum);
    Sb.ch(&sb_q, '\n'); // [NGC] Query Format: the query is "terminated with a new line"
    return Sb.finish(&sb_q);
}

size_t protocore_haas_mdc_build_var(char *buf, size_t cap, uint32_t var)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    protocore_sb sb_var = {buf, cap, 0, PROTO_TRUE};
    protocore_sb_lit(&sb_var, "?Q600 ");
    Sb.u32(&sb_var, var);
    Sb.ch(&sb_var, '\n'); // the ?Q600 line is a query in the ?Q### family, same sentence
    return Sb.finish(&sb_var);
}

proto_bool protocore_haas_mdc_parse(const char *buf, size_t len, HaasMdcResp *out)
{
    if (!buf || !out)
    {
        return PROTO_FALSE;
    }
    out->n_fields = 0;

    // Two published frames carry the same CSV payload, and this codec serves both transports.
    // [S143] RS-232: "<STX><CSV response><ETB><CR/LF><0x3E>" - the payload is strictly between STX
    // and the first following ETB. [NGC] Ethernet: "Responses from the control begin with > and end
    // with /r/n" - no STX, no ETB. The RS-232 window is looked for first, so a prompt and a CR/LF
    // left over from a previous reply sit outside it and belong to no field.
    const char *p = NULL;
    size_t plen = 0;

    size_t stx = 0;
    proto_bool have_stx = PROTO_FALSE;
    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] == PROTOCORE_HAAS_MDC_STX)
        {
            stx = i;
            have_stx = PROTO_TRUE;
            break;
        }
    }
    if (have_stx)
    {
        for (size_t i = stx + 1; i < len; i++)
        {
            if (buf[i] == PROTOCORE_HAAS_MDC_ETB)
            {
                p = buf + stx + 1;
                plen = i - stx - 1;
                break;
            }
        }
    }
    if (p == NULL)
    {
        // The NGC frame BEGINS with the prompt and ENDS with CR/LF, so both bound the payload the
        // same way STX and ETB do. A prompt anywhere else is the tail of the previous reply, and a
        // reply with no terminator yet is incomplete rather than empty.
        if (len < 2u || buf[0] != PROTOCORE_HAAS_MDC_PROMPT)
        {
            return PROTO_FALSE;
        }
        size_t end = 0;
        proto_bool have_end = PROTO_FALSE;
        for (size_t i = 1; i < len; i++)
        {
            if (buf[i] == '\r' || buf[i] == '\n')
            {
                end = i;
                have_end = PROTO_TRUE;
                break;
            }
        }
        if (!have_end)
        {
            return PROTO_FALSE;
        }
        p = buf + 1;
        plen = end - 1u;
    }

    // Split the CSV payload; each field trimmed of surrounding spaces. Extra fields past the cap drop.
    size_t start = 0;
    for (size_t i = 0; i <= plen; i++)
    {
        if (i == plen || p[i] == ',')
        {
            const char *f = p + start;
            size_t fl = i - start;
            trim(&f, &fl);
            if (out->n_fields < PROTOCORE_HAAS_MDC_MAX_FIELDS)
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

proto_bool protocore_haas_mdc_field(const HaasMdcResp *r, size_t idx, const char **p, size_t *l)
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

proto_bool protocore_haas_mdc_value(const HaasMdcResp *r, const char **p, size_t *l)
{
    return protocore_haas_mdc_field(r, 1, p, l);
}

proto_bool protocore_haas_mdc_is_error(const HaasMdcResp *r)
{
    return r && field_is(r, 0, "UNKNOWN");
}

proto_bool protocore_haas_mdc_parse_status(const HaasMdcResp *r, HaasMdcStatus *out)
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

proto_bool protocore_haas_mdc_parse_macro(const HaasMdcResp *r, uint32_t *var, const char **value, size_t *value_len)
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

proto_bool protocore_haas_mdc_dprnt_line(const char *buf, size_t len, const char **text, size_t *text_len)
{
    if (!buf || len == 0)
    {
        return PROTO_FALSE;
    }
    // A framed Q response carries an STX - not a DPRNT push.
    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] == PROTOCORE_HAAS_MDC_STX)
        {
            return PROTO_FALSE;
        }
    }

    const char *p = buf;
    size_t n = len;
    // Strip a leading prompt / newline / POPEN (DC2).
    while (n && (*p == (char)PROTOCORE_HAAS_MDC_PROMPT || *p == '\r' || *p == '\n' || *p == 0x12))
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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HAAS_MDC
