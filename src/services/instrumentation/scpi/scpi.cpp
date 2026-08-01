// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file scpi.cpp
 * @brief SCPI / IEEE 488.2 instrument-control codec (pure, host-tested).
 */

#include "services/instrumentation/scpi/scpi.h"
#include "shared_primitives/frame.h" // the one frame engine

#if PC_ENABLE_SCPI

#include <stdio.h> // snprintf (number formatting only; parsing is hand-rolled - no stdlib)
#include <string.h>

// A response value is one number. 10 significant digits is the SCPI NR2/NR3 rendering.
static const pc_field SCPI_REAL[] = {{PC_FK_G, 10, 0, nullptr}, PC_END};

// ── small helpers ──────────────────────────────────────────────────────────────────────────────

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// Case-insensitive equality of two byte ranges.
static bool ieq(const char *a, size_t alen, const char *b, size_t blen)
{
    if (alen != blen)
    {
        return false;
    }
    for (size_t i = 0; i < alen; i++)
    {
        if (lower(a[i]) != lower(b[i]))
        {
            return false;
        }
    }
    return true;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// ── common commands ────────────────────────────────────────────────────────────────────────────

const char *pc_scpi_common(ScpiCommon c)
{
    switch (c)
    {
    case ScpiCommon::SCPI_CLS:
        return "*CLS";
    case ScpiCommon::SCPI_ESE:
        return "*ESE";
    case ScpiCommon::SCPI_ESE_Q:
        return "*ESE?";
    case ScpiCommon::SCPI_ESR_Q:
        return "*ESR?";
    case ScpiCommon::SCPI_IDN_Q:
        return "*IDN?";
    case ScpiCommon::SCPI_OPC:
        return "*OPC";
    case ScpiCommon::SCPI_OPC_Q:
        return "*OPC?";
    case ScpiCommon::SCPI_RST:
        return "*RST";
    case ScpiCommon::SCPI_SRE:
        return "*SRE";
    case ScpiCommon::SCPI_SRE_Q:
        return "*SRE?";
    case ScpiCommon::SCPI_STB_Q:
        return "*STB?";
    case ScpiCommon::SCPI_TST_Q:
        return "*TST?";
    case ScpiCommon::SCPI_WAI:
        return "*WAI";
    }
    return "";
}

// ── command builder ────────────────────────────────────────────────────────────────────────────

size_t pc_scpi_build(char *buf, size_t cap, const char *header, const char *const *args, size_t argc)
{
    if (!buf || !header || (argc && !args))
    {
        return 0;
    }
    size_t hlen = strnlen(header, cap);
    if (hlen == 0 || hlen >= cap)
    {
        return 0;
    }

    size_t p = 0;
    memcpy(buf, header, hlen);
    p = hlen;
    for (size_t i = 0; i < argc; i++)
    {
        if (!args[i])
        {
            return 0;
        }
        char sep = (i == 0) ? ' ' : ',';
        size_t alen = strnlen(args[i], cap);
        if (p + 1 + alen + 1 >= cap) // sep + arg + the trailing '\n' + NUL
        {
            return 0;
        }
        buf[p++] = sep;
        memcpy(buf + p, args[i], alen);
        p += alen;
    }
    if (p + 1 >= cap) // room for '\n' + NUL
    {
        return 0;
    }
    buf[p++] = '\n';
    buf[p] = '\0';
    return p;
}

size_t pc_scpi_fmt_real(char *buf, size_t cap, double v)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    // %g renders NR2 (fixed) or NR3 (scientific) and trims trailing zeros - exactly the SCPI forms.
    // The frame's own contract is this function's contract: bytes written, or 0 with buf emptied.
    return pc_frame_build(buf, cap, SCPI_REAL, v);
}

// ── response parsers ───────────────────────────────────────────────────────────────────────────

// Fold the digit run at s[*i] into *acc, advancing *i past it. Returns how many digits were consumed.
static int scan_digits(const char *s, size_t len, size_t *i, double *acc)
{
    int n = 0;
    while (*i < len && is_digit(s[*i]))
    {
        *acc = *acc * 10.0 + (s[*i] - '0');
        n++;
        (*i)++;
    }
    return n;
}

// Optional E[+-]ddd suffix. Absent is not an error (*exp stays 0); false only for an E with no digits.
static bool scan_exponent(const char *s, size_t len, size_t *i, int *exp)
{
    *exp = 0;
    if (*i >= len || (s[*i] != 'e' && s[*i] != 'E'))
    {
        return true;
    }
    (*i)++;
    bool neg = false;
    if (*i < len && (s[*i] == '+' || s[*i] == '-'))
    {
        neg = (s[*i] == '-');
        (*i)++;
    }
    int digits = 0;
    while (*i < len && is_digit(s[*i]))
    {
        *exp = *exp * 10 + (s[*i] - '0');
        digits++;
        (*i)++;
    }
    if (digits == 0)
    {
        return false;
    }
    if (neg)
    {
        *exp = -*exp;
    }
    return true;
}

// Repeated multiply/divide rather than pow(): no libm dependency, and the digit counts here are small.
static double apply_scale(double v, int scale)
{
    for (int k = 0; k < scale; k++)
    {
        v *= 10.0;
    }
    for (int k = 0; k < -scale; k++)
    {
        v /= 10.0;
    }
    return v;
}

bool pc_scpi_parse_number(const char *s, size_t len, double *out)
{
    if (!s || !out || len == 0)
    {
        return false;
    }
    size_t i = 0;
    bool neg = false;
    if (s[i] == '+' || s[i] == '-')
    {
        neg = (s[i] == '-');
        i++;
    }
    double mant = 0.0;
    int digits = scan_digits(s, len, &i, &mant);
    int frac = 0; // number of fractional digits accumulated
    if (i < len && s[i] == '.')
    {
        i++;
        frac = scan_digits(s, len, &i, &mant);
        digits += frac;
    }
    if (digits == 0) // no mantissa digit on either side of the point
    {
        return false;
    }
    int exp = 0;
    if (!scan_exponent(s, len, &i, &exp))
    {
        return false;
    }
    if (i != len) // trailing junk -> not a clean numeric field
    {
        return false;
    }

    double val = apply_scale(mant, exp - frac); // decimal point and exponent applied together
    *out = neg ? -val : val;
    return true;
}

bool pc_scpi_parse_bool(const char *s, size_t len, bool *out)
{
    if (!s || !out || len == 0)
    {
        return false;
    }
    if (len == 1 && s[0] == '1')
    {
        *out = true;
        return true;
    }
    if (len == 1 && s[0] == '0')
    {
        *out = false;
        return true;
    }
    if (ieq(s, len, "ON", 2))
    {
        *out = true;
        return true;
    }
    if (ieq(s, len, "OFF", 3))
    {
        *out = false;
        return true;
    }
    return false;
}

size_t pc_scpi_parse_string(const char *s, size_t len, char *out, size_t cap)
{
    if (!s || !out || cap == 0 || len < 2)
    {
        return 0;
    }
    char q = s[0];
    if ((q != '"' && q != '\'') || s[len - 1] != q)
    {
        return 0;
    }
    size_t o = 0;
    size_t i = 1;
    while (i + 1 < len)
    {
        size_t adv = 1;
        if (s[i] == q)
        {
            // a doubled quote inside is one literal quote; anything else is a malformed close
            if (i + 2 < len && s[i + 1] == q)
            {
                adv = 2; // step over the pair, emitting the single quote below
            }
            else
            {
                return 0;
            }
        }
        if (o + 1 >= cap) // leave room for the NUL
        {
            return 0;
        }
        out[o++] = s[i];
        i += adv;
    }
    out[o] = '\0';
    return o;
}

bool pc_scpi_parse_block(const uint8_t *buf, size_t len, const uint8_t **data, size_t *data_len, size_t *consumed)
{
    if (!buf || !data || !data_len || !consumed || len < 2 || buf[0] != '#')
    {
        return false;
    }
    char n = (char)buf[1];
    if (n == '0') // indefinite: #0<data><NL with EOI> - data runs to the final newline
    {
        if (len < 3 || buf[len - 1] != '\n')
        {
            return false;
        }
        *data = buf + 2;
        *data_len = len - 3; // drop the leading "#0" and the trailing NL
        *consumed = len;
        return true;
    }
    if (n < '1' || n > '9') // definite: one nonzero digit giving the length-field width
    {
        return false;
    }
    size_t ndig = (size_t)(n - '0');
    if (len < 2 + ndig)
    {
        return false;
    }
    size_t dlen = 0;
    for (size_t i = 0; i < ndig; i++)
    {
        char c = (char)buf[2 + i];
        if (!is_digit(c))
        {
            return false;
        }
        dlen = dlen * 10 + (size_t)(c - '0');
    }
    size_t start = 2 + ndig;
    if (start + dlen > len)
    {
        return false;
    }
    *data = buf + start;
    *data_len = dlen;
    *consumed = start + dlen;
    return true;
}

// ── IEEE 488.2 / SCPI status model ─────────────────────────────────────────────────────────────

// The standard SCPI error/event messages (SCPI-99 Vol 2, "Error and Event Handling"). Only the
// common standard numbers are tabled; an unknown number yields "" (the app supplies its own text).
struct ScpiStdMsg
{
    int16_t number;
    const char *msg;
};
static const ScpiStdMsg SCPI_STD[] = {
    {0, "No error"},
    // -1xx Command errors (ESR bit CME)
    {-100, "Command error"},
    {-101, "Invalid character"},
    {-102, "Syntax error"},
    {-103, "Invalid separator"},
    {-104, "Data type error"},
    {-108, "Parameter not allowed"},
    {-109, "Missing parameter"},
    {-110, "Command header error"},
    {-111, "Header separator error"},
    {-112, "Program mnemonic too long"},
    {-113, "Undefined header"},
    {-114, "Header suffix out of range"},
    {-120, "Numeric data error"},
    {-128, "Numeric data not allowed"},
    {-148, "Character data not allowed"},
    {-150, "String data error"},
    {-158, "String data not allowed"},
    {-168, "Block data not allowed"},
    // -2xx Execution errors (ESR bit EXE)
    {-200, "Execution error"},
    {-220, "Parameter error"},
    {-221, "Settings conflict"},
    {-222, "Data out of range"},
    {-223, "Too much data"},
    {-224, "Illegal parameter value"},
    {-230, "Data corrupt or stale"},
    {-240, "Hardware error"},
    {-241, "Hardware missing"},
    // -3xx Device-specific errors (ESR bit DDE)
    {-300, "Device-specific error"},
    {-310, "System error"},
    {-311, "Memory error"},
    {-313, "Calibration memory lost"},
    {-314, "Save/recall memory lost"},
    {-315, "Configuration memory lost"},
    {-321, "Out of memory"},
    {-330, "Self-test failed"},
    {-350, "Queue overflow"},
    {-360, "Communication error"},
    {-363, "Input buffer overrun"},
    // -4xx Query errors (ESR bit QYE)
    {-400, "Query error"},
    {-410, "Query INTERRUPTED"},
    {-420, "Query UNTERMINATED"},
    {-430, "Query DEADLOCKED"},
    {-440, "Query UNTERMINATED after indefinite response"},
};

const char *pc_scpi_std_error(int16_t number)
{
    for (size_t i = 0; i < sizeof(SCPI_STD) / sizeof(SCPI_STD[0]); i++)
    {
        if (SCPI_STD[i].number == number)
        {
            return SCPI_STD[i].msg;
        }
    }
    return "";
}

void pc_scpi_status_init(ScpiStatus *s)
{
    if (!s)
    {
        return;
    }
    memset(s, 0, sizeof(*s));
}

void pc_scpi_event(ScpiStatus *s, uint8_t esr_bits)
{
    if (s)
    {
        s->esr |= esr_bits;
    }
}

// Map an error/event number to the ESR bit it latches, per SCPI-99 Vol 2 §21.8 class ranges:
// -1xx CME, -2xx EXE, -3xx (and positive device-specific) DDE, -4xx QYE, -5xx PON, -6xx URQ,
// -7xx RQC, -8xx OPC. 0 = No error latches nothing.
static uint8_t esr_bit_for(int16_t number)
{
    if (number >= 0)
    {
        return number == 0 ? 0 : SCPI_ESR_DDE; // GCOVR_EXCL_BR_LINE  ==0 arm unreachable: pc_scpi_push_error (only
                                               // caller) already returns on number==0 before calling this
    }
    if (number > -200)
    {
        return SCPI_ESR_CME;
    }
    if (number > -300)
    {
        return SCPI_ESR_EXE;
    }
    if (number > -400)
    {
        return SCPI_ESR_DDE;
    }
    if (number > -500)
    {
        return SCPI_ESR_QYE;
    }
    if (number > -600)
    {
        return SCPI_ESR_PON;
    }
    if (number > -700)
    {
        return SCPI_ESR_URQ;
    }
    if (number > -800)
    {
        return SCPI_ESR_RQC;
    }
    if (number > -900)
    {
        return SCPI_ESR_OPC;
    }
    return 0;
}

void pc_scpi_push_error(ScpiStatus *s, int16_t number, const char *msg)
{
    if (!s || number == 0)
    {
        return;
    }
    if (!msg)
    {
        msg = pc_scpi_std_error(number);
    }
    s->esr |= esr_bit_for(number);
    if (s->count >= PC_SCPI_ERR_QUEUE)
    {
        // Overflow: the most recent entry becomes -350 "Queue overflow" (SCPI rule); latch DDE.
        uint8_t tail = (uint8_t)((s->head + s->count - 1) % PC_SCPI_ERR_QUEUE);
        s->queue[tail].number = -350;
        s->queue[tail].msg = pc_scpi_std_error(-350);
        s->esr |= SCPI_ESR_DDE;
        return;
    }
    uint8_t slot = (uint8_t)((s->head + s->count) % PC_SCPI_ERR_QUEUE);
    s->queue[slot].number = number;
    s->queue[slot].msg = msg;
    s->count++;
}

bool pc_scpi_pop_error(ScpiStatus *s, ScpiError *out)
{
    if (!out)
    {
        return false;
    }
    if (!s || s->count == 0)
    {
        out->number = 0;
        out->msg = "No error";
        return false;
    }
    *out = s->queue[s->head];
    s->head = (uint8_t)((s->head + 1) % PC_SCPI_ERR_QUEUE);
    s->count--;
    return true;
}

uint8_t pc_scpi_stb(const ScpiStatus *s)
{
    if (!s)
    {
        return 0;
    }
    uint8_t stb = (uint8_t)(s->summary & (SCPI_STB_QSB | SCPI_STB_MAV | SCPI_STB_OSB));
    if (s->count)
    {
        stb |= SCPI_STB_EAV;
    }
    if (s->esr & s->ese)
    {
        stb |= SCPI_STB_ESB;
    }
    // MSS = OR of (STB & SRE) over every bit except bit 6 (it cannot summarize itself).
    if (stb & s->sre & (uint8_t)~SCPI_STB_MSS)
    {
        stb |= SCPI_STB_MSS;
    }
    return stb;
}

void pc_scpi_cls(ScpiStatus *s)
{
    if (!s)
    {
        return;
    }
    s->esr = 0;
    s->head = 0;
    s->count = 0;
}

// ── SCPI short/long-form header matcher ────────────────────────────────────────────────────────

// Parse a decimal numeric suffix; empty defaults to 1 (SCPI numeric-suffix rule). -1 on a non-digit.
static int suffix_val(const char *s, size_t len)
{
    if (len == 0)
    {
        return 1;
    }
    int v = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (!is_digit(s[i]))
        {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// Match one header node: input [i,ilen) vs pattern [p,plen) (uppercase run = short form).
static bool match_node(const char *i, size_t ilen, const char *p, size_t plen)
{
    if (ilen == 0 || plen == 0)
    {
        return false;
    }
    // pattern alpha length + short-form (uppercase-prefix) length
    size_t palpha = 0;
    while (palpha < plen && is_alpha(p[palpha]))
    {
        palpha++;
    }
    size_t pshort = 0;
    // ">= 'A'" false is unreachable: every byte here already passed is_alpha() in the palpha loop above,
    // and both alpha ranges ('A'-'Z', 'a'-'z') satisfy >= 'A'; the branch data for a multi-line "&&" chain
    // lands on the line each sub-test's operator sits on, so the marker must sit on this line, not the
    // "<= 'Z'" continuation below, to actually take effect.
    while (pshort < palpha && p[pshort] >= 'A' && // GCOVR_EXCL_BR_LINE
           p[pshort] <= 'Z')
    {
        pshort++;
    }
    // input alpha length
    size_t ialpha = 0;
    while (ialpha < ilen && is_alpha(i[ialpha]))
    {
        ialpha++;
    }
    // the input alpha must equal the short form OR the whole long form (case-insensitive)
    if (!ieq(i, ialpha, p, pshort) && !ieq(i, ialpha, p, palpha))
    {
        return false;
    }
    // numeric suffix (rest of each node) - omitted defaults to 1
    int pv = suffix_val(p + palpha, plen - palpha);
    int iv = suffix_val(i + ialpha, ilen - ialpha);
    return pv >= 0 && iv >= 0 && pv == iv;
}

bool pc_scpi_match(const char *input, size_t input_len, const char *pattern)
{
    if (!input || !pattern)
    {
        return false;
    }
    // clip the input to its header (everything before the first space)
    size_t hlen = 0;
    while (hlen < input_len && input[hlen] != ' ')
    {
        hlen++;
    }
    const char *ip = input;
    size_t irem = hlen;

    // common command: match the whole token case-insensitively
    if (pattern[0] == '*')
    {
        return ieq(ip, irem, pattern, strnlen(pattern, 64));
    }

    // a leading ':' on the input is an absolute-root anchor - skip it
    if (irem && ip[0] == ':')
    {
        ip++;
        irem--;
    }

    size_t prem = strnlen(pattern, 256);
    const char *pp = pattern;

    // reconcile the query '?' suffix: both must have it or neither
    bool pq = prem && pp[prem - 1] == '?';
    bool iq = irem && ip[irem - 1] == '?';
    if (pq != iq)
    {
        return false;
    }
    if (pq)
    {
        prem--;
    }
    if (iq)
    {
        irem--;
    }

    // walk the ':'-separated nodes in lockstep
    while (true)
    {
        size_t pn = 0;
        while (pn < prem && pp[pn] != ':')
        {
            pn++;
        }
        size_t in = 0;
        while (in < irem && ip[in] != ':')
        {
            in++;
        }
        if (!match_node(ip, in, pp, pn))
        {
            return false;
        }
        // advance past this node (and its ':' if present)
        bool p_more = pn < prem;
        bool i_more = in < irem;
        if (p_more != i_more) // different depth
        {
            return false;
        }
        if (!p_more) // both exhausted -> full match
        {
            return true;
        }
        pp += pn + 1;
        prem -= pn + 1;
        ip += in + 1;
        irem -= in + 1;
    }
}

#endif // PC_ENABLE_SCPI
