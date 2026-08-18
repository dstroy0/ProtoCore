// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HTTP delivery cores (services/file_transfer/http_delivery/http_delivery.h).
//
// The load-bearing case is test_rfc5861_worked_example, built on the response RFC 5861 sec 3.1
// prints - "Cache-Control: max-age=600, stale-while-revalidate=30" - and on the two boundaries the
// documents state outright:
//
//   * the outer edge is settled. RFC 5861 sec 4 says the extension's "value indicates the upper
//     limit to staleness; when the cached response is more stale than the indicated amount, the
//     cached response SHOULD NOT be used", and sec 4.1 works it out for max-age=600 + 1200: "After
//     the age is greater than 1800 seconds (i.e., it has been stale for 1200 seconds), the cache
//     must write the error message through". sec 3.1 says the same for the swr window - both set to
//     600 means "served from cache for up to 20 minutes", i.e. through age 1200. So age
//     max-age + window is still servable and age max-age + window + 1 is not.
//
//   * the inner edge - the single second where age equals max-age - is NOT settled, and no
//     assertion here claims it. RFC 9111 sec 4.2 opens "A 'fresh' response is one whose age has not
//     yet exceeded its freshness lifetime", which makes that second fresh, and RFC 9111 sec 5.2.2.1
//     agrees ("the response is to be considered stale after its age is greater than the specified
//     number of seconds"); but the calculation printed in that same sec 4.2,
//     "response_is_fresh = (freshness_lifetime > current_age)", makes it stale. The two readings do
//     agree that the second is not past both windows, so that is all these cases assert about it.
//     A test that picked one would be picking it out of the implementation.
//
// The Cache-Control directive spellings are RFC 9111 sec 5.2.2.1 (max-age), sec 5.2.2.9 (public) and
// RFC 5861 sec 3 (stale-while-revalidate), their arguments are the delta-seconds rule of RFC 9111
// sec 1.2.2 (1*DIGIT), and the "public, " prefix and the directive order are http_delivery.h's own
// documented output format, not an RFC requirement - RFC 9111 sec 5.2 leaves the list unordered.
//
// The manifest member names, their order and the array form are likewise http_delivery.h's
// published format (its @brief prints the whole document). What RFC 8259 governs is the string
// escaping, and only the parts sec 7 determines are asserted as bytes: it forbids a raw octet below
// U+0020 outright, and 0x07 has no two-character form in its ABNF, so \u0007 is the only conforming
// spelling of it. Where sec 7 permits two spellings the case says so.

#include "services/file_transfer/http_delivery/http_delivery.h"
#include <string.h>

#include <unity.h>

static uint8_t http_delivery_work[16]; // the borrow an entry takes; HttpDelivery never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 5861 sec 3.1: "Cache-Control: max-age=600, stale-while-revalidate=30 ... indicates that it is
// fresh for 600 seconds, and it may continue to be served stale for up to an additional 30 seconds".
// Below 600 every reading agrees the response is fresh; at 600 the two readings disagree and only
// "not past both windows" is asserted; from 601 it is stale, and the last servable second is
// 600 + 30 = 630, so 631 is the first that must revalidate before serving.
void test_rfc5861_worked_example(void)
{
    HttpDelivery.swr_args.age_s = 0;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 1;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 599;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, HttpDelivery.value);

    HttpDelivery.swr_args.age_s = 600;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_NOT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);

    HttpDelivery.swr_args.age_s = 601;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 629;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 630;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 631;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 100000;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 30;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);
}

// The two totals RFC 5861 works out in full.
//   sec 3.1: max-age and stale-while-revalidate "both set to 600" means the response may be "served
//            from cache for up to 20 minutes" - 20 * 60 = 1200 = 600 + 600.
//   sec 4.1: max-age=600 with a 1200-second stale window, "After the age is greater than 1800
//            seconds (i.e., it has been stale for 1200 seconds), the cache must write ... through" -
//            1800 = 600 + 1200, and the write-through starts only past it.
void test_rfc5861_published_totals(void)
{
    HttpDelivery.swr_args.age_s = 1199;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 600;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 1200;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 600;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 1201;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 600;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);

    HttpDelivery.swr_args.age_s = 1799;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 1200;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 1800;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 1200;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 1801;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 1200;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);
}

// What every reading of RFC 9111 sec 4.2 and sec 5.2.2.1 agrees on, over the whole range of
// max-age: one second short of max-age the response is fresh, one second past it is not.
void test_the_second_past_max_age_is_never_fresh(void)
{
    static const uint32_t MAX_AGE[] = {1u, 2u, 60u, 600u, 86400u, 2147483648u, 4294967294u};

    for (size_t i = 0; i < sizeof(MAX_AGE) / sizeof(MAX_AGE[0]); i++)
    {
        const uint32_t m = MAX_AGE[i];
        HttpDelivery.swr_args.age_s = m - 1u;
        HttpDelivery.swr_args.max_age_s = m;
        HttpDelivery.swr_args.swr_s = 10u;
        HttpDelivery.swr(http_delivery_work);
        TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, HttpDelivery.value);
        HttpDelivery.swr_args.age_s = m + 1u;
        HttpDelivery.swr_args.max_age_s = m;
        HttpDelivery.swr_args.swr_s = 10u;
        HttpDelivery.swr(http_delivery_work);
        TEST_ASSERT_NOT_EQUAL_INT(DELIVERY_FRESH, HttpDelivery.value);
    }
}

// RFC 5861 sec 3 gives the stale allowance a length: caches "MAY serve the response ... after it
// becomes stale, up to the indicated number of seconds". A window of zero seconds indicates no
// allowance, so one second past max-age must already revalidate.
void test_a_zero_stale_window_has_no_stale_band(void)
{
    HttpDelivery.swr_args.age_s = 599;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 0;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 600;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 0;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_NOT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 601;
    HttpDelivery.swr_args.max_age_s = 600;
    HttpDelivery.swr_args.swr_s = 0;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);

    HttpDelivery.swr_args.age_s = 0;
    HttpDelivery.swr_args.max_age_s = 0;
    HttpDelivery.swr_args.swr_s = 0;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_NOT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = 1;
    HttpDelivery.swr_args.max_age_s = 0;
    HttpDelivery.swr_args.swr_s = 0;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);
}

// Age only ever moves the verdict one way: a response that has revalidation owed to it never gets it
// back by getting older. The enum's own order (FRESH 0, STALE_REVALIDATE 1, EXPIRED 2) is that
// progression, so the walk is a plain compare.
void test_verdict_is_monotonic_in_age(void)
{
    DeliveryVerdict prev = DELIVERY_FRESH;
    for (uint32_t age = 0; age <= 200; age++)
    {
        HttpDelivery.swr_args.age_s = age;
        HttpDelivery.swr_args.max_age_s = 100;
        HttpDelivery.swr_args.swr_s = 50;
        HttpDelivery.swr(http_delivery_work);
        DeliveryVerdict v = HttpDelivery.value;
        TEST_ASSERT_TRUE(v >= prev);
        prev = v;
    }
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, prev);
}

// max-age + window is a sum of two 32-bit delta-seconds and can leave the range; if it were folded
// back into 32 bits the total would collapse to a small number and both of these would read EXPIRED.
void test_the_window_sum_does_not_wrap(void)
{
    const uint32_t huge = 0xFFFFFFFFu;
    HttpDelivery.swr_args.age_s = huge;
    HttpDelivery.swr_args.max_age_s = huge - 1u;
    HttpDelivery.swr_args.swr_s = 2u;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, HttpDelivery.value);
    HttpDelivery.swr_args.age_s = huge;
    HttpDelivery.swr_args.max_age_s = huge;
    HttpDelivery.swr_args.swr_s = 1u;
    HttpDelivery.swr(http_delivery_work);
    TEST_ASSERT_NOT_EQUAL_INT(DELIVERY_EXPIRED, HttpDelivery.value);
}

// RFC 5861 sec 3.1 prints the field value "max-age=600, stale-while-revalidate=30"; the module
// prepends the RFC 9111 sec 5.2.2.9 public directive, using the RFC 9110 sec 5.6.1 list separator.
void test_rfc5861_example_cache_control_value(void)
{
    char out[64];
    HttpDelivery.cache_control_args.max_age_s = 600;
    HttpDelivery.cache_control_args.swr_s = 30;
    HttpDelivery.cache_control_args.out = out;
    HttpDelivery.cache_control_args.cap = sizeof(out);
    HttpDelivery.cache_control(http_delivery_work);
    size_t n = HttpDelivery.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "max-age=600, stale-while-revalidate=30"));
    TEST_ASSERT_EQUAL_STRING("public, max-age=600, stale-while-revalidate=30", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);
}

// http_delivery.h: "The swr directive is omitted when @p swr_s is 0." Both arguments are
// delta-seconds (RFC 9111 sec 1.2.2, 1*DIGIT), so zero renders as "0" and the largest uint32 renders
// with all ten of its digits rather than being clipped.
void test_cache_control_directive_forms(void)
{
    char out[80];

    HttpDelivery.cache_control_args.max_age_s = 3600;
    HttpDelivery.cache_control_args.swr_s = 0;
    HttpDelivery.cache_control_args.out = out;
    HttpDelivery.cache_control_args.cap = sizeof(out);
    HttpDelivery.cache_control(http_delivery_work);
    TEST_ASSERT_TRUE(HttpDelivery.n > 0);
    TEST_ASSERT_EQUAL_STRING("public, max-age=3600", out);
    TEST_ASSERT_NULL(strstr(out, "stale-while-revalidate"));

    HttpDelivery.cache_control_args.max_age_s = 0;
    HttpDelivery.cache_control_args.swr_s = 0;
    HttpDelivery.cache_control_args.out = out;
    HttpDelivery.cache_control_args.cap = sizeof(out);
    HttpDelivery.cache_control(http_delivery_work);
    TEST_ASSERT_TRUE(HttpDelivery.n > 0);
    TEST_ASSERT_EQUAL_STRING("public, max-age=0", out);

    HttpDelivery.cache_control_args.max_age_s = 4294967295u;
    HttpDelivery.cache_control_args.swr_s = 4294967295u;
    HttpDelivery.cache_control_args.out = out;
    HttpDelivery.cache_control_args.cap = sizeof(out);
    HttpDelivery.cache_control(http_delivery_work);
    TEST_ASSERT_TRUE(HttpDelivery.n > 0);
    TEST_ASSERT_EQUAL_STRING("public, max-age=4294967295, stale-while-revalidate=4294967295", out);
}

// The value is returned as a C string, so the buffer has to hold the octets and the terminator.
// "public, " (8) + "max-age=" (8) + "600" (3) + ", " (2) + "stale-while-revalidate=" (23) + "30" (2)
// = 46 octets, so 47 is the smallest buffer that works and 46 must refuse rather than truncate.
void test_cache_control_needs_room_for_the_terminator(void)
{
    char out[64];
    char exact[47];
    char tight[46];

    HttpDelivery.cache_control_args.max_age_s = 600;
    HttpDelivery.cache_control_args.swr_s = 30;
    HttpDelivery.cache_control_args.out = out;
    HttpDelivery.cache_control_args.cap = sizeof(out);
    HttpDelivery.cache_control(http_delivery_work);
    size_t need = HttpDelivery.n;
    TEST_ASSERT_EQUAL_UINT(46u, need);
    TEST_ASSERT_EQUAL_UINT(strlen(out), need);

    HttpDelivery.cache_control_args.max_age_s = 600;
    HttpDelivery.cache_control_args.swr_s = 30;
    HttpDelivery.cache_control_args.out = exact;
    HttpDelivery.cache_control_args.cap = sizeof(exact);
    HttpDelivery.cache_control(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(need, HttpDelivery.n);
    TEST_ASSERT_EQUAL_STRING(out, exact);
    HttpDelivery.cache_control_args.max_age_s = 600;
    HttpDelivery.cache_control_args.swr_s = 30;
    HttpDelivery.cache_control_args.out = tight;
    HttpDelivery.cache_control_args.cap = sizeof(tight);
    HttpDelivery.cache_control(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);
    HttpDelivery.cache_control_args.max_age_s = 600;
    HttpDelivery.cache_control_args.swr_s = 30;
    HttpDelivery.cache_control_args.out = out;
    HttpDelivery.cache_control_args.cap = 0;
    HttpDelivery.cache_control(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);
    HttpDelivery.cache_control_args.max_age_s = 600;
    HttpDelivery.cache_control_args.swr_s = 30;
    HttpDelivery.cache_control_args.out = NULL;
    HttpDelivery.cache_control_args.cap = sizeof(out);
    HttpDelivery.cache_control(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);
}

// http_delivery.h publishes the whole document: {"version":"..","precache":["/a","/b",...]}. RFC 8259
// sec 4 and sec 5 make the object and array separators structural and any whitespace between them
// insignificant, and this emits none, so the document is that shape octet for octet.
void test_manifest_documented_shape(void)
{
    static const char *const PATHS[] = {"/", "/app.js", "/app.css"};
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];

    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 3;
    HttpDelivery.sw_manifest_args.version = "v1.2.3";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    size_t n = HttpDelivery.n;
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v1.2.3\",\"precache\":[\"/\",\"/app.js\",\"/app.css\"]}", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);

    HttpDelivery.sw_manifest_args.paths = NULL;
    HttpDelivery.sw_manifest_args.n = 0;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_TRUE(HttpDelivery.n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v1\",\"precache\":[]}", out);
}

// RFC 8259 sec 7: "All Unicode characters may be placed within the quotation marks, except for the
// characters that MUST be escaped: quotation mark, reverse solidus, and the control characters
// (U+0000 through U+001F)", and its grammar spells that out as
// unescaped = %x20-21 / %x23-5B / %x5D-10FFFF. So no octet below 0x20 may survive into the document
// whatever escape form is chosen - which is what the scan asserts, over every control character a C
// string can carry.
void test_manifest_leaves_no_raw_control_character(void)
{
    char ctrl[32];
    const char *paths[1];
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];

    for (unsigned c = 1; c <= 31; c++)
    {
        ctrl[c - 1u] = (char)c;
    }
    ctrl[31] = '\0';
    paths[0] = ctrl;

    HttpDelivery.sw_manifest_args.paths = paths;
    HttpDelivery.sw_manifest_args.n = 1;
    HttpDelivery.sw_manifest_args.version = ctrl;
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    size_t n = HttpDelivery.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE((unsigned char)out[i] >= 0x20u, "a raw control octet reached the JSON document");
    }
}

// sec 7's grammar lists two-character forms for exactly \" \\ \/ \b \f \n \r \t. 0x07 is not among
// them, so the six-character \u0007 - "a reverse solidus, followed by the lowercase letter u,
// followed by four hexadecimal digits" - is its only conforming spelling.
void test_manifest_escapes_a_character_with_no_short_form(void)
{
    static const char *const BELL[] = {"/\x07"};
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];

    HttpDelivery.sw_manifest_args.paths = BELL;
    HttpDelivery.sw_manifest_args.n = 1;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_TRUE(HttpDelivery.n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v1\",\"precache\":[\"/\\u0007\"]}", out);
}

// The three characters sec 7 names outright, in the two-character forms its grammar lists. Both the
// short form and \u00XX conform for these, so the exact octets are the module's choice among them;
// what the RFC settles is that none of the three may appear raw.
void test_manifest_escapes_quote_solidus_and_control(void)
{
    static const char *const PATHS[] = {"/a\"b", "/c\\d", "/e\tf"};
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];

    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 3;
    HttpDelivery.sw_manifest_args.version = "v\n1";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_TRUE(HttpDelivery.n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v\\n1\",\"precache\":[\"/a\\\"b\",\"/c\\\\d\",\"/e\\tf\"]}", out);
}

// PROTOCORE_DELIVERY_MANIFEST_BUF is sized in protocore_config.h to hold a manifest of
// PROTOCORE_DELIVERY_PRECACHE_MAX paths; a full list must therefore come back whole, ending in the
// end-object octet RFC 8259 sec 4 requires.
void test_full_precache_list_fits_the_configured_buffer(void)
{
    const char *paths[PROTOCORE_DELIVERY_PRECACHE_MAX];
    static const char NAMES[PROTOCORE_DELIVERY_PRECACHE_MAX][4] = {"/a", "/b", "/c", "/d", "/e", "/f", "/g", "/h",
                                                                   "/i", "/j", "/k", "/l", "/m", "/n", "/o", "/p"};
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];

    for (size_t i = 0; i < PROTOCORE_DELIVERY_PRECACHE_MAX; i++)
    {
        paths[i] = NAMES[i];
    }
    HttpDelivery.sw_manifest_args.paths = paths;
    HttpDelivery.sw_manifest_args.n = PROTOCORE_DELIVERY_PRECACHE_MAX;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    size_t n = HttpDelivery.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
}

// A truncated document is not a JSON text (RFC 8259 sec 2: a text is one complete value), so a
// buffer too small to hold the whole manifest must be refused with nothing written into it.
void test_manifest_refuses_rather_than_truncating(void)
{
    static const char *const PATHS[] = {"/aaaaaaaaaa", "/bbbbbbbbbb", "/cccccccccc"};
    char out[32];
    char big[PROTOCORE_DELIVERY_MANIFEST_BUF];
    char exact[PROTOCORE_DELIVERY_MANIFEST_BUF];

    memset(out, 'Z', sizeof(out));
    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 3;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);

    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 3;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = big;
    HttpDelivery.sw_manifest_args.cap = sizeof(big);
    HttpDelivery.sw_manifest(http_delivery_work);
    size_t need = HttpDelivery.n;
    TEST_ASSERT_TRUE(need > 0);
    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 3;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = exact;
    HttpDelivery.sw_manifest_args.cap = need + 1u;
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(need, HttpDelivery.n);
    TEST_ASSERT_EQUAL_STRING(big, exact);
    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 3;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = exact;
    HttpDelivery.sw_manifest_args.cap = need;
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);
}

// Nothing to write into, or paths claimed but not supplied, is a refusal rather than a partial
// document. A null version is not a refusal: the member is required by the format, so it is emitted
// with the empty string RFC 8259 sec 7 allows.
void test_manifest_refuses_bad_arguments(void)
{
    static const char *const PATHS[] = {"/a"};
    char out[64];

    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 1;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = NULL;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);
    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 1;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = 0;
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);
    HttpDelivery.sw_manifest_args.paths = NULL;
    HttpDelivery.sw_manifest_args.n = 1;
    HttpDelivery.sw_manifest_args.version = "v1";
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDelivery.n);

    HttpDelivery.sw_manifest_args.paths = PATHS;
    HttpDelivery.sw_manifest_args.n = 1;
    HttpDelivery.sw_manifest_args.version = NULL;
    HttpDelivery.sw_manifest_args.out = out;
    HttpDelivery.sw_manifest_args.cap = sizeof(out);
    HttpDelivery.sw_manifest(http_delivery_work);
    TEST_ASSERT_TRUE(HttpDelivery.n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"\",\"precache\":[\"/a\"]}", out);
}
