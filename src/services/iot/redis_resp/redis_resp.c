// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file redis_resp.c
 * @brief Redis RESP2 command encoder + reply parser (pure, host-tested).
 */

#include "services/iot/redis_resp/redis_resp.h"

#if PC_ENABLE_REDIS

#include <string.h>

// Write a "<tag><decimal>\r\n" length prefix into buf at *pos, advancing it. A hand-rolled decimal is
// several times faster than snprintf on the ESP32-S3, where this was the dominant cost of encoding a
// command (docs/FEATURE_PERFORMANCE.md section 4). Reserves a trailing byte for the final NUL, as before.
static proto_bool put_len_prefix(char *buf, size_t cap, size_t *pos, char tag, size_t n)
{
    char tmp[20];
    int t = 0;
    if (n == 0)
    {
        tmp[t++] = '0';
    }
    while (n)
    {
        tmp[t++] = (char)('0' + (n % 10));
        n /= 10;
    }
    if (*pos + 1u + (size_t)t + 2u >= cap)
    {
        return PROTO_FALSE;
    }
    buf[(*pos)++] = tag;
    while (t)
    {
        buf[(*pos)++] = tmp[--t]; // reverse the digits
    }
    buf[(*pos)++] = '\r';
    buf[(*pos)++] = '\n';
    return PROTO_TRUE;
}

size_t pc_resp_encode_command(char *buf, size_t cap, const char *const *args, const size_t *arg_lens, size_t argc)
{
    if (!buf || cap == 0 || !args || argc == 0)
    {
        return 0;
    }
    size_t pos = 0;
    if (!put_len_prefix(buf, cap, &pos, '*', argc))
    {
        return 0;
    }

    for (size_t i = 0; i < argc; i++)
    {
        if (!args[i])
        {
            return 0;
        }
        size_t alen = arg_lens ? arg_lens[i] : strnlen(args[i], cap);
        if (!put_len_prefix(buf, cap, &pos, '$', alen))
        {
            return 0;
        }
        if (pos + alen + 2 >= cap) // arg body + trailing CRLF (reserve NUL room)
        {
            return 0;
        }
        memcpy(buf + pos, args[i], alen);
        pos += alen;
        buf[pos++] = '\r';
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return pos;
}

// Find the CRLF that ends the line starting at buf[from]; returns the index of the
// '\r', or len if not found (incomplete line).
static size_t find_crlf(const uint8_t *buf, size_t len, size_t from)
{
    for (size_t i = from; i + 1 < len; i++)
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
        {
            return i;
        }
    }
    return len;
}

// Parse a base-10 (optionally negative) integer from [buf+from, buf+end).
static proto_bool parse_int(const uint8_t *buf, size_t from, size_t end, int64_t *out)
{
    if (from >= end)
    {
        return PROTO_FALSE;
    }
    proto_bool neg = PROTO_FALSE;
    size_t i = from;
    if (buf[i] == '-')
    {
        neg = PROTO_TRUE;
        i++;
    }
    if (i >= end)
    {
        return PROTO_FALSE;
    }
    uint64_t v = 0; // accumulate unsigned: signed overflow (a huge digit run) is UB
    for (; i < end; i++)
    {
        if (buf[i] < '0' || buf[i] > '9')
        {
            return PROTO_FALSE;
        }
        v = v * 10u + (uint64_t)(buf[i] - '0');
    }
    *out = neg ? (int64_t)(0ULL - v) : (int64_t)v; // two's-complement reinterpret, no negation UB
    return PROTO_TRUE;
}

// Case-insensitively compare the slice [buf+from, buf+end) to the C string s.
static proto_bool slice_ieq(const uint8_t *buf, size_t from, size_t end, const char *s)
{
    for (size_t i = from; i < end; i++, s++)
    {
        if (*s == '\0')
        {
            return PROTO_FALSE;
        }
        uint8_t a = buf[i];
        if (a >= 'A' && a <= 'Z')
        {
            a = (uint8_t)(a + 32);
        }
        char b = *s;
        if (b >= 'A' && b <= 'Z') // GCOVR_EXCL_BR_LINE  b is the pattern char; every caller (parse_double_special)
        {
            b = (char)(b + 32); // GCOVR_EXCL_LINE  passes a lowercase literal ("inf"/"+inf"/"-inf"/"nan"), so
                                // this upper->lower fold of the pattern side never fires; slice_ieq is static
                                // (file-local) with no other caller, so no host test can reach it either.
        }
        if (a != (uint8_t)b)
        {
            return PROTO_FALSE;
        }
    }
    return *s == '\0';
}

// RESP3 double special forms: inf / +inf / -inf / nan. Returns true (setting *out) if [from,end) is one,
// else false with *out untouched.
static proto_bool parse_double_special(const uint8_t *buf, size_t from, size_t end, double *out)
{
    if (slice_ieq(buf, from, end, "inf") || slice_ieq(buf, from, end, "+inf"))
    {
        *out = 1e308 * 10.0;
        return PROTO_TRUE;
    }
    if (slice_ieq(buf, from, end, "-inf"))
    {
        *out = -1e308 * 10.0;
        return PROTO_TRUE;
    }
    if (slice_ieq(buf, from, end, "nan"))
    {
        double inf = 1e308 * 10.0; // +infinity by overflow (this file avoids <math.h>, cf. -inf above)
        *out = inf * 0.0;          // infinity * 0 = NaN, without an identical-operand division
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// Parse an optional [ (e|E) [sign] digits ] exponent at *i (advancing it). A missing exponent leaves
// *exp = 0 and returns true; an 'e' with no following digits returns false.
static proto_bool parse_exponent(const uint8_t *buf, size_t *i, size_t end, int *exp)
{
    *exp = 0;
    if (!(*i < end && (buf[*i] == 'e' || buf[*i] == 'E')))
    {
        return PROTO_TRUE;
    }
    (*i)++;
    proto_bool eneg = PROTO_FALSE;
    if (*i < end && (buf[*i] == '+' || buf[*i] == '-'))
    {
        eneg = (buf[*i] == '-');
        (*i)++;
    }
    proto_bool edig = PROTO_FALSE;
    while (*i < end && buf[*i] >= '0' && buf[*i] <= '9')
    {
        if (*exp < 1000000) // clamp: a larger exponent saturates the double to inf/0 anyway
        {
            *exp = *exp * 10 + (buf[*i] - '0');
        }
        edig = PROTO_TRUE;
        (*i)++;
    }
    if (!edig)
    {
        return PROTO_FALSE;
    }
    if (eneg)
    {
        *exp = -*exp;
    }
    return PROTO_TRUE;
}

// Best-effort decimal-to-double for a RESP3 double line: inf / -inf / nan, or
// [sign] int [.frac] [(e|E)[sign]exp]. The raw text stays authoritative in str.
static proto_bool parse_double(const uint8_t *buf, size_t from, size_t end, double *out)
{
    if (parse_double_special(buf, from, end, out))
    {
        return PROTO_TRUE;
    }

    size_t i = from;
    proto_bool neg = PROTO_FALSE;
    if (i < end && (buf[i] == '+' || buf[i] == '-'))
    {
        neg = (buf[i] == '-');
        i++;
    }
    double mant = 0.0;
    proto_bool any = PROTO_FALSE;
    for (; i < end && buf[i] >= '0' && buf[i] <= '9'; i++)
    {
        mant = mant * 10.0 + (buf[i] - '0');
        any = PROTO_TRUE;
    }
    if (i < end && buf[i] == '.')
    {
        i++;
        double scale = 0.1;
        for (; i < end && buf[i] >= '0' && buf[i] <= '9'; i++)
        {
            mant += (buf[i] - '0') * scale;
            scale *= 0.1;
            any = PROTO_TRUE;
        }
    }
    if (!any)
    {
        return PROTO_FALSE;
    }
    int exp = 0;
    if (!parse_exponent(buf, &i, end, &exp))
    {
        return PROTO_FALSE;
    }
    if (i != end)
    {
        return PROTO_FALSE; // trailing garbage
    }
    double scale = 1.0;
    int e = exp < 0 ? -exp : exp;
    for (int k = 0; k < e; k++)
    {
        scale *= 10.0;
    }
    mant = (exp < 0) ? (mant / scale) : (mant * scale);
    *out = neg ? -mant : mant;
    return PROTO_TRUE;
}

// Length-prefixed body: bulk string ($), bulk error (!), verbatim string (=). Validates the declared
// length against the buffer and its trailing CRLF; $-1 decodes to nil. type is buf[0].
static proto_bool parse_bulk_body(const uint8_t *buf, size_t len, uint8_t type, size_t header_from, size_t header_to,
                                  size_t after_header, RespReply *out, size_t *consumed)
{
    int64_t blen;
    if (!parse_int(buf, header_from, header_to, &blen))
    {
        return PROTO_FALSE;
    }
    if (type == '$' && blen < 0) // $-1 = nil
    {
        out->type = RESP_NIL;
        *consumed = after_header;
        return PROTO_TRUE;
    }
    if (blen < 0)
    {
        return PROTO_FALSE;
    }
    // Bound the length against the remaining capacity without adding it (a 32-bit
    // size_t would wrap if we computed after_header + blen + 2 first).
    if (after_header + 2 > len || (uint64_t)blen > (uint64_t)(len - after_header - 2))
    {
        return PROTO_FALSE; // body + trailing CRLF not fully buffered
    }
    size_t need = after_header + (size_t)blen + 2; // body + trailing CRLF
    if (buf[after_header + (size_t)blen] != '\r' || buf[after_header + (size_t)blen + 1] != '\n')
    {
        return PROTO_FALSE; // malformed terminator
    }
    if (type == '$')
    {
        out->type = RESP_BULK;
    }
    else if (type == '!')
    {
        out->type = RESP_BULK_ERROR;
    }
    else
    {
        out->type = RESP_VERBATIM;
    }
    out->str = (const char *)(buf + after_header);
    out->str_len = (size_t)blen;
    *consumed = need;
    return PROTO_TRUE;
}

// Aggregate header whose children follow: array (*), set (~), push (>). Only the count is read here; the
// caller parses each element next. *-1 decodes to nil. type is buf[0].
static proto_bool parse_aggregate(const uint8_t *buf, uint8_t type, size_t header_from, size_t header_to,
                                  size_t after_header, RespReply *out, size_t *consumed)
{
    int64_t n;
    if (!parse_int(buf, header_from, header_to, &n))
    {
        return PROTO_FALSE;
    }
    if (type == '*' && n < 0) // *-1 = nil array
    {
        out->type = RESP_NIL;
        *consumed = after_header;
        return PROTO_TRUE;
    }
    if (n < 0)
    {
        return PROTO_FALSE;
    }
    if (type == '*')
    {
        out->type = RESP_ARRAY;
    }
    else if (type == '~')
    {
        out->type = RESP_SET;
    }
    else
    {
        out->type = RESP_PUSH;
    }
    out->ival = n;
    out->count = n;
    *consumed = after_header; // header only; the caller parses each element next
    return PROTO_TRUE;
}

proto_bool pc_resp_parse(const uint8_t *buf, size_t len, RespReply *out, size_t *consumed)
{
    if (!buf || len < 3 || !out || !consumed) // shortest value is "+\r\n" style; need a type + CRLF
    {
        return PROTO_FALSE;
    }

    size_t crlf = find_crlf(buf, len, 1);
    if (crlf == len)
    {
        return PROTO_FALSE; // header line incomplete
    }
    size_t header_from = 1;
    size_t header_to = crlf;
    size_t after_header = crlf + 2; // past \r\n

    out->str = NULL;
    out->str_len = 0;
    out->ival = 0;
    out->dval = 0;
    out->count = 0;

    switch (buf[0])
    {
    case '+':
    case '-':
        out->type = (buf[0] == '+') ? RESP_SIMPLE : RESP_ERROR;
        out->str = (const char *)(buf + header_from);
        out->str_len = header_to - header_from;
        *consumed = after_header;
        return PROTO_TRUE;

    case ':': {
        int64_t v;
        if (!parse_int(buf, header_from, header_to, &v))
        {
            return PROTO_FALSE;
        }
        out->type = RESP_INTEGER;
        out->ival = v;
        *consumed = after_header;
        return PROTO_TRUE;
    }

    // Length-prefixed bodies: bulk string ($), bulk error (!), verbatim string (=).
    case '$':
    case '!':
    case '=':
        return parse_bulk_body(buf, len, buf[0], header_from, header_to, after_header, out, consumed);

    // Aggregates whose children follow: array (*), set (~), push (>).
    case '*':
    case '~':
    case '>':
        return parse_aggregate(buf, buf[0], header_from, header_to, after_header, out, consumed);

    case '%': { // map: N pairs -> 2N following child values
        int64_t n;
        if (!parse_int(buf, header_from, header_to, &n) || n < 0)
        {
            return PROTO_FALSE;
        }
        out->type = RESP_MAP;
        out->ival = n * 2;
        out->count = n * 2;
        *consumed = after_header;
        return PROTO_TRUE;
    }

    case '_': // RESP3 null
        out->type = RESP_NIL;
        *consumed = after_header;
        return PROTO_TRUE;

    case '#': { // boolean
        if (header_to - header_from != 1 || (buf[header_from] != 't' && buf[header_from] != 'f'))
        {
            return PROTO_FALSE;
        }
        out->type = RESP_BOOL;
        out->ival = (buf[header_from] == 't') ? 1 : 0;
        *consumed = after_header;
        return PROTO_TRUE;
    }

    case ',': // double (text authoritative; dval best-effort)
        out->type = RESP_DOUBLE;
        out->str = (const char *)(buf + header_from);
        out->str_len = header_to - header_from;
        parse_double(buf, header_from, header_to, &out->dval);
        *consumed = after_header;
        return PROTO_TRUE;

    case '(': // big number (digits kept as text)
        out->type = RESP_BIG_NUMBER;
        out->str = (const char *)(buf + header_from);
        out->str_len = header_to - header_from;
        *consumed = after_header;
        return PROTO_TRUE;

    default:
        return PROTO_FALSE; // unknown type byte
    }
}

#endif // PC_ENABLE_REDIS
