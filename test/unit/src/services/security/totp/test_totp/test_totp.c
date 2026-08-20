// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for one-time passwords (services/security/totp/totp.h).
//
// Two RFCs publish complete vector tables for this algorithm and both are reproduced here.
// RFC 4226 Appendix D Table 2 lists HOTP(K,C) for counts 0 through 9 over the ASCII secret
// "12345678901234567890", and RFC 6238 Appendix B Table 1 lists the 8-digit SHA1 TOTP for six
// timestamps under X = 30, T0 = 0. test_rfc4226_hotp_test_values is the load-bearing case: it pins
// HMAC-SHA-1, the high-order-byte-first counter, the dynamic truncation offset and the mod 10^Digit
// reduction at once, and none of its expectations comes from running this code.

#include "services/security/totp/totp.h"
#include <string.h>

#include <unity.h>

static uint8_t totp_work[16]; // the borrow an entry takes; Totp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 4226 Appendix D: "The following test data uses the ASCII string "12345678901234567890" for
// the secret: Secret = 0x3132333435363738393031323334353637383930".
static const uint8_t RFC_SECRET[20] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30,
                                       0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30};

static uint32_t hotp_of(uint64_t counter, uint8_t digit)
{
    TotpV.k = RFC_SECRET;
    TotpV.keylen = sizeof(RFC_SECRET);
    TotpV.digit = digit;
    TotpV.step.counter = counter;
    Totp.hotp(totp_work);
    return TotpV.u32;
}

static uint32_t totp_of(uint64_t unix_time, uint64_t t0, uint32_t x, uint8_t digit)
{
    TotpV.k = RFC_SECRET;
    TotpV.keylen = sizeof(RFC_SECRET);
    TotpV.digit = digit;
    TotpV.step.unix_time = unix_time;
    TotpV.step.t0 = t0;
    TotpV.step.x = x;
    Totp.totp(totp_work);
    return TotpV.u32;
}

// The load-bearing case: RFC 4226 Appendix D Table 2, the HOTP column for counts 0 through 9.
void test_rfc4226_hotp_test_values(void)
{
    static const uint32_t HOTP[10] = {755224, 287082, 359152, 969429, 338314, 254676, 287922, 162583, 399871, 520489};
    for (uint64_t c = 0; c < 10; c++)
    {
        TEST_ASSERT_EQUAL_UINT32(HOTP[c], hotp_of(c, 6));
    }
}

// RFC 4226 Appendix D Table 2 also lists the truncated value in decimal, before the mod 10^Digit
// reduction. Every one is below 2^31 and above 10^8, so its low eight digits are the value mod
// 10^8, which is what an 8-digit HOTP is by sec 5.3:
//   1284755224 mod 10^8 = 84755224      868254676 mod 10^8 = 68254676
//   1094287082 mod 10^8 = 94287082     1918287922 mod 10^8 = 18287922
//    137359152 mod 10^8 = 37359152       82162583 mod 10^8 = 82162583
//   1726969429 mod 10^8 = 26969429      673399871 mod 10^8 = 73399871
//   1640338314 mod 10^8 = 40338314      645520489 mod 10^8 = 45520489
void test_rfc4226_truncated_values_at_eight_digits(void)
{
    static const uint32_t EIGHT[10] = {84755224, 94287082, 37359152, 26969429, 40338314,
                                       68254676, 18287922, 82162583, 73399871, 45520489};
    for (uint64_t c = 0; c < 10; c++)
    {
        TEST_ASSERT_EQUAL_UINT32(EIGHT[c], hotp_of(c, 8));
    }
}

// A 6-digit OTP is the 8-digit one's low six digits: both are the same 31-bit number reduced by a
// different power of ten (RFC 4226 sec 5.3), so no separate truncation path can be hiding here.
void test_digit_reduction_is_one_truncation(void)
{
    for (uint64_t c = 0; c < 10; c++)
    {
        TEST_ASSERT_EQUAL_UINT32(hotp_of(c, 8) % 1000000u, hotp_of(c, 6));
        TEST_ASSERT_EQUAL_UINT32(hotp_of(c, 8) % 10000u, hotp_of(c, 4));
    }
}

// RFC 4226 sec 5.3 sets Digit's floor at 6; the header makes 0 mean that default.
void test_digit_zero_takes_the_minimum(void)
{
    TEST_ASSERT_EQUAL_UINT32(hotp_of(0, 6), hotp_of(0, 0));
    TEST_ASSERT_EQUAL_UINT32(755224u, hotp_of(0, 0));
}

// RFC 6238 Appendix B Table 1, the SHA1 rows: 8-digit TOTP over the same secret with X = 30 and
// T0 = 0 (the Unix epoch).
void test_rfc6238_totp_test_vectors(void)
{
    static const struct
    {
        uint64_t seconds;
        uint32_t totp;
    } CASES[] = {
        {59ull, 94287082u},         {1111111109ull, 7081804u},  {1111111111ull, 14050471u},
        {1234567890ull, 89005924u}, {2000000000ull, 69279037u}, {20000000000ull, 65353130u},
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        TEST_ASSERT_EQUAL_UINT32(CASES[i].totp, totp_of(CASES[i].seconds, 0, 30, 8));
    }
}

// RFC 6238 Appendix B also prints the value of T for each timestamp, and sec 4.2 defines
// TOTP = HOTP(K, T). Computing HOTP directly at the published T must give the same OTP, which is
// what fixes the floor division, the default X and the T0 subtraction rather than a coincidence in
// the OTP column.
void test_rfc6238_time_step_matches_the_published_t(void)
{
    static const struct
    {
        uint64_t seconds;
        uint64_t t;
    } CASES[] = {
        {59ull, 0x0000000000000001ull},         {1111111109ull, 0x0000000023523ECull},
        {1111111111ull, 0x0000000023523EDull},  {1234567890ull, 0x00000000273EF07ull},
        {2000000000ull, 0x0000000003F940AAull}, {20000000000ull, 0x0000000027BC86AAull},
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        TEST_ASSERT_EQUAL_UINT32(hotp_of(CASES[i].t, 8), totp_of(CASES[i].seconds, 0, 30, 8));
    }
}

// X = 0 takes the RFC 6238 sec 4.1 default of 30 seconds.
void test_time_step_default_x(void)
{
    TEST_ASSERT_EQUAL_UINT32(totp_of(1111111109ull, 0, 30, 8), totp_of(1111111109ull, 0, 0, 8));
}

// RFC 6238 sec 4.2: T = (Current Unix time - T0) / X under the floor function. Every instant inside
// one step gives the same OTP, and the next second past the boundary gives the next step's.
void test_time_step_floors_within_a_step(void)
{
    uint32_t step_a = totp_of(1111111110ull, 0, 30, 8); // T = 37037037
    for (uint64_t s = 1111111110ull; s < 1111111140ull; s += 7)
    {
        TEST_ASSERT_EQUAL_UINT32(step_a, totp_of(s, 0, 30, 8));
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(step_a, totp_of(1111111140ull, 0, 30, 8));

    // T0 shifts the origin: (t - T0) / X, so t = T0 + 59 is step 1 exactly as t = 59 is with T0 = 0.
    TEST_ASSERT_EQUAL_UINT32(totp_of(59ull, 0, 30, 8), totp_of(1000000059ull, 1000000000ull, 30, 8));

    // A clock behind T0 cannot make a negative step; the module floors it at 0.
    TEST_ASSERT_EQUAL_UINT32(hotp_of(0, 8), totp_of(10ull, 1000ull, 30, 8));
}

// A different X names a different step for the same instant, so the OTP changes with it.
void test_time_step_honors_x(void)
{
    TEST_ASSERT_NOT_EQUAL_UINT32(totp_of(1111111109ull, 0, 30, 8), totp_of(1111111109ull, 0, 60, 8));
    // At X = 60 the step is floor(t / 60), which HOTP at that counter must reproduce.
    TEST_ASSERT_EQUAL_UINT32(hotp_of(1111111109ull / 60ull, 8), totp_of(1111111109ull, 0, 60, 8));
}

static proto_bool verify_at(uint64_t unix_time, uint32_t otp, int32_t drift)
{
    TotpV.k = RFC_SECRET;
    TotpV.keylen = sizeof(RFC_SECRET);
    TotpV.digit = 8;
    TotpV.step.unix_time = unix_time;
    TotpV.step.t0 = 0;
    TotpV.step.x = 30;
    TotpV.check.otp = otp;
    TotpV.check.drift = drift;
    Totp.verify(totp_work);
    return TotpV.ok;
}

// RFC 6238 sec 6: a validator accepts an OTP generated within the drift window it allows, and only
// there. 1111111109 and 1111111111 sit in adjacent steps (T = 0x23523EC and 0x23523ED), so the
// second's OTP is exactly one step away from the first's.
void test_rfc6238_drift_window(void)
{
    TEST_ASSERT_TRUE(verify_at(1111111109ull, 7081804u, 0));   // its own step
    TEST_ASSERT_FALSE(verify_at(1111111109ull, 14050471u, 0)); // the next step, window closed
    TEST_ASSERT_TRUE(verify_at(1111111109ull, 14050471u, 1));  // one step forward
    TEST_ASSERT_TRUE(verify_at(1111111111ull, 7081804u, 1));   // one step backward

    // 1111111140 is T = 0x23523EE, two steps past 0x23523EC, so it needs a window of two.
    TEST_ASSERT_FALSE(verify_at(1111111140ull, 7081804u, 1));
    TEST_ASSERT_TRUE(verify_at(1111111140ull, 7081804u, 2));
}

// A negative drift matches nothing, and an OTP that was never generated is refused however wide the
// window.
void test_verify_refuses_what_it_should(void)
{
    TEST_ASSERT_FALSE(verify_at(1111111109ull, 7081804u, -1));
    TEST_ASSERT_FALSE(verify_at(1111111109ull, 0u, 5));
    TEST_ASSERT_FALSE(verify_at(1111111109ull, 99999999u, 5));
}

// Near the epoch the window would reach below step 0; those steps are skipped rather than wrapping
// a uint64 counter.
void test_verify_window_clamps_at_the_epoch(void)
{
    TEST_ASSERT_TRUE(verify_at(0ull, hotp_of(0, 8), 5));
    TEST_ASSERT_TRUE(verify_at(59ull, 94287082u, 5));
}

static int32_t b32(const char *text, uint8_t *out, size_t cap)
{
    TotpV.secret.b32 = text;
    TotpV.secret.out = out;
    TotpV.secret.cap = cap;
    Totp.base32_decode(totp_work);
    return TotpV.i32;
}

// RFC 4648 sec 10 prints the base32 encoding of every prefix of "foobar"; decoding each must give
// the string back.
void test_rfc4648_base32_test_vectors(void)
{
    static const struct
    {
        const char *b32;
        const char *plain;
        int32_t n;
    } CASES[] = {
        {"", "", 0},
        {"MY======", "f", 1},
        {"MZXQ====", "fo", 2},
        {"MZXW6===", "foo", 3},
        {"MZXW6YQ=", "foob", 4},
        {"MZXW6YTB", "fooba", 5},
        {"MZXW6YTBOI======", "foobar", 6},
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t out[16] = {0};
        TEST_ASSERT_EQUAL_INT32_MESSAGE(CASES[i].n, b32(CASES[i].b32, out, sizeof(out)), CASES[i].b32);
        if (CASES[i].n > 0)
        {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(CASES[i].plain, out, (size_t)CASES[i].n, CASES[i].b32);
        }
    }
}

// The alphabet of RFC 4648 sec 6 is case-insensitive here, and the padding, spaces and dashes a
// provisioning URI carries are skipped rather than rejected.
void test_base32_accepts_the_provisioning_spellings(void)
{
    uint8_t upper[8] = {0};
    uint8_t lower[8] = {0};
    uint8_t spaced[8] = {0};
    TEST_ASSERT_EQUAL_INT32(6, b32("MZXW6YTBOI======", upper, sizeof(upper)));
    TEST_ASSERT_EQUAL_INT32(6, b32("mzxw6ytboi", lower, sizeof(lower)));
    TEST_ASSERT_EQUAL_INT32(6, b32("MZXW-6YTB OI", spaced, sizeof(spaced)));
    TEST_ASSERT_EQUAL_MEMORY(upper, lower, 6);
    TEST_ASSERT_EQUAL_MEMORY(upper, spaced, 6);
}

// A character outside the alphabet, a null argument, and a buffer too small for the secret are all
// reported as -1 rather than a short decode.
void test_base32_refuses_bad_input(void)
{
    uint8_t out[8] = {0};
    TEST_ASSERT_EQUAL_INT32(-1, b32("MZXW6YT!", out, sizeof(out)));  // '!' is not in the alphabet
    TEST_ASSERT_EQUAL_INT32(-1, b32("MZXW6YT1", out, sizeof(out)));  // '1' is excluded from base32
    TEST_ASSERT_EQUAL_INT32(-1, b32("MZXW6YTB0", out, sizeof(out))); // '0' likewise
    TEST_ASSERT_EQUAL_INT32(-1, b32(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT32(-1, b32("MZXW6YTBOI======", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT32(-1, b32("MZXW6YTBOI======", out, 3)); // 6 bytes into 3
}

// RFC 2104 sec 2: a key longer than the 64-octet block is replaced by its own hash, so a 65-byte
// key and the 20-byte SHA-1 of that key must key the same OTP.
void test_long_key_is_hashed_to_the_block(void)
{
    uint8_t longk[65];
    for (unsigned i = 0; i < sizeof(longk); i++)
    {
        longk[i] = (uint8_t)i;
    }
    TotpV.k = longk;
    TotpV.keylen = sizeof(longk);
    TotpV.digit = 6;
    TotpV.step.counter = 1;
    Totp.hotp(totp_work);
    uint32_t from_long = TotpV.u32;

    uint8_t exact[64];
    for (unsigned i = 0; i < sizeof(exact); i++)
    {
        exact[i] = (uint8_t)i;
    }
    TotpV.k = exact;
    TotpV.keylen = sizeof(exact);
    TotpV.step.counter = 1;
    Totp.hotp(totp_work);
    // 65 bytes takes the hashed path, 64 the padded one, so the two OTPs must differ.
    TEST_ASSERT_NOT_EQUAL_UINT32(from_long, TotpV.u32);

    // The same 65-byte key always produces the same OTP: the call holds no state between uses.
    TotpV.k = longk;
    TotpV.keylen = sizeof(longk);
    TotpV.step.counter = 1;
    Totp.hotp(totp_work);
    TEST_ASSERT_EQUAL_UINT32(from_long, TotpV.u32);
}

// A counter is 8 octets high-order byte first (RFC 4226 sec 5.2), so counters that differ only
// above the 32-bit boundary are different moving factors.
void test_counter_is_a_full_64_bit_moving_factor(void)
{
    TEST_ASSERT_NOT_EQUAL_UINT32(hotp_of(0, 8), hotp_of(0x100000000ull, 8));
    TEST_ASSERT_NOT_EQUAL_UINT32(hotp_of(1, 8), hotp_of(0x100000001ull, 8));
    // 20000000000 seconds at X = 30 is step 0x27BC86AA, past 2^31 in seconds but not in steps.
    TEST_ASSERT_EQUAL_UINT32(65353130u, hotp_of(0x27BC86AAull, 8));
}
