// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file redis_resp.c
 * @brief The RESP command encoder and the RESP cursor parser.
 *
 * encode_command writes the array of bulk strings of "Sending commands to a Redis server" into the
 * caller's buffer. parse_reply decodes the one value at the head of the buffered octets, leaves it
 * in ns->reply, and reports the octets it occupied in ns->n.
 */

#include "services/iot/redis_resp/redis_resp.h"

#if PROTOCORE_ENABLE_REDIS

#include "mmgr/protomem.h" // mem.cpy: the argument octets an encode moves
#include "mmgr/protostr.h" // str.len: the bounded length of a NUL-terminated argument

/**
 * @brief The codec's state and the calls that reach it - what RespNs points at.
 *
 * No storage member: the codec keeps nothing between calls. An encode writes into the caller's
 * buffer, and every string a parse decodes points into the caller's buffer.
 *
 * @var RespInternal::ns  the handle a caller sets a call's members on
 */
struct RespInternal
{
    RespNs *ns;
};

static struct RespInternal s_resp = {.ns = &Resp};

// Write "<first_byte><decimal n>\r\n" into buf at *pos and advance it. The digits fall out low first
// into tmp and are emitted reversed. False when the prefix would reach cap with the NUL reserved.
static proto_bool put_len_prefix(char *buf, size_t cap, size_t *pos, char first_byte, size_t n)
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
    buf[(*pos)++] = first_byte;
    while (t)
    {
        buf[(*pos)++] = tmp[--t];
    }
    buf[(*pos)++] = '\r';
    buf[(*pos)++] = '\n';
    return PROTO_TRUE;
}

// Index of the '\r' of the CRLF terminator that ends the line starting at buf[from], or len when no
// CRLF is buffered.
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

// Read a base-10 integer, an optional leading '-' included, from [buf+from, buf+end).
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
    uint64_t v = 0; // accumulated unsigned, so a digit run past the signed maximum still wraps defined
    for (; i < end; i++)
    {
        if (buf[i] < '0' || buf[i] > '9')
        {
            return PROTO_FALSE;
        }
        v = v * 10u + (uint64_t)(buf[i] - '0');
    }
    *out = neg ? (int64_t)(0ULL - v) : (int64_t)v; // negated in two's complement, then reinterpreted
    return PROTO_TRUE;
}

// Compare the slice [buf+from, buf+end) to the NUL-terminated s, folding ASCII letters to lower case.
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
        if (b >= 'A' && b <= 'Z')
        {
            b = (char)(b + 32);
        }
        if (a != (uint8_t)b)
        {
            return PROTO_FALSE;
        }
    }
    return *s == '\0';
}

// The Doubles special forms `,inf\r\n`, `,-inf\r\n` and `,nan\r\n`, the leading '+' accepted. True with
// *out set when [from,end) is one of them, false with *out untouched otherwise.
static proto_bool parse_double_special(const uint8_t *buf, size_t from, size_t end, double *out)
{
    if (slice_ieq(buf, from, end, "inf") || slice_ieq(buf, from, end, "+inf"))
    {
        *out = 1e308 * 10.0; // overflows to +infinity
        return PROTO_TRUE;
    }
    if (slice_ieq(buf, from, end, "-inf"))
    {
        *out = -1e308 * 10.0;
        return PROTO_TRUE;
    }
    if (slice_ieq(buf, from, end, "nan"))
    {
        double inf = 1e308 * 10.0;
        *out = inf * 0.0; // infinity times zero is NaN
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// Read the optional [(E|e) [sign] digits] exponent at *i, advancing it. No exponent leaves *exp at 0
// and returns true; an 'E' with no digits after it returns false.
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
        if (*exp < 1000000) // clamped: anything larger saturates the double to infinity or zero
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

// Decode a Doubles line, `,[<+|->]<integral>[.<fractional>][<E|e>[sign]<exponent>]\r\n` or one of the
// special forms, into a double. The raw text stays authoritative in str.
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
        return PROTO_FALSE; // octets left over after the number
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

// The length-prefixed bodies: Bulk strings ($), Bulk errors (!), Verbatim strings (=). Checks the
// declared length and its trailing CRLF against the buffered octets; `$-1` is the Null bulk string.
static proto_bool parse_bulk_body(struct RespInternal *restrict ctx, uint8_t first_byte, size_t header_from,
                                  size_t header_to, size_t after_header)
{
    const uint8_t *buf = ctx->ns->wire.buf;
    const size_t len = ctx->ns->wire.len;
    int64_t blen;
    if (!parse_int(buf, header_from, header_to, &blen))
    {
        return PROTO_FALSE;
    }
    // The Null bulk string is the length -1 exactly ("Null bulk strings"); no other negative length
    // has an encoding, so it is a malformed reply rather than another spelling of null.
    if (first_byte == '$' && blen == -1)
    {
        ctx->ns->reply.type = RESP_NULL;
        ctx->ns->n = after_header;
        return PROTO_TRUE;
    }
    if (blen < 0)
    {
        return PROTO_FALSE;
    }
    // Compares the declared length against what remains, never forming after_header + blen + 2,
    // which wraps a 32-bit size_t.
    if (after_header + 2 > len || (uint64_t)blen > (uint64_t)(len - after_header - 2))
    {
        return PROTO_FALSE; // the body and its CRLF are not fully buffered
    }
    if (buf[after_header + (size_t)blen] != '\r' || buf[after_header + (size_t)blen + 1] != '\n')
    {
        return PROTO_FALSE; // the terminator is not where the length puts it
    }
    if (first_byte == '$')
    {
        ctx->ns->reply.type = RESP_BULK_STRING;
    }
    else if (first_byte == '!')
    {
        ctx->ns->reply.type = RESP_BULK_ERROR;
    }
    else
    {
        ctx->ns->reply.type = RESP_VERBATIM_STRING;
    }
    ctx->ns->reply.str = (const char *)(buf + after_header);
    ctx->ns->reply.str_len = (size_t)blen;
    ctx->ns->n = after_header + (size_t)blen + 2; // the body and its trailing CRLF
    return PROTO_TRUE;
}

// The aggregate headers whose children follow: Arrays (*), Sets (~), Pushes (>). Only the count is
// read here, and the caller parses each element next; `*-1` is the Null array.
static proto_bool parse_aggregate(struct RespInternal *restrict ctx, uint8_t first_byte, size_t header_from,
                                  size_t header_to, size_t after_header)
{
    const uint8_t *buf = ctx->ns->wire.buf;
    int64_t elements;
    if (!parse_int(buf, header_from, header_to, &elements))
    {
        return PROTO_FALSE;
    }
    // The Null array is the length -1 exactly ("Null arrays"), as for the Null bulk string above.
    if (first_byte == '*' && elements == -1)
    {
        ctx->ns->reply.type = RESP_NULL;
        ctx->ns->n = after_header;
        return PROTO_TRUE;
    }
    if (elements < 0)
    {
        return PROTO_FALSE;
    }
    if (first_byte == '*')
    {
        ctx->ns->reply.type = RESP_ARRAY;
    }
    else if (first_byte == '~')
    {
        ctx->ns->reply.type = RESP_SET;
    }
    else
    {
        ctx->ns->reply.type = RESP_PUSH;
    }
    ctx->ns->reply.ival = elements;
    ctx->ns->reply.count = elements;
    ctx->ns->n = after_header; // the header alone
    return PROTO_TRUE;
}

// Build the array of bulk strings a client sends ("Sending commands to a Redis server") from
// ns->command into ns->out, NUL-terminate it, and report its length in ns->n.
static void resp_encode_command(struct RespInternal *restrict ctx)
{
    char *buf = ctx->ns->out.buf;
    const size_t cap = ctx->ns->out.cap;
    const char *const *argv = ctx->ns->command.argv;
    const size_t *argv_len = ctx->ns->command.argv_len;
    const size_t argc = ctx->ns->command.argc;

    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!buf || cap == 0 || !argv || argc == 0)
    {
        return;
    }
    size_t pos = 0;
    if (!put_len_prefix(buf, cap, &pos, '*', argc))
    {
        return;
    }
    for (size_t i = 0; i < argc; i++)
    {
        if (!argv[i])
        {
            return;
        }
        const size_t alen = argv_len ? argv_len[i] : str.len(argv[i], cap);
        if (!put_len_prefix(buf, cap, &pos, '$', alen))
        {
            return;
        }
        if (pos + alen + 2 >= cap) // the octets and their CRLF, the NUL still reserved
        {
            return;
        }
        mem.cpy(buf + pos, argv[i], alen);
        pos += alen;
        buf[pos++] = '\r';
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    ctx->ns->n = pos;
    ctx->ns->ok = PROTO_TRUE;
}

// Decode the value at the head of ns->wire into ns->reply, and report the octets it occupied in
// ns->n. An aggregate header reports the header alone and its child count.
static void resp_parse_reply(struct RespInternal *restrict ctx)
{
    const uint8_t *buf = ctx->ns->wire.buf;
    const size_t len = ctx->ns->wire.len;

    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!buf || len < 3) // the shortest value is a first byte and its CRLF
    {
        return;
    }

    const size_t crlf = find_crlf(buf, len, 1);
    if (crlf == len)
    {
        return; // the header line is not fully buffered
    }
    const size_t header_from = 1;
    const size_t header_to = crlf;
    const size_t after_header = crlf + 2; // past the CRLF terminator

    ctx->ns->reply.str = NULL;
    ctx->ns->reply.str_len = 0;
    ctx->ns->reply.ival = 0;
    ctx->ns->reply.dval = 0;
    ctx->ns->reply.count = 0;

    switch (buf[0])
    {
    // Simple strings (+) and Simple errors (-): the line itself, CRLF excluded.
    case '+':
    case '-':
        ctx->ns->reply.type = (buf[0] == '+') ? RESP_SIMPLE_STRING : RESP_SIMPLE_ERROR;
        ctx->ns->reply.str = (const char *)(buf + header_from);
        ctx->ns->reply.str_len = header_to - header_from;
        ctx->ns->n = after_header;
        ctx->ns->ok = PROTO_TRUE;
        return;

    case ':': { // Integers: a signed, base-10, 64-bit value
        int64_t v;
        if (!parse_int(buf, header_from, header_to, &v))
        {
            return;
        }
        ctx->ns->reply.type = RESP_INTEGER;
        ctx->ns->reply.ival = v;
        ctx->ns->n = after_header;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }

    // The length-prefixed bodies: Bulk strings ($), Bulk errors (!), Verbatim strings (=).
    case '$':
    case '!':
    case '=':
        ctx->ns->ok = parse_bulk_body(ctx, buf[0], header_from, header_to, after_header);
        return;

    // The aggregates whose children follow: Arrays (*), Sets (~), Pushes (>).
    case '*':
    case '~':
    case '>':
        ctx->ns->ok = parse_aggregate(ctx, buf[0], header_from, header_to, after_header);
        return;

    case '%': { // Maps: N entries, so 2N children follow, one per key and one per value
        int64_t entries;
        if (!parse_int(buf, header_from, header_to, &entries) || entries < 0)
        {
            return;
        }
        const int64_t children = (int64_t)((uint64_t)entries * 2u); // doubled unsigned, then reinterpreted
        ctx->ns->reply.type = RESP_MAP;
        ctx->ns->reply.ival = children;
        ctx->ns->reply.count = children;
        ctx->ns->n = after_header;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }

    case '_': // Nulls
        ctx->ns->reply.type = RESP_NULL;
        ctx->ns->n = after_header;
        ctx->ns->ok = PROTO_TRUE;
        return;

    case '#': { // Booleans: exactly one octet, 't' or 'f'
        if (header_to - header_from != 1 || (buf[header_from] != 't' && buf[header_from] != 'f'))
        {
            return;
        }
        ctx->ns->reply.type = RESP_BOOLEAN;
        ctx->ns->reply.ival = (buf[header_from] == 't') ? 1 : 0;
        ctx->ns->n = after_header;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }

    case ',': // Doubles: the text is authoritative, dval is decoded from it
        ctx->ns->reply.type = RESP_DOUBLE;
        ctx->ns->reply.str = (const char *)(buf + header_from);
        ctx->ns->reply.str_len = header_to - header_from;
        parse_double(buf, header_from, header_to, &ctx->ns->reply.dval);
        ctx->ns->n = after_header;
        ctx->ns->ok = PROTO_TRUE;
        return;

    case '(': // Big numbers: the digits stay text
        ctx->ns->reply.type = RESP_BIG_NUMBER;
        ctx->ns->reply.str = (const char *)(buf + header_from);
        ctx->ns->reply.str_len = header_to - header_from;
        ctx->ns->n = after_header;
        ctx->ns->ok = PROTO_TRUE;
        return;

    default: // no RESP type claims this first byte
        return;
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
RespNs Resp = {.encode_command = resp_encode_command, .parse_reply = resp_parse_reply, .internal = &s_resp};

#endif // PROTOCORE_ENABLE_REDIS
