// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file der.c
 * @brief One DER value at a time, over the caller's bytes. See der.h.
 */

#include "shared/der/der.h"

#include "mmgr/protomem.h" // mem.cmp: the OID comparison

PROTOCORE_BEGIN_DECLS

// Days from 1970-01-01 to the first of the given month, in a non-leap year.
static const uint16_t MONTH_DAYS[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

// One value's identifier and length octets. Reports the content's offset and length, or false when
// the encoding is not the definite short-or-long form DER allows (X.690 sec 10.1).
static proto_bool tlv_at(const uint8_t *buf, size_t len, size_t pos, uint8_t *tag, size_t *content, size_t *clen)
{
    if (buf == NULL || pos >= len)
    {
        return PROTO_FALSE;
    }
    const uint8_t id = buf[pos];
    // X.690 sec 8.1.2.4: tag number 31 begins a multi-octet form. No field this profile reads uses
    // one, so it is refused rather than skipped past.
    if ((id & 0x1Fu) == 0x1Fu)
    {
        return PROTO_FALSE;
    }
    size_t p = pos + 1;
    if (p >= len)
    {
        return PROTO_FALSE;
    }
    size_t n = buf[p];
    p++;
    if (n & 0x80u)
    {
        const size_t count = n & 0x7Fu;
        // X.690 sec 8.1.3.6 indefinite length: BER only, and sec 10.1 forbids it in DER.
        if (count == 0u || count > sizeof(size_t))
        {
            return PROTO_FALSE;
        }
        if (p + count > len)
        {
            return PROTO_FALSE;
        }
        n = 0;
        for (size_t i = 0; i < count; i++)
        {
            n = (n << 8) | buf[p + i];
        }
        p += count;
        // X.690 sec 10.1: the shortest form. A long form that fits the short one, or one with a
        // leading zero octet, is a second encoding of the same value and is refused.
        if (n < 0x80u || buf[pos + 2] == 0x00u)
        {
            return PROTO_FALSE;
        }
    }
    if (n > len - p) // subtraction, not p + n: the sum could wrap
    {
        return PROTO_FALSE;
    }
    *tag = id;
    *content = p;
    *clen = n;
    return PROTO_TRUE;
}

static void der_read(uint8_t *restrict work)
{
    (void)work;
    size_t content = 0;
    size_t clen = 0;
    uint8_t tag = 0;
    Der.ok = tlv_at(Der.read_args.buf, Der.read_args.len, Der.read_args.pos, &tag, &content, &clen);
    if (!Der.ok)
    {
        return;
    }
    Der.tlv.tag = tag;
    Der.tlv.content = Der.read_args.buf + content;
    Der.tlv.len = clen;
    Der.tlv.next = content + clen;
}

static void der_enter(uint8_t *restrict work)
{
    der_read(work);
    if (!Der.ok)
    {
        return;
    }
    // X.690 sec 8.1.2.5: only a constructed value holds other values.
    if ((Der.tlv.tag & PROTOCORE_DER_CONSTRUCTED) == 0u)
    {
        Der.ok = PROTO_FALSE;
        return;
    }
    if (Der.tlv.len == 0u)
    {
        Der.ok = PROTO_FALSE; // nothing inside to step to
        return;
    }
    const size_t inner = (size_t)(Der.tlv.content - Der.read_args.buf);
    Der.read_args.pos = inner;
    der_read(work);
}

static void der_uint(uint8_t *restrict work)
{
    der_read(work);
    if (!Der.ok)
    {
        return;
    }
    if (Der.tlv.tag != PROTOCORE_DER_INTEGER || Der.tlv.len == 0u)
    {
        Der.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *v = Der.tlv.content;
    size_t n = Der.tlv.len;
    // X.690 sec 8.3.3: two's complement, and the shortest form. A leading 0x00 is present only to
    // keep a value with the high bit set positive; any other leading 0x00 is a second encoding.
    if (v[0] == 0x00u)
    {
        if (n == 1u)
        {
            Der.u64 = 0;
            return;
        }
        if ((v[1] & 0x80u) == 0u)
        {
            Der.ok = PROTO_FALSE;
            return;
        }
        v++;
        n--;
    }
    else if (v[0] & 0x80u)
    {
        Der.ok = PROTO_FALSE; // negative; no field this profile reads is
        return;
    }
    if (n > 8u)
    {
        Der.ok = PROTO_FALSE; // wider than the value this reports
        return;
    }
    uint64_t out = 0;
    for (size_t i = 0; i < n; i++)
    {
        out = (out << 8) | v[i];
    }
    Der.u64 = out;
}

static void der_bitstring(uint8_t *restrict work)
{
    der_read(work);
    if (!Der.ok)
    {
        return;
    }
    if (Der.tlv.tag != PROTOCORE_DER_BIT_STRING || Der.tlv.len == 0u)
    {
        Der.ok = PROTO_FALSE;
        return;
    }
    // X.690 sec 8.6.2.2: the first content octet is the count of unused bits in the last one. A key
    // and a signature are whole octets, so any count but zero is refused rather than shifted out.
    if (Der.tlv.content[0] != 0x00u)
    {
        Der.ok = PROTO_FALSE;
        return;
    }
    Der.tlv.content++;
    Der.tlv.len--;
}

static void der_oid_eq(uint8_t *restrict work)
{
    der_read(work);
    if (!Der.ok)
    {
        return;
    }
    if (Der.tlv.tag != PROTOCORE_DER_OID || Der.oid_args.oid == NULL)
    {
        Der.ok = PROTO_FALSE;
        return;
    }
    Der.ok = (Der.tlv.len == Der.oid_args.oid_len) &&
             (mem.cmp(Der.tlv.content, Der.oid_args.oid, Der.oid_args.oid_len) == 0);
}

// Four ASCII digits as a number, or -1 when any of them is not a digit.
static int32_t digits(const uint8_t *p, size_t n)
{
    int32_t out = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (p[i] < '0' || p[i] > '9')
        {
            return -1;
        }
        out = out * 10 + (p[i] - '0');
    }
    return out;
}

// Seconds since 1970-01-01T00:00:00Z for a proleptic Gregorian date. The year is whole, so the leap
// rule is applied to the years before it rather than to a running count.
static uint64_t epoch_of(int32_t y, int32_t mon, int32_t day, int32_t hh, int32_t mm, int32_t ss)
{
    int64_t days = 0;
    for (int32_t i = 1970; i < y; i++)
    {
        days += ((i % 4 == 0 && i % 100 != 0) || i % 400 == 0) ? 366 : 365;
    }
    days += MONTH_DAYS[mon - 1];
    if (mon > 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
    {
        days += 1; // this year's leap day is behind the walk position
    }
    days += day - 1;
    return (uint64_t)(((days * 24 + hh) * 60 + mm) * 60 + ss);
}

static void der_time(uint8_t *restrict work)
{
    der_read(work);
    if (!Der.ok)
    {
        return;
    }
    const uint8_t *v = Der.tlv.content;
    int32_t year = 0;
    size_t at = 0;

    if (Der.tlv.tag == PROTOCORE_DER_UTC_TIME)
    {
        // RFC 5280 sec 4.1.2.5.1: YYMMDDHHMMSSZ, seconds always present, Zulu always.
        if (Der.tlv.len != 13u || v[12] != 'Z')
        {
            Der.ok = PROTO_FALSE;
            return;
        }
        const int32_t yy = digits(v, 2);
        if (yy < 0)
        {
            Der.ok = PROTO_FALSE;
            return;
        }
        year = (yy >= 50) ? (1900 + yy) : (2000 + yy); // sec 4.1.2.5.1: the pivot is 50
        at = 2;
    }
    else if (Der.tlv.tag == PROTOCORE_DER_GENERALIZED_TIME)
    {
        // RFC 5280 sec 4.1.2.5.2: YYYYMMDDHHMMSSZ, seconds always present, no fractional part.
        if (Der.tlv.len != 15u || v[14] != 'Z')
        {
            Der.ok = PROTO_FALSE;
            return;
        }
        year = digits(v, 4);
        if (year < 0)
        {
            Der.ok = PROTO_FALSE;
            return;
        }
        at = 4;
    }
    else
    {
        Der.ok = PROTO_FALSE;
        return;
    }

    const int32_t mon = digits(v + at, 2);
    const int32_t day = digits(v + at + 2, 2);
    const int32_t hh = digits(v + at + 4, 2);
    const int32_t mm = digits(v + at + 6, 2);
    const int32_t ss = digits(v + at + 8, 2);
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60 ||
        year < 1970)
    {
        Der.ok = PROTO_FALSE; // ss 60 is a leap second, which X.690 permits and the epoch folds
        return;
    }
    Der.u64 = epoch_of(year, mon, day, hh, mm, (ss == 60) ? 59 : ss);
}

// Designated, so a member's position in the struct does not decide what it binds to.
DerNs Der = {
    .read = der_read,
    .enter = der_enter,
    .uint = der_uint,
    .bitstring = der_bitstring,
    .oid_eq = der_oid_eq,
    .time = der_time,
};

PROTOCORE_END_DECLS
