// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CDN edge-cache engine (server/web/edge_cache/edge_cache.h) and the shared
// byte-range parser it cuts 206 windows with (network_drivers/application/http_range.h).
//
// The load-bearing cases are the ones a document prints outright. RFC 9110 sec 5.6.7 prints one
// instant in all three HTTP-date spellings; sec 14.1.2 prints four byte-range examples against a
// 10000-octet representation and the two satisfiability bullets; RFC 9111 sec 4.2 prints
// "response_is_fresh = (freshness_lifetime > current_age)" and sec 4.2.3 prints the age
// arithmetic; RFC 6234 sec 8.5 prints SHA-256("abc"). Every epoch here is derived from the Unix
// epoch's own definition with the arithmetic shown, so a wrong month table cannot be reproduced by
// accident.
//
// test_rfc9110_zero_length_representation FAILS against the current parser, on purpose. RFC 9110
// sec 14.1.2 states "When a selected representation has zero length, the only satisfiable form of
// range-spec in a GET request is a suffix-range with a non-zero suffix-length", and sec 15.5.17
// scopes 416 to a range set in which no range is satisfiable. http_range.c returns -1 (the
// caller's 416) for "bytes=-1" against a zero-length representation, which the two sections
// together forbid. The assertion is the RFC's, not the parser's.
//
// The L1 store cases (alloc, LRU, purge, sweep) and the cache-key spelling are properties -
// ordering, distinctness, round trip, bounds refusal - because no standard fixes a cache's
// internal key format. RFC 9110 sec 4.2.3 does fix which parts of a URI compare case-insensitively,
// so that much of the key is anchored.

#include "network_drivers/application/http_range/http_range.h"
#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "server/web/edge_cache/edge_cache/edge_cache.h"
#include "shared/http_date/http_date.h"
#include <string.h>

#include <unity.h>

static uint8_t httpcache_work[16]; // the borrow an entry takes; Httpcache never reads it

static uint8_t http_range_work[16]; // the borrow an entry takes; HttpRange never reads it

static uint8_t edge_cache_work[16]; // the borrow an entry takes; EdgeCache never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 9110 sec 5.6.7 prints "Sun, 06 Nov 1994 08:49:37 GMT" as the example of the preferred
// format. Its epoch, from the definition of the Unix epoch alone:
//   1970-01-01 .. 1994-01-01 = 24 years, of which 1972/76/80/84/88/92 are leap
//                            = 24*365 + 6                                    = 8766 days
//   1994-01-01 .. 1994-11-06 = 31+28+31+30+31+30+31+31+30+31 = 304 to Oct 31,
//                              +6 to Nov 6, less the 1st itself              =  309 days
//   (8766 + 309) * 86400                                                     = 784080000
//   08:49:37 = 8*3600 + 49*60 + 37                                           =     31777
//                                                                              ----------
//                                                                              784111777
#define NOV6_1994 ((int64_t)784111777)

static int64_t date_of(const char *s)
{
    EdgeCache.parse_http_date_args.s = s;
    EdgeCache.parse_http_date_args.len = strlen(s);
    EdgeCache.parse_http_date(edge_cache_work);
    return EdgeCache.epoch;
}

// sec 5.6.7 prints all three formats as spellings of one instant:
//   Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate
//   Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format
//   Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format
// and requires "A recipient that parses a timestamp value in an HTTP field MUST accept all three
// HTTP-date formats". Three spellings, one epoch.
void test_rfc9110_three_spellings_of_one_instant(void)
{
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, date_of("Sun, 06 Nov 1994 08:49:37 GMT"));
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, date_of("Sunday, 06-Nov-94 08:49:37 GMT"));
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, date_of("Sun Nov  6 08:49:37 1994"));
}

// Three instants fixed by arithmetic on the epoch, not by a second date library.
//   epoch 0 is 1970-01-01 00:00:00 UTC by definition, a Thursday.
//   2^31-1 = 2147483647 is the last instant a signed 32-bit time_t names.
//   1970-01-01 .. 2000-01-01 = 30 years, leap 1972..1996 every 4th = 7 leap days
//                            = 30*365 + 7                                  = 10957 days
//   +31 (January) +28 (to Feb 29)                                          = 11016 days
//   11016 * 86400                                                          = 951782400
// 2000 is leap under the Gregorian century rule, so Feb 29 2000 exists.
void test_http_date_anchor_instants(void)
{
    TEST_ASSERT_EQUAL_INT64((int64_t)0, date_of("Thu, 01 Jan 1970 00:00:00 GMT"));
    TEST_ASSERT_EQUAL_INT64((int64_t)2147483647, date_of("Tue, 19 Jan 2038 03:14:07 GMT"));
    TEST_ASSERT_EQUAL_INT64((int64_t)951782400, date_of("Tue, 29 Feb 2000 00:00:00 GMT"));
}

// sec 5.6.7 closes each field: month is a choice of twelve 3-letter tokens, time-of-day is
// hour ":" minute ":" second over 00:00:00 - 23:59:60, and day names a calendar date. A string
// outside those is not an HTTP-date and must not resolve to an instant, since a cache that invents
// one expires an entry at a time the origin never named.
void test_http_date_refuses_text_that_names_no_instant(void)
{
    static const char *const BAD[] = {
        "",                              // no date at all
        "not a date",                    // no month token
        "Sun, 06 Xxx 1994 08:49:37 GMT", // month outside the twelve-token choice
        "Sun, 06 Nov 1994 08:49 GMT",    // time-of-day without its second
        "Sun, 32 Nov 1994 08:49:37 GMT", // November has 30 days
        "Sun, 06 Nov 1994 24:49:37 GMT", // hour past the stated 23 maximum
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT64_MESSAGE((int64_t)-1, date_of(BAD[i]), BAD[i]);
    }
}

// RFC 9110 sec 14.1.2 prints these four against "a representation of length 10000":
//   bytes=0-499    the first 500 bytes (byte offsets 0-499, inclusive)
//   bytes=500-999  the second 500 bytes
//   bytes=-500     the final 500 bytes (byte offsets 9500-9999, inclusive)
//   bytes=9500-    the same final 500 bytes
// The section also fixes that "the byte positions specified are inclusive" and offsets start at 0.
void test_rfc9110_published_range_examples(void)
{
    size_t s = 0;
    size_t e = 0;

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=0-499";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(499u, e);

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=500-999";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(500u, s);
    TEST_ASSERT_EQUAL_UINT(999u, e);

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=-500";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(9500u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=9500-";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(9500u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);
}

// sec 14.1.2 states both clamps. Int-range: "If the last-pos value is absent, or if the value is
// greater than or equal to the current length of the representation data, the byte range is
// interpreted as the remainder of the representation (i.e., the server replaces the value of
// last-pos with a value that is one less than the current length)" - so 10000-1 = 9999.
// Suffix-range: "If the selected representation is shorter than the specified suffix-length, the
// entire representation is used" - so bytes=-99999 of 10000 octets is 0-9999.
void test_rfc9110_last_pos_and_suffix_clamping(void)
{
    size_t s = 0;
    size_t e = 0;

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=0-99999";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=-99999";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);

    // The one-byte ends of both forms: first byte, last byte.
    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=0-0";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(0u, e);

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=-1";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(9999u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);
}

// sec 14.1.2: "a valid bytes range-spec is satisfiable if it is either: an int-range with a
// first-pos that is less than the current length of the selected representation or a suffix-range
// with a non-zero suffix-length". 10000 and 10500 are not less than 10000, and -0 has a zero
// suffix-length, so none of the three is satisfiable and sec 15.5.17 puts them at 416.
void test_rfc9110_unsatisfiable_ranges(void)
{
    size_t s = 0;
    size_t e = 0;

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=10000-";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(-1, HttpRangeV.n);
    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=10500-11000";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(-1, HttpRangeV.n);
    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=-0";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(-1, HttpRangeV.n);
}

// sec 14.1.1: "An int-range is invalid if the last-pos value is present and less than the
// first-pos." sec 14.2 lets a server "ignore or reject a Range header field that contains an
// invalid ranges-specifier", so both the 200 fallback (0) and the 416 (-1) conform; serving it as
// a satisfiable window (1) does not. Only that is asserted.
void test_rfc9110_an_invalid_int_range_is_never_served(void)
{
    size_t s = 0;
    size_t e = 0;

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=500-499";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_NOT_EQUAL(1, HttpRangeV.n);
}

// sec 14.1.2: "When a selected representation has zero length, the only satisfiable form of
// range-spec in a GET request is a suffix-range with a non-zero suffix-length."
//
// So against a zero-length representation:
//   bytes=0-0  int-range, first-pos 0, and 0 is not less than the length 0 -> unsatisfiable, 416.
//   bytes=-1   suffix-range, suffix-length 1, non-zero -> satisfiable, and sec 15.5.17 confines
//              416 to a set where "none of the requested ranges are satisfiable", so 416 is not
//              an available answer. Serving it (1) or ignoring the field for a full 200 (0, sec
//              14.2 "A server MAY ignore the Range header field") both conform; -1 does not.
//
// The second assertion FAILS: http_range.c's suffix branch returns -1 whenever size == 0.
void test_rfc9110_zero_length_representation(void)
{
    size_t s = 0;
    size_t e = 0;

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=0-0";
    HttpRangeV.http_parse_byte_range_args.size = 0;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(-1, HttpRangeV.n);
    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=-1";
    HttpRangeV.http_parse_byte_range_args.size = 0;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_NOT_EQUAL(-1, HttpRangeV.n);
}

// sec 14.2: "An origin server MUST ignore a Range header field that contains a range unit it does
// not understand", and "A server MAY ignore the Range header field" - which covers the two
// multi-range specifiers sec 14.1.2 prints, since this parser serves one window. Anything that is
// not a ranges-specifier at all (sec 14.1.1 grammar) is likewise no usable Range. Ignoring means a
// full 200, which is the 0 return.
//
// sec 14.1: "All range unit names are case-insensitive", so BYTES= is the bytes unit.
void test_rfc9110_unusable_range_headers_fall_back_to_a_full_response(void)
{
    size_t s = 0;
    size_t e = 0;
    static const char *const IGNORED[] = {
        "bytes=0-0,-1",                   // sec 14.1.2 example: first and last bytes only
        "bytes= 0-999, 4500-5499, -1000", // sec 14.1.2 example: first, middle and last 1000
        "items=0-499",                    // unregistered range unit
        "bytes=abc",                      // first-pos is 1*DIGIT
        "bytes=",                         // empty range-set
        "bytes=0-499x",                   // trailing octet outside the grammar
        "0-499",                          // no range-unit "=" prefix
    };
    for (size_t i = 0; i < sizeof(IGNORED) / sizeof(IGNORED[0]); i++)
    {
        HttpRangeV.http_parse_byte_range_args.hdr = IGNORED[i];
        HttpRangeV.http_parse_byte_range_args.size = 10000;
        HttpRangeV.http_parse_byte_range_args.out_start = &s;
        HttpRangeV.http_parse_byte_range_args.out_end = &e;
        HttpRange.http_parse_byte_range(http_range_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, HttpRangeV.n, IGNORED[i]);
    }
    HttpRangeV.http_parse_byte_range_args.hdr = NULL;
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(0, HttpRangeV.n);

    HttpRangeV.http_parse_byte_range_args.hdr = "BYTES=0-9";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(9u, e);
}

// sec 14.1.2: "Since there is no predefined limit to the length of content, recipients MUST
// anticipate potentially large decimal numerals and prevent parsing errors due to integer
// conversion overflows." 23 digits overflow every size_t, so a wrap would turn a past-EOF
// first-pos into a small in-range one and serve the wrong bytes. The satisfiability bullet then
// decides: a first-pos that large is not less than 10000 (416), while a last-pos that large is
// "greater than or equal to the current length" and clamps to 9999.
void test_rfc9110_large_decimal_numerals_do_not_wrap(void)
{
    size_t s = 0;
    size_t e = 0;

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=99999999999999999999999-";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(-1, HttpRangeV.n);

    HttpRangeV.http_parse_byte_range_args.hdr = "bytes=10-99999999999999999999999";
    HttpRangeV.http_parse_byte_range_args.size = 10000;
    HttpRangeV.http_parse_byte_range_args.out_start = &s;
    HttpRangeV.http_parse_byte_range_args.out_end = &e;
    HttpRange.http_parse_byte_range(http_range_work);
    TEST_ASSERT_EQUAL_INT(1, HttpRangeV.n);
    TEST_ASSERT_EQUAL_UINT(10u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);
}

static const char *const HEAD = "HTTP/1.1 200 OK\r\n"
                                "ETag: \"abc123\"\r\n"
                                "Cache-Control:   max-age=60  \r\n"
                                "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                                "Content-Type: text/html\r\n"
                                "ETag: \"second\"\r\n"
                                "\r\n";

// RFC 9110 sec 5.1: "Field names are case-insensitive". RFC 9112 sec 5: "field-line = field-name
// ":" OWS field-value OWS" and "The field line value does not include that leading or trailing
// whitespace: OWS occurring before the first non-whitespace octet ... or after the last
// non-whitespace octet ... is excluded by parsers". So "max-age=60" is the value of the line
// spelled "Cache-Control:   max-age=60  ", whatever case the lookup asks for.
//
// The status line is not a field line (RFC 9112 sec 4 puts it before the field section), so its
// text is not reachable as a field value.
void test_rfc9112_field_lookup_is_case_insensitive_and_ows_trimmed(void)
{
    char out[64];
    size_t n = strlen(HEAD);

    EdgeCache.header_value_args.hdrs = HEAD;
    EdgeCache.header_value_args.len = n;
    EdgeCache.header_value_args.name = "ETag";
    EdgeCache.header_value_args.out = out;
    EdgeCache.header_value_args.out_cap = sizeof(out);
    EdgeCache.header_value(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING("\"abc123\"", out);
    EdgeCache.header_value_args.hdrs = HEAD;
    EdgeCache.header_value_args.len = n;
    EdgeCache.header_value_args.name = "cache-CONTROL";
    EdgeCache.header_value_args.out = out;
    EdgeCache.header_value_args.out_cap = sizeof(out);
    EdgeCache.header_value(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING("max-age=60", out);
    EdgeCache.header_value_args.hdrs = HEAD;
    EdgeCache.header_value_args.len = n;
    EdgeCache.header_value_args.name = "Content-Type";
    EdgeCache.header_value_args.out = out;
    EdgeCache.header_value_args.out_cap = sizeof(out);
    EdgeCache.header_value(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING("text/html", out);

    EdgeCache.header_value_args.hdrs = HEAD;
    EdgeCache.header_value_args.len = n;
    EdgeCache.header_value_args.name = "HTTP/1.1 200 OK";
    EdgeCache.header_value_args.out = out;
    EdgeCache.header_value_args.out_cap = sizeof(out);
    EdgeCache.header_value(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
}

// RFC 9110 sec 8.8.3.2 compares entity tags "character-by-character", so a validator that lost its
// tail is a different validator and would revalidate against the wrong representation. A value
// that does not fit is therefore reported absent, never truncated, and the buffer is left empty
// rather than holding a prefix a caller could send.
void test_field_lookup_refuses_rather_than_truncates(void)
{
    char out[64];
    size_t n = strlen(HEAD);

    EdgeCache.header_value_args.hdrs = HEAD;
    EdgeCache.header_value_args.len = n;
    EdgeCache.header_value_args.name = "X-Missing";
    EdgeCache.header_value_args.out = out;
    EdgeCache.header_value_args.out_cap = sizeof(out);
    EdgeCache.header_value(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING("", out);

    char tiny[4]; // "text/html" is 9 octets
    EdgeCache.header_value_args.hdrs = HEAD;
    EdgeCache.header_value_args.len = n;
    EdgeCache.header_value_args.name = "Content-Type";
    EdgeCache.header_value_args.out = tiny;
    EdgeCache.header_value_args.out_cap = sizeof(tiny);
    EdgeCache.header_value(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

static void parse_cc(const char *s, protocore_cache_control *cc)
{
    Httpcache.control_parse_args.s = s;
    Httpcache.control_parse_args.len = strlen(s);
    Httpcache.control_parse_args.cc = cc;
    Httpcache.control_parse(httpcache_work);
}

// RFC 9111 sec 4.2.1 evaluates four rules and uses the first match:
//   1. shared cache and s-maxage present -> its value
//   2. max-age present                   -> its value
//   3. Expires present                   -> Expires minus Date
//   4. otherwise                         -> no explicit expiration (heuristic territory)
// So the same header set gives 100 to a shared cache and 50 to a private one, and max-age wins
// over an Expires that is 600 s after Date.
//
// sec 4.2: "If an origin server wishes to force a cache to validate every request, it can assign
// an explicit expiration time in the past". Rule 3 is plain subtraction, so an Expires 100 s
// before Date is a lifetime of -100: explicit and already elapsed, not absent.
void test_rfc9111_freshness_lifetime_precedence(void)
{
    protocore_cache_control cc;
    parse_cc("public, max-age=50, s-maxage=100", &cc);
    EdgeCache.freshness_lifetime_args.cc = &cc;
    EdgeCache.freshness_lifetime_args.shared = PROTO_TRUE;
    EdgeCache.freshness_lifetime_args.date_epoch = -1;
    EdgeCache.freshness_lifetime_args.expires_epoch = -1;
    EdgeCache.freshness_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(100, EdgeCache.secs);
    EdgeCache.freshness_lifetime_args.cc = &cc;
    EdgeCache.freshness_lifetime_args.shared = PROTO_FALSE;
    EdgeCache.freshness_lifetime_args.date_epoch = -1;
    EdgeCache.freshness_lifetime_args.expires_epoch = -1;
    EdgeCache.freshness_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(50, EdgeCache.secs);

    protocore_cache_control none;
    Httpcache.control_init_args.cc = &none;
    Httpcache.control_init(httpcache_work);
    EdgeCache.freshness_lifetime_args.cc = &none;
    EdgeCache.freshness_lifetime_args.shared = PROTO_TRUE;
    EdgeCache.freshness_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.freshness_lifetime_args.expires_epoch = NOV6_1994 + 600;
    EdgeCache.freshness_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(600, EdgeCache.secs);
    EdgeCache.freshness_lifetime_args.cc = &none;
    EdgeCache.freshness_lifetime_args.shared = PROTO_TRUE;
    EdgeCache.freshness_lifetime_args.date_epoch = -1;
    EdgeCache.freshness_lifetime_args.expires_epoch = -1;
    EdgeCache.freshness_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(-1, EdgeCache.secs);

    protocore_cache_control ma;
    parse_cc("max-age=30", &ma);
    EdgeCache.freshness_lifetime_args.cc = &ma;
    EdgeCache.freshness_lifetime_args.shared = PROTO_TRUE;
    EdgeCache.freshness_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.freshness_lifetime_args.expires_epoch = NOV6_1994 + 600;
    EdgeCache.freshness_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(30, EdgeCache.secs);

    EdgeCache.freshness_lifetime_args.cc = &none;
    EdgeCache.freshness_lifetime_args.shared = PROTO_TRUE;
    EdgeCache.freshness_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.freshness_lifetime_args.expires_epoch = NOV6_1994 - 100;
    EdgeCache.freshness_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(-100, EdgeCache.secs);
}

// sec 4.2.2: "If the response has a Last-Modified header field, caches are encouraged to use a
// heuristic expiration value that is no more than some fraction of the interval since that time.
// A typical setting of this fraction might be 10%."
//   86400 / 10 = 8640
//       9 / 10 = 0 (integer tenth of an interval shorter than ten seconds)
// A Last-Modified at or after Date names no elapsed interval, so there is no fraction to take and
// no heuristic is available.
void test_rfc9111_heuristic_freshness_is_a_tenth_of_the_interval(void)
{
    EdgeCache.heuristic_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.heuristic_lifetime_args.last_modified_epoch = NOV6_1994 - 86400;
    EdgeCache.heuristic_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(8640, EdgeCache.secs);
    EdgeCache.heuristic_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.heuristic_lifetime_args.last_modified_epoch = NOV6_1994 - 9;
    EdgeCache.heuristic_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(0, EdgeCache.secs);

    EdgeCache.heuristic_lifetime_args.date_epoch = -1;
    EdgeCache.heuristic_lifetime_args.last_modified_epoch = NOV6_1994;
    EdgeCache.heuristic_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(-1, EdgeCache.secs);
    EdgeCache.heuristic_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.heuristic_lifetime_args.last_modified_epoch = -1;
    EdgeCache.heuristic_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(-1, EdgeCache.secs);
    EdgeCache.heuristic_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.heuristic_lifetime_args.last_modified_epoch = NOV6_1994;
    EdgeCache.heuristic_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(-1, EdgeCache.secs);
    EdgeCache.heuristic_lifetime_args.date_epoch = NOV6_1994;
    EdgeCache.heuristic_lifetime_args.last_modified_epoch = NOV6_1994 + 10;
    EdgeCache.heuristic_lifetime(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(-1, EdgeCache.secs);
}

// sec 4.2.3, verbatim:
//   apparent_age = max(0, response_time - date_value);
//   corrected_age_value = age_value + response_delay;
//   corrected_initial_age = max(apparent_age, corrected_age_value);
// with age_value "0, if not available". This engine has no request_time, so response_delay is 0
// and corrected_age_value is the Age field alone.
//   (age 0,   Date D, response D+40) -> max(40, 0)   = 40
//   (age 500, Date D, response D+40) -> max(40, 500) = 500
//   (age 0,   Date D, response D-40) -> max(0, 0)    = 0   (negative apparent age replaced by zero)
// Without a wall clock there is no response_time and no apparent age, leaving the Age field.
void test_rfc9111_corrected_initial_age(void)
{
    EdgeCache.initial_age_args.age_hdr = 0;
    EdgeCache.initial_age_args.date_epoch = NOV6_1994;
    EdgeCache.initial_age_args.response_time_epoch = NOV6_1994 + 40;
    EdgeCache.initial_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(40, EdgeCache.secs);
    EdgeCache.initial_age_args.age_hdr = 500;
    EdgeCache.initial_age_args.date_epoch = NOV6_1994;
    EdgeCache.initial_age_args.response_time_epoch = NOV6_1994 + 40;
    EdgeCache.initial_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(500, EdgeCache.secs);
    EdgeCache.initial_age_args.age_hdr = 0;
    EdgeCache.initial_age_args.date_epoch = NOV6_1994;
    EdgeCache.initial_age_args.response_time_epoch = NOV6_1994 - 40;
    EdgeCache.initial_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(0, EdgeCache.secs);

    EdgeCache.initial_age_args.age_hdr = 77;
    EdgeCache.initial_age_args.date_epoch = NOV6_1994;
    EdgeCache.initial_age_args.response_time_epoch = -1;
    EdgeCache.initial_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(77, EdgeCache.secs);
    EdgeCache.initial_age_args.age_hdr = -1;
    EdgeCache.initial_age_args.date_epoch = -1;
    EdgeCache.initial_age_args.response_time_epoch = -1;
    EdgeCache.initial_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(0, EdgeCache.secs);
}

// sec 4.2.3, verbatim:
//   resident_time = now - response_time;
//   current_age = corrected_initial_age + resident_time;
// The residency clock here is a free-running 32-bit millisecond counter.
//   (11000 - 1000) ms = 10 s, initial 0  -> 10
//   ( 6000 - 1000) ms =  5 s, initial 30 -> 35
// The counter wraps at 2^32 ms. 0xFFFFF000 + 10000 wraps to 5904, and the unsigned difference is
// still 10000 ms, so residency stays 10 s instead of jumping ~49 days.
void test_rfc9111_current_age_over_a_wrapping_millisecond_clock(void)
{
    EdgeCache.current_age_args.initial_age = 0;
    EdgeCache.current_age_args.insert_ms = 1000u;
    EdgeCache.current_age_args.now_ms = 11000u;
    EdgeCache.current_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(10, EdgeCache.secs);
    EdgeCache.current_age_args.initial_age = 30;
    EdgeCache.current_age_args.insert_ms = 1000u;
    EdgeCache.current_age_args.now_ms = 6000u;
    EdgeCache.current_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(35, EdgeCache.secs);
    EdgeCache.current_age_args.initial_age = 0;
    EdgeCache.current_age_args.insert_ms = 0xFFFFF000u;
    EdgeCache.current_age_args.now_ms = 0xFFFFF000u + 10000u;
    EdgeCache.current_age(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(10, EdgeCache.secs);
}

// sec 4.2 prints the predicate: "response_is_fresh = (freshness_lifetime > current_age)".
// Strictly greater, so the edge second is stale: lifetime 60 with age 60 is 60 > 60, false. The
// same section defines stale as "a response whose age has exceeded its freshness lifetime" only
// after that formula, and sec 4.2.1 leaves lifetime unknown (-1) when nothing is explicit, which
// is not a fresh state.
void test_rfc9111_fresh_predicate_is_strictly_greater(void)
{
    EdgeCache.is_fresh_at_args.lifetime = 60;
    EdgeCache.is_fresh_at_args.current_age = 59;
    EdgeCache.is_fresh_at(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.is_fresh_at_args.lifetime = 60;
    EdgeCache.is_fresh_at_args.current_age = 60;
    EdgeCache.is_fresh_at(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
    EdgeCache.is_fresh_at_args.lifetime = 60;
    EdgeCache.is_fresh_at_args.current_age = 61;
    EdgeCache.is_fresh_at(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
    EdgeCache.is_fresh_at_args.lifetime = -1;
    EdgeCache.is_fresh_at_args.current_age = 0;
    EdgeCache.is_fresh_at(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
}

// RFC 9110 sec 4.2.3: "The scheme and host are case-insensitive and normally provided in
// lowercase; all other components are compared in a case-sensitive manner." So two spellings of
// the host must land on one key and two spellings of the path must not - anything else either
// splits one resource across slots or serves /a/B for /a/b.
//
// The key's own layout is this engine's, not a standard's; what is asserted about it is the round
// trip (the returned length is the string it wrote) and the refusal to emit a truncated key, since
// a truncated key aliases two resources.
void test_cache_key_is_canonical(void)
{
    char a[PROTOCORE_EDGE_KEY_MAX];
    char b[PROTOCORE_EDGE_KEY_MAX];

    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "Example.COM";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=1";
    EdgeCache.key_canon_args.include_query = PROTO_TRUE;
    EdgeCache.key_canon_args.out = a;
    EdgeCache.key_canon_args.out_cap = sizeof(a);
    EdgeCache.key_canon(edge_cache_work);
    size_t n = EdgeCache.n;
    TEST_ASSERT_EQUAL_UINT(strlen(a), n);

    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "EXAMPLE.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=1";
    EdgeCache.key_canon_args.include_query = PROTO_TRUE;
    EdgeCache.key_canon_args.out = b;
    EdgeCache.key_canon_args.out_cap = sizeof(b);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    TEST_ASSERT_EQUAL_STRING(a, b); // host case-insensitive

    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/b";
    EdgeCache.key_canon_args.query = "q=1";
    EdgeCache.key_canon_args.include_query = PROTO_TRUE;
    EdgeCache.key_canon_args.out = b;
    EdgeCache.key_canon_args.out_cap = sizeof(b);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b)); // path case-sensitive

    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=1";
    EdgeCache.key_canon_args.include_query = PROTO_TRUE;
    EdgeCache.key_canon_args.out = a;
    EdgeCache.key_canon_args.out_cap = sizeof(a);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    EdgeCache.key_canon_args.method = "HEAD";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=1";
    EdgeCache.key_canon_args.include_query = PROTO_TRUE;
    EdgeCache.key_canon_args.out = b;
    EdgeCache.key_canon_args.out_cap = sizeof(b);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b)); // the method is part of the key

    // Excluding the query collapses the two queries onto one key, and including it separates them.
    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=1";
    EdgeCache.key_canon_args.include_query = PROTO_FALSE;
    EdgeCache.key_canon_args.out = a;
    EdgeCache.key_canon_args.out_cap = sizeof(a);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=2";
    EdgeCache.key_canon_args.include_query = PROTO_FALSE;
    EdgeCache.key_canon_args.out = b;
    EdgeCache.key_canon_args.out_cap = sizeof(b);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    TEST_ASSERT_EQUAL_STRING(a, b);
    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=1";
    EdgeCache.key_canon_args.include_query = PROTO_TRUE;
    EdgeCache.key_canon_args.out = a;
    EdgeCache.key_canon_args.out_cap = sizeof(a);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = "q=2";
    EdgeCache.key_canon_args.include_query = PROTO_TRUE;
    EdgeCache.key_canon_args.out = b;
    EdgeCache.key_canon_args.out_cap = sizeof(b);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b));

    char small[8];
    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = "example.com";
    EdgeCache.key_canon_args.path = "/a/B";
    EdgeCache.key_canon_args.query = NULL;
    EdgeCache.key_canon_args.include_query = PROTO_FALSE;
    EdgeCache.key_canon_args.out = small;
    EdgeCache.key_canon_args.out_cap = sizeof(small);
    EdgeCache.key_canon(edge_cache_work);
    TEST_ASSERT_EQUAL_UINT(0u, EdgeCache.n);
}

// RFC 6234 sec 8.5 prints the SHA-256 vector for TEST1 = "abc" as
//   BA7816BF 8F01CFEA 414140DE 5DAE2223 B00361A3 96177A9C B410FF61 F20015AD
// (the same value FIPS 180-4 Appendix B.1 works through by hand). The digest doubles as the L2
// dbm key, so a wrong one silently misfiles every spilled entry.
void test_key_digest_matches_the_published_sha256_vector(void)
{
    static const uint8_t WANT[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                     0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                     0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    static uint8_t work[PROTOCORE_SHA256_BORROW + 1];
    uint8_t got[32];

    EdgeCache.key_digest_args.digest_work = work;
    EdgeCache.key_digest_args.canon = "abc";
    EdgeCache.key_digest_args.len = 3;
    EdgeCache.key_digest_args.digest = got;
    EdgeCache.key_digest(edge_cache_work);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, got, 32);
}

static const char *lookup_accept_gzip(void *ctx, const char *name)
{
    (void)ctx;
    return strcmp(name, "accept-encoding") == 0 ? "gzip" : NULL;
}

static const char *lookup_accept_br(void *ctx, const char *name)
{
    (void)ctx;
    return strcmp(name, "accept-encoding") == 0 ? "br" : NULL;
}

static const char *lookup_nothing(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    return NULL;
}

static const char *lookup_empty(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    return "";
}

// RFC 9111 sec 4.1 defines the secondary key by matching, not by a serialization: a stored
// response is reusable only if "all the presented request header fields nominated by that Vary
// field value match those fields in the original request", and "If (after any normalization that
// might take place) a header field is absent from a request, it can only match another request if
// it is also absent there." So the serialized form must separate gzip from br, and both from an
// absent Accept-Encoding, and an absent field from a present-but-empty one. Those are the
// properties asserted; the octets in between are this engine's business.
//
// The nominated names are field names, which RFC 9110 sec 5.1 makes case-insensitive, so the two
// spellings of the Vary header select one variant. And "A stored response with a Vary header field
// value containing a member '*' always fails to match", which is a refusal, not a key.
void test_rfc9111_vary_secondary_key(void)
{
    char gzip[PROTOCORE_EDGE_VARY_MAX];
    char br[PROTOCORE_EDGE_VARY_MAX];
    char absent[PROTOCORE_EDGE_VARY_MAX];
    char present_empty[PROTOCORE_EDGE_VARY_MAX];

    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = gzip;
    EdgeCache.vary_serialize_args.out_cap = sizeof(gzip);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_br;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = br;
    EdgeCache.vary_serialize_args.out_cap = sizeof(br);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_nothing;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = absent;
    EdgeCache.vary_serialize_args.out_cap = sizeof(absent);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_empty;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = present_empty;
    EdgeCache.vary_serialize_args.out_cap = sizeof(present_empty);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);

    TEST_ASSERT_NOT_EQUAL(0, strcmp(gzip, br));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(gzip, absent));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(absent, present_empty));

    char spelled[PROTOCORE_EDGE_VARY_MAX];
    EdgeCache.vary_serialize_args.vary_header = "accept-encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = spelled;
    EdgeCache.vary_serialize_args.out_cap = sizeof(spelled);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING(gzip, spelled);

    // No Vary nominates no field, so every request matches: one key, and it is empty.
    char nothing[PROTOCORE_EDGE_VARY_MAX];
    EdgeCache.vary_serialize_args.vary_header = NULL;
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = nothing;
    EdgeCache.vary_serialize_args.out_cap = sizeof(nothing);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING("", nothing);
    EdgeCache.vary_serialize_args.vary_header = "";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = nothing;
    EdgeCache.vary_serialize_args.out_cap = sizeof(nothing);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_STRING("", nothing);

    EdgeCache.vary_serialize_args.vary_header = "*";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = nothing;
    EdgeCache.vary_serialize_args.out_cap = sizeof(nothing);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);

    // Two nominated names cannot serialize like one, or the second field stops selecting.
    char two[PROTOCORE_EDGE_VARY_MAX];
    char one[PROTOCORE_EDGE_VARY_MAX];
    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding, Accept";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = two;
    EdgeCache.vary_serialize_args.out_cap = sizeof(two);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = one;
    EdgeCache.vary_serialize_args.out_cap = sizeof(one);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(two, one));
}

static EdgeCacheStore g_store;

static EdgeEntry *store(const char *canon, const char *vary_key)
{
    EdgeCache.store_alloc_args.s = &g_store;
    EdgeCache.store_alloc_args.canon = canon;
    EdgeCache.store_alloc_args.vary_key = vary_key;
    EdgeCache.store_alloc(edge_cache_work);
    return EdgeCache.entry;
}

// Store and retrieve: what went in under a (key, vary-key) pair comes back out under exactly that
// pair and under no other. A key that does not fit PROTOCORE_EDGE_KEY_MAX is refused outright,
// since a stored prefix would answer for every key sharing it.
void test_store_alloc_and_lookup(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    EdgeEntry *a = store("GET\nexample.com\n/a", "");
    TEST_ASSERT_NOT_NULL(a);
    a->status = 200;

    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "GET\nexample.com\n/a";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_EQUAL_PTR(a, EdgeCache.entry);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "GET\nexample.com\n/b";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NULL(EdgeCache.entry);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "GET\nexample.com\n/a";
    EdgeCache.store_lookup_args.vary_key = "gzip";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NULL(EdgeCache.entry);

    EdgeEntry *b = store("GET\nexample.com\n/a", "gzip");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "GET\nexample.com\n/a";
    EdgeCache.store_lookup_args.vary_key = "gzip";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_EQUAL_PTR(b, EdgeCache.entry);
    TEST_ASSERT_EQUAL_UINT32(2u, g_store.stats.stores);

    char huge[PROTOCORE_EDGE_KEY_MAX + 8];
    memset(huge, 'k', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    TEST_ASSERT_NULL(store(huge, ""));
}

// The eviction order is the property: with the pool full, the next store displaces the entry
// touched longest ago and no other. Filling the pool exactly evicts nothing; a lookup of the
// oldest entry makes the second-oldest the victim.
void test_store_evicts_the_least_recently_used_slot(void)
{
    char key[32];
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    for (int i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        key[0] = '/';
        key[1] = (char)('0' + i);
        key[2] = '\0';
        TEST_ASSERT_NOT_NULL(store(key, ""));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, g_store.stats.evictions);

    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "/0";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 100u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NOT_NULL(EdgeCache.entry);

    TEST_ASSERT_NOT_NULL(store("/new", ""));
    TEST_ASSERT_EQUAL_UINT32(1u, g_store.stats.evictions);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "/0";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 200u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NOT_NULL(EdgeCache.entry);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "/1";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 200u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NULL(EdgeCache.entry);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "/new";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 200u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NOT_NULL(EdgeCache.entry);
}

// RFC 9111 sec 4.1: the cache may reuse a stored response only when the request's nominated
// fields match the request that stored it. Two variants of one key are stored under Accept-Encoding
// gzip and br; a gzip request selects the gzip variant, a br request the br one, and a request
// carrying no Accept-Encoding selects neither, because "a header field ... absent from a request
// ... can only match another request if it is also absent there".
void test_rfc9111_store_find_resolves_the_vary_variant(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    char vk[PROTOCORE_EDGE_VARY_MAX];

    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_gzip;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = vk;
    EdgeCache.vary_serialize_args.out_cap = sizeof(vk);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeEntry *g = store("GET\nexample.com\n/a", vk);
    TEST_ASSERT_NOT_NULL(g);
    memcpy(g->vary_names, "Accept-Encoding", sizeof("Accept-Encoding"));

    EdgeCache.vary_serialize_args.vary_header = "Accept-Encoding";
    EdgeCache.vary_serialize_args.lookup = lookup_accept_br;
    EdgeCache.vary_serialize_args.ctx = NULL;
    EdgeCache.vary_serialize_args.out = vk;
    EdgeCache.vary_serialize_args.out_cap = sizeof(vk);
    EdgeCache.vary_serialize(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeEntry *b = store("GET\nexample.com\n/a", vk);
    TEST_ASSERT_NOT_NULL(b);
    memcpy(b->vary_names, "Accept-Encoding", sizeof("Accept-Encoding"));

    EdgeCache.store_find_args.s = &g_store;
    EdgeCache.store_find_args.canon = "GET\nexample.com\n/a";
    EdgeCache.store_find_args.lookup = lookup_accept_gzip;
    EdgeCache.store_find_args.ctx = NULL;
    EdgeCache.store_find_args.now_ms = 0u;
    EdgeCache.store_find(edge_cache_work);
    TEST_ASSERT_EQUAL_PTR(g, EdgeCache.entry);
    EdgeCache.store_find_args.s = &g_store;
    EdgeCache.store_find_args.canon = "GET\nexample.com\n/a";
    EdgeCache.store_find_args.lookup = lookup_accept_br;
    EdgeCache.store_find_args.ctx = NULL;
    EdgeCache.store_find_args.now_ms = 0u;
    EdgeCache.store_find(edge_cache_work);
    TEST_ASSERT_EQUAL_PTR(b, EdgeCache.entry);
    EdgeCache.store_find_args.s = &g_store;
    EdgeCache.store_find_args.canon = "GET\nexample.com\n/a";
    EdgeCache.store_find_args.lookup = lookup_nothing;
    EdgeCache.store_find_args.ctx = NULL;
    EdgeCache.store_find_args.now_ms = 0u;
    EdgeCache.store_find(edge_cache_work);
    TEST_ASSERT_NULL(EdgeCache.entry);
}

// A purge removes every variant of the named key and nothing outside it; a prefix purge removes
// every key whose path starts with the prefix and nothing outside it. The counts returned are the
// numbers removed, and the purge counter is their sum.
void test_store_purge_by_key_and_by_path_prefix(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/img/a.png", ""));
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/img/a.png", "gzip"));
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/img/b.png", ""));
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/css/c.css", ""));

    EdgeCache.store_purge_args.s = &g_store;
    EdgeCache.store_purge_args.canon = "GET\nexample.com\n/img/a.png";
    EdgeCache.store_purge(edge_cache_work);
    TEST_ASSERT_EQUAL_UINT32(2u, EdgeCache.count);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "GET\nexample.com\n/img/a.png";
    EdgeCache.store_lookup_args.vary_key = "gzip";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NULL(EdgeCache.entry);

    EdgeCache.store_purge_prefix_args.s = &g_store;
    EdgeCache.store_purge_prefix_args.prefix = "/img/";
    EdgeCache.store_purge_prefix(edge_cache_work);
    TEST_ASSERT_EQUAL_UINT32(1u, EdgeCache.count);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "GET\nexample.com\n/css/c.css";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NOT_NULL(EdgeCache.entry);
    TEST_ASSERT_EQUAL_UINT32(3u, g_store.stats.purges);
}

// RFC 9111 sec 4.3 lets a cache reuse a stale response by validating it against the origin, and
// sec 4.3.1 says the preconditions come from the stored response's validators. A stale entry that
// carries a validator is therefore still worth keeping; a stale entry with none can only be
// refetched in full, so the sweep drops exactly that set and leaves fresh entries alone.
//
// Freshness follows sec 4.2: lifetime 10 s with initial age 0 is fresh at 9 s of residency and
// stale at 10 s (10 > 10 is false).
void test_sweep_drops_only_unrevalidatable_stale_entries(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    EdgeEntry *dead = store("/dead", "");
    dead->lifetime_s = 10;
    dead->initial_age = 0;
    dead->insert_ms = 0u;

    EdgeEntry *keep = store("/keep", "");
    keep->lifetime_s = 10;
    keep->initial_age = 0;
    keep->insert_ms = 0u;
    memcpy(keep->etag, "\"v1\"", sizeof("\"v1\""));

    EdgeEntry *fresh = store("/fresh", "");
    fresh->lifetime_s = 1000;
    fresh->initial_age = 0;
    fresh->insert_ms = 0u;

    EdgeCache.entry_fresh_args.e = dead;
    EdgeCache.entry_fresh_args.now_ms = 9000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.entry_fresh_args.e = dead;
    EdgeCache.entry_fresh_args.now_ms = 10000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
    EdgeCache.entry_has_validator_args.e = keep;
    EdgeCache.entry_has_validator(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.entry_has_validator_args.e = dead;
    EdgeCache.entry_has_validator(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);

    EdgeCache.store_sweep_args.s = &g_store;
    EdgeCache.store_sweep_args.now_ms = 20000u;
    EdgeCache.store_sweep(edge_cache_work);
    TEST_ASSERT_EQUAL_UINT32(1u, EdgeCache.count);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "/dead";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NULL(EdgeCache.entry);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "/keep";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NOT_NULL(EdgeCache.entry);
    EdgeCache.store_lookup_args.s = &g_store;
    EdgeCache.store_lookup_args.canon = "/fresh";
    EdgeCache.store_lookup_args.vary_key = "";
    EdgeCache.store_lookup_args.now_ms = 0u;
    EdgeCache.store_lookup(edge_cache_work);
    TEST_ASSERT_NOT_NULL(EdgeCache.entry);
}

// RFC 9111 sec 3 lists what a cache MUST NOT store without. Two of its bullets are refusals this
// engine owes whatever else it does: "the no-store cache directive is not present in the
// response", and "if the cache is shared: the private response directive is either not present or
// allows a shared cache to store a modified response". This is a shared cache, so both are hard
// no. sec 4.1 adds that a stored response whose Vary contains "*" always fails to match, so it
// could never be selected again.
//
// The list is a set of necessary conditions, not sufficient ones - a cache is never obliged to
// store anything - so the GET-and-200-only narrowing (a POST or a 404 refused, though sec 3 and
// RFC 9110 sec 15.1 would permit storing a 404) and the body ceiling are this engine's policy,
// asserted as policy. The ceiling is asserted at the boundary: the largest storeable body is
// exactly PROTOCORE_EDGE_BODY_MAX, one more is refused.
void test_rfc9111_storeability(void)
{
    protocore_cache_control cc;
    Httpcache.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = &cc;
    EdgeCache.is_storeable_args.vary_header = NULL;
    EdgeCache.is_storeable_args.body_len = 100;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = NULL;
    EdgeCache.is_storeable_args.vary_header = "Accept-Encoding";
    EdgeCache.is_storeable_args.body_len = 100;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);

    protocore_cache_control ns;
    parse_cc("no-store", &ns);
    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = &ns;
    EdgeCache.is_storeable_args.vary_header = NULL;
    EdgeCache.is_storeable_args.body_len = 100;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);

    protocore_cache_control pv;
    parse_cc("private, max-age=60", &pv);
    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = &pv;
    EdgeCache.is_storeable_args.vary_header = NULL;
    EdgeCache.is_storeable_args.body_len = 100;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);

    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = &cc;
    EdgeCache.is_storeable_args.vary_header = "*";
    EdgeCache.is_storeable_args.body_len = 100;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);

    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "POST";
    EdgeCache.is_storeable_args.cc = &cc;
    EdgeCache.is_storeable_args.vary_header = NULL;
    EdgeCache.is_storeable_args.body_len = 100;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
    EdgeCache.is_storeable_args.status = 404;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = &cc;
    EdgeCache.is_storeable_args.vary_header = NULL;
    EdgeCache.is_storeable_args.body_len = 100;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = &cc;
    EdgeCache.is_storeable_args.vary_header = NULL;
    EdgeCache.is_storeable_args.body_len = PROTOCORE_EDGE_BODY_MAX;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.is_storeable_args.status = 200;
    EdgeCache.is_storeable_args.method = "GET";
    EdgeCache.is_storeable_args.cc = &cc;
    EdgeCache.is_storeable_args.vary_header = NULL;
    EdgeCache.is_storeable_args.body_len = PROTOCORE_EDGE_BODY_MAX + 1;
    EdgeCache.is_storeable(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
}

// RFC 9111 sec 4.3.1: a validating cache "MUST send the relevant entity tags (using If-Match,
// If-None-Match, or If-Range) if the entity tags were provided in the stored response(s)" and
// "SHOULD send the Last-Modified value (using If-Modified-Since) ... if that response contains a
// Last-Modified value", noting that "in most cases, both validators are generated". An entry
// holding neither has nothing to condition on, so it produces no lines at all.
//
// The line form is RFC 9112 sec 5's field-line grammar - field-name ":" OWS field-value OWS - with
// the single SP the same section prefers, terminated by CRLF per sec 2.1. The ETag value keeps its
// quotes because RFC 9110 sec 8.8.3 makes the DQUOTEs part of the entity-tag.
void test_rfc9111_conditional_request_carries_the_stored_validators(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    EdgeEntry *e = store("/x", "");
    char out[256];

    EdgeCache.build_conditional_args.e = e;
    EdgeCache.build_conditional_args.out = out;
    EdgeCache.build_conditional_args.cap = sizeof(out);
    EdgeCache.build_conditional(edge_cache_work);
    TEST_ASSERT_EQUAL_UINT(0u, EdgeCache.n);

    memcpy(e->etag, "\"abc123\"", sizeof("\"abc123\""));
    EdgeCache.build_conditional_args.e = e;
    EdgeCache.build_conditional_args.out = out;
    EdgeCache.build_conditional_args.cap = sizeof(out);
    EdgeCache.build_conditional(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    TEST_ASSERT_EQUAL_STRING("If-None-Match: \"abc123\"\r\n", out);

    memcpy(e->last_modified, "Sun, 06 Nov 1994 08:49:37 GMT", sizeof("Sun, 06 Nov 1994 08:49:37 GMT"));
    EdgeCache.build_conditional_args.e = e;
    EdgeCache.build_conditional_args.out = out;
    EdgeCache.build_conditional_args.cap = sizeof(out);
    EdgeCache.build_conditional(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.n > 0);
    TEST_ASSERT_EQUAL_STRING("If-None-Match: \"abc123\"\r\n"
                             "If-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n",
                             out);

    // A precondition cut in half asks a different question, so a buffer too small emits nothing.
    char small[16];
    EdgeCache.build_conditional_args.e = e;
    EdgeCache.build_conditional_args.out = small;
    EdgeCache.build_conditional_args.cap = sizeof(small);
    EdgeCache.build_conditional(edge_cache_work);
    TEST_ASSERT_EQUAL_UINT(0u, EdgeCache.n);
}

// RFC 9111 sec 4.3.4: on a 304 "the cache MUST update its header fields with the header fields
// provided in the 304 (Not Modified) response, as per Section 3.2", and sec 3.2 replaces the
// values already present. RFC 9110 sec 15.4.5 makes the 304 carry no representation, so the stored
// content survives the update untouched - that is the whole point of validating.
//
// The refreshed freshness is sec 4.2.1 rule 2 over the 304's own Cache-Control (max-age=120) and
// sec 4.2.3 over its Date and Age. Arrival at Date gives apparent_age 0 and no Age field gives
// age_value 0, so the entry restarts fresh. With "Age: 100" the corrected initial age is 100, so
// by sec 4.2 it is fresh while 120 > 100 + residency and stale at 20 s of residency (120 > 120 is
// false).
void test_rfc9111_a_304_freshens_and_keeps_the_stored_content(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    EdgeEntry *e = store("/x", "");
    memcpy(e->etag, "\"old\"", sizeof("\"old\""));
    e->body_len = 3;
    e->body[0] = 'a';
    e->lifetime_s = 0;
    e->insert_ms = 0u;
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = 1000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);

    static const char *const NOT_MODIFIED = "HTTP/1.1 304 Not Modified\r\n"
                                            "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                                            "Cache-Control: max-age=120\r\n"
                                            "ETag: \"new\"\r\n"
                                            "\r\n";
    EdgeCache.apply_304_args.e = e;
    EdgeCache.apply_304_args.new_hdrs = NOT_MODIFIED;
    EdgeCache.apply_304_args.hdr_len = strlen(NOT_MODIFIED);
    EdgeCache.apply_304_args.response_time_epoch = NOV6_1994;
    EdgeCache.apply_304_args.now_ms = 5000u;
    EdgeCache.apply_304(edge_cache_work);

    TEST_ASSERT_EQUAL_STRING("\"new\"", e->etag);
    TEST_ASSERT_EQUAL_INT32(120, e->lifetime_s);
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, e->date_epoch);
    TEST_ASSERT_EQUAL_INT32(0, e->initial_age);
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = 5000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    TEST_ASSERT_EQUAL_UINT(3u, e->body_len);
    TEST_ASSERT_EQUAL_CHAR('a', (char)e->body[0]);

    static const char *const AGED = "HTTP/1.1 304 Not Modified\r\n"
                                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                                    "Cache-Control: max-age=120\r\n"
                                    "Age: 100\r\n"
                                    "\r\n";
    EdgeCache.apply_304_args.e = e;
    EdgeCache.apply_304_args.new_hdrs = AGED;
    EdgeCache.apply_304_args.hdr_len = strlen(AGED);
    EdgeCache.apply_304_args.response_time_epoch = NOV6_1994;
    EdgeCache.apply_304_args.now_ms = 5000u;
    EdgeCache.apply_304(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(100, e->initial_age);
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = 5000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = 5000u + 20000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
}

// RFC 9111 sec 4.2.1 rule 4 leaves the lifetime unset when nothing is explicit, and sec 4.2.2
// permits a heuristic in that case. With neither directive, Expires, nor Last-Modified there is
// nothing to compute from, so the engine falls back to its configured TTL - a policy value, so it
// is asserted against the knob and not against a number.
//
// Given a Last-Modified, sec 4.2.2's typical tenth applies instead: 3600 / 10 = 360.
void test_freshness_falls_back_to_the_default_ttl(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    EdgeEntry *e = store("/x", "");
    protocore_cache_control cc;
    Httpcache.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);

    EdgeCache.entry_set_freshness_args.e = e;
    EdgeCache.entry_set_freshness_args.cc = &cc;
    EdgeCache.entry_set_freshness_args.shared = PROTO_TRUE;
    EdgeCache.entry_set_freshness_args.date_epoch = -1;
    EdgeCache.entry_set_freshness_args.expires_epoch = -1;
    EdgeCache.entry_set_freshness_args.last_modified_epoch = -1;
    EdgeCache.entry_set_freshness_args.age_hdr = 0;
    EdgeCache.entry_set_freshness_args.response_time_epoch = -1;
    EdgeCache.entry_set_freshness_args.now_ms = 0u;
    EdgeCache.entry_set_freshness(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_EDGE_DEFAULT_TTL_S, e->lifetime_s);

    EdgeCache.entry_set_freshness_args.e = e;
    EdgeCache.entry_set_freshness_args.cc = &cc;
    EdgeCache.entry_set_freshness_args.shared = PROTO_TRUE;
    EdgeCache.entry_set_freshness_args.date_epoch = NOV6_1994;
    EdgeCache.entry_set_freshness_args.expires_epoch = -1;
    EdgeCache.entry_set_freshness_args.last_modified_epoch = NOV6_1994 - 3600;
    EdgeCache.entry_set_freshness_args.age_hdr = 0;
    EdgeCache.entry_set_freshness_args.response_time_epoch = -1;
    EdgeCache.entry_set_freshness_args.now_ms = 0u;
    EdgeCache.entry_set_freshness(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(360, e->lifetime_s);
}

// RFC 9111 sec 4.2: "If an origin server wishes to force a cache to validate every request, it can
// assign an explicit expiration time in the past to indicate that the response is already stale."
// sec 4.2.2 forbids a heuristic then - "A cache MUST NOT use heuristics to determine freshness when
// an explicit expiration time is present in the stored response" - so an Expires 100 s before Date
// must store stale, not pick up the 8640 s tenth that its day-old Last-Modified would otherwise
// supply.
//
// Expires 100 s after Date is the same rule with the sign flipped: lifetime 100 s, fresh at 99 s of
// residency and stale at 100 (100 > 100 is false).
void test_an_expires_in_the_past_stores_as_stale(void)
{
    EdgeCache.store_init_args.s = &g_store;
    EdgeCache.store_init(edge_cache_work);
    EdgeEntry *e = store("/x", "");
    protocore_cache_control cc;
    Httpcache.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);

    EdgeCache.entry_set_freshness_args.e = e;
    EdgeCache.entry_set_freshness_args.cc = &cc;
    EdgeCache.entry_set_freshness_args.shared = PROTO_TRUE;
    EdgeCache.entry_set_freshness_args.date_epoch = NOV6_1994;
    EdgeCache.entry_set_freshness_args.expires_epoch = NOV6_1994 - 100;
    EdgeCache.entry_set_freshness_args.last_modified_epoch = NOV6_1994 - 86400;
    EdgeCache.entry_set_freshness_args.age_hdr = 0;
    EdgeCache.entry_set_freshness_args.response_time_epoch = -1;
    EdgeCache.entry_set_freshness_args.now_ms = 0u;
    EdgeCache.entry_set_freshness(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(0, e->lifetime_s);
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = 0u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);

    EdgeCache.entry_set_freshness_args.e = e;
    EdgeCache.entry_set_freshness_args.cc = &cc;
    EdgeCache.entry_set_freshness_args.shared = PROTO_TRUE;
    EdgeCache.entry_set_freshness_args.date_epoch = NOV6_1994;
    EdgeCache.entry_set_freshness_args.expires_epoch = NOV6_1994 + 100;
    EdgeCache.entry_set_freshness_args.last_modified_epoch = NOV6_1994 - 86400;
    EdgeCache.entry_set_freshness_args.age_hdr = 0;
    EdgeCache.entry_set_freshness_args.response_time_epoch = -1;
    EdgeCache.entry_set_freshness_args.now_ms = 0u;
    EdgeCache.entry_set_freshness(edge_cache_work);
    TEST_ASSERT_EQUAL_INT32(100, e->lifetime_s);
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = 99000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_TRUE(EdgeCache.ok);
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = 100000u;
    EdgeCache.entry_fresh(edge_cache_work);
    TEST_ASSERT_FALSE(EdgeCache.ok);
}
