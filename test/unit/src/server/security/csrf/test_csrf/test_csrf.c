// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the stateless HMAC-signed CSRF token (server/security/csrf/csrf.h).
//
// No standard defines this token, so the anchor is the primitive underneath it: RFC 2104 /
// FIPS 198-1 HMAC-SHA-256. test_token_is_the_documented_hmac_over_the_nonce is the load-bearing
// case - the nonce is a per-issue counter, so the first token after a reset has a fully determined
// nonce half, and its signature half is rebuilt here from HMAC-SHA-256(secret, nonce) truncated to
// CSRF_SIG_BYTES and hex-encoded, using the MAC primitive directly. That pins the construction
// rather than the round trip: a token that verified against itself would still pass every other
// case here if the signature covered the wrong bytes, or no bytes at all.

#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "server/security/csrf/csrf.h"
#include <string.h>

#include <unity.h>

static uint8_t tw[4096]; // the borrow every namespace call in this suite runs out of

// RFC 4231's first test-case key, used here only as a fixed secret with no structure of its own.
static const uint8_t SECRET[20] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                   0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};

// HMAC-SHA-256 borrows its working set from the caller; the test owns one, aligned for uint32_t.
static uint32_t g_work[(PROTOCORE_HMAC_SHA256_BORROW + 4) / 4];

void setUp(void)
{
    Csrf.reset(protocore_csrf_span());
    CsrfV.secret_args.secret = SECRET;
    CsrfV.secret_args.len = sizeof(SECRET);
    Csrf.set_secret(protocore_csrf_span());
}
void tearDown(void)
{
}

static const char HEX[] = "0123456789abcdef";

// Lowercase hex of @p n bytes, plus a NUL.
static void hex_of(const uint8_t *in, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i] = HEX[(in[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = HEX[in[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
}

// The nonce is a counter, little-endian over CSRF_NONCE_BYTES octets: the first token issued after a
// reset carries counter 1, so its nonce is 01 00 00 00 00 00 and its hex is "010000000000". The
// signature is the first CSRF_SIG_BYTES octets of HMAC-SHA-256(secret, nonce), lowercase hex.
void test_token_is_the_documented_hmac_over_the_nonce(void)
{
    char token[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = token;
    CsrfV.issue_args.cap = sizeof(token);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);

    uint8_t nonce[CSRF_NONCE_BYTES] = {1, 0, 0, 0, 0, 0};
    char want_nonce[CSRF_NONCE_BYTES * 2 + 1];
    hex_of(nonce, sizeof(nonce), want_nonce);
    TEST_ASSERT_EQUAL_STRING_LEN(want_nonce, token, CSRF_NONCE_BYTES * 2);
    TEST_ASSERT_EQUAL_CHAR('.', token[CSRF_NONCE_BYTES * 2]);

    uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
    HmacSha256V.mac_args.key = SECRET;
    HmacSha256V.mac_args.key_len = sizeof(SECRET);
    HmacSha256V.mac_args.data = nonce;
    HmacSha256V.mac_args.len = sizeof(nonce);
    HmacSha256V.mac_args.out = mac;
    HmacSha256.mac((uint8_t *)g_work);
    char want_sig[CSRF_SIG_BYTES * 2 + 1];
    hex_of(mac, CSRF_SIG_BYTES, want_sig);
    TEST_ASSERT_EQUAL_STRING(want_sig, &token[CSRF_NONCE_BYTES * 2 + 1]);

    CsrfV.verify_args.token = token;
    Csrf.verify(protocore_csrf_span());
    // and the same token, hand-assembled from the two halves, verifies
    TEST_ASSERT_TRUE(CsrfV.valid);
}

// The counter advances once per issue, so the second token names nonce 2 and the third nonce 3.
void test_the_nonce_counter_advances_by_one(void)
{
    char t[CSRF_TOKEN_BUF];
    for (unsigned c = 1; c <= 3u; c++)
    {
        uint8_t nonce[CSRF_NONCE_BYTES] = {(uint8_t)c, 0, 0, 0, 0, 0};
        char want[CSRF_NONCE_BYTES * 2 + 1];
        hex_of(nonce, sizeof(nonce), want);
        CsrfV.issue_args.out = t;
        CsrfV.issue_args.cap = sizeof(t);
        Csrf.issue(protocore_csrf_span());
        TEST_ASSERT_TRUE(CsrfV.n > 0);
        TEST_ASSERT_EQUAL_STRING_LEN(want, t, CSRF_NONCE_BYTES * 2);
    }
}

// The token is `<nonce_hex>.<sig_hex>` and nothing else, so its length is fixed by the two field
// widths plus the separator.
void test_the_token_shape_is_fixed(void)
{
    char t[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = t;
    CsrfV.issue_args.cap = sizeof(t);
    Csrf.issue(protocore_csrf_span());
    int n = CsrfV.n;
    TEST_ASSERT_EQUAL_INT(CSRF_NONCE_BYTES * 2 + 1 + CSRF_SIG_BYTES * 2, n);
    TEST_ASSERT_EQUAL_INT(n, (int)strlen(t));
    TEST_ASSERT_TRUE(n < CSRF_TOKEN_BUF);

    const char *dot = strchr(t, '.');
    TEST_ASSERT_NOT_NULL(dot);
    TEST_ASSERT_EQUAL_UINT((unsigned)(CSRF_NONCE_BYTES * 2), (unsigned)(dot - t));
    TEST_ASSERT_NULL(strchr(dot + 1, '.')); // exactly one separator

    // both halves are lowercase hex
    for (int i = 0; i < n; i++)
    {
        if (i == CSRF_NONCE_BYTES * 2)
        {
            continue;
        }
        TEST_ASSERT_TRUE((t[i] >= '0' && t[i] <= '9') || (t[i] >= 'a' && t[i] <= 'f'));
    }
}

// A freshly issued token verifies, and each successive one does too.
void test_issued_tokens_verify(void)
{
    for (int i = 0; i < 8; i++)
    {
        char t[CSRF_TOKEN_BUF];
        CsrfV.issue_args.out = t;
        CsrfV.issue_args.cap = sizeof(t);
        Csrf.issue(protocore_csrf_span());
        TEST_ASSERT_TRUE(CsrfV.n > 0);
        CsrfV.verify_args.token = t;
        Csrf.verify(protocore_csrf_span());
        TEST_ASSERT_TRUE(CsrfV.valid);
    }
}

// Successive tokens are distinct, because the nonce they are signed over is.
void test_successive_tokens_differ(void)
{
    char a[CSRF_TOKEN_BUF], b[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = a;
    CsrfV.issue_args.cap = sizeof(a);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    CsrfV.issue_args.out = b;
    CsrfV.issue_args.cap = sizeof(b);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    CsrfV.verify_args.token = a;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.valid);
    CsrfV.verify_args.token = b;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.valid);
}

// Every single character of the signature is covered: flipping any one of them must fail. A
// signature only some of whose characters are compared is the classic truncated-compare bug.
void test_every_signature_character_is_checked(void)
{
    char t[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = t;
    CsrfV.issue_args.cap = sizeof(t);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);

    for (int i = 0; i < CSRF_SIG_BYTES * 2; i++)
    {
        char tampered[CSRF_TOKEN_BUF];
        memcpy(tampered, t, sizeof(tampered));
        char *c = &tampered[CSRF_NONCE_BYTES * 2 + 1 + i];
        *c = (*c == 'a') ? 'b' : 'a';
        CsrfV.verify_args.token = tampered;
        Csrf.verify(protocore_csrf_span());
        TEST_ASSERT_FALSE_MESSAGE(CsrfV.valid, tampered);
    }
    CsrfV.verify_args.token = t;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.valid); // the untouched token still verifies
}

// Every character of the nonce is covered too: the signature is over the nonce, so changing it
// changes what the signature must be.
void test_every_nonce_character_is_checked(void)
{
    char t[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = t;
    CsrfV.issue_args.cap = sizeof(t);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);

    for (int i = 0; i < CSRF_NONCE_BYTES * 2; i++)
    {
        char tampered[CSRF_TOKEN_BUF];
        memcpy(tampered, t, sizeof(tampered));
        tampered[i] = (tampered[i] == 'a') ? 'b' : 'a';
        CsrfV.verify_args.token = tampered;
        Csrf.verify(protocore_csrf_span());
        TEST_ASSERT_FALSE_MESSAGE(CsrfV.valid, tampered);
    }
}

// A token is bound to the secret it was signed under: rotating the secret invalidates every token
// already issued, which is what makes the secret a revocation handle.
void test_a_token_is_bound_to_its_secret(void)
{
    char t[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = t;
    CsrfV.issue_args.cap = sizeof(t);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    CsrfV.verify_args.token = t;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.valid);

    uint8_t other[32];
    memset(other, 0xAB, sizeof(other));
    CsrfV.secret_args.secret = other;
    CsrfV.secret_args.len = sizeof(other);
    Csrf.set_secret(protocore_csrf_span());
    CsrfV.verify_args.token = t;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_FALSE(CsrfV.valid);

    Csrf.reset(protocore_csrf_span());
    // the same nonce under the new secret is a different, valid token
    CsrfV.secret_args.secret = other;
    CsrfV.secret_args.len = sizeof(other);
    Csrf.set_secret(protocore_csrf_span());
    char u[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = u;
    CsrfV.issue_args.cap = sizeof(u);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    TEST_ASSERT_EQUAL_STRING_LEN(t, u, CSRF_NONCE_BYTES * 2); // same nonce
    CsrfV.verify_args.token = u;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(strcmp(t, u) != 0); // different signature
    TEST_ASSERT_TRUE(CsrfV.valid);
}

// RFC 2104 sec 3: a key longer than the block is pre-hashed rather than truncated. This module caps
// what it stores at 32 octets, so a longer secret is the first 32 of it and nothing beyond that
// changes a token.
void test_the_secret_is_capped_at_thirty_two_octets(void)
{
    uint8_t long_key[64];
    for (size_t i = 0; i < sizeof(long_key); i++)
    {
        long_key[i] = (uint8_t)i;
    }

    Csrf.reset(protocore_csrf_span());
    CsrfV.secret_args.secret = long_key;
    CsrfV.secret_args.len = sizeof(long_key);
    Csrf.set_secret(protocore_csrf_span());
    char a[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = a;
    CsrfV.issue_args.cap = sizeof(a);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);

    Csrf.reset(protocore_csrf_span());
    long_key[32] ^= 0xFFu; // an octet past the cap
    CsrfV.secret_args.secret = long_key;
    CsrfV.secret_args.len = sizeof(long_key);
    Csrf.set_secret(protocore_csrf_span());
    char b[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = b;
    CsrfV.issue_args.cap = sizeof(b);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    TEST_ASSERT_EQUAL_STRING(a, b);

    // an octet inside the cap does change it
    long_key[31] ^= 0xFFu;
    Csrf.reset(protocore_csrf_span());
    CsrfV.secret_args.secret = long_key;
    CsrfV.secret_args.len = sizeof(long_key);
    Csrf.set_secret(protocore_csrf_span());
    char c[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = c;
    CsrfV.issue_args.cap = sizeof(c);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    TEST_ASSERT_TRUE(strcmp(a, c) != 0);
}

// With no secret installed, issue and verify both fail closed rather than signing with zeros.
void test_no_secret_fails_closed(void)
{
    Csrf.reset(protocore_csrf_span());
    char t[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = t;
    CsrfV.issue_args.cap = sizeof(t);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_EQUAL_INT(0, CsrfV.n);
    CsrfV.verify_args.token = "010000000000.0102030405060708090a0b0c0d0e";
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_FALSE(CsrfV.valid);

    CsrfV.secret_args.secret = SECRET;
    CsrfV.secret_args.len = sizeof(SECRET);
    Csrf.set_secret(protocore_csrf_span());
    // a null secret pointer clears the length rather than leaving the previous secret installed
    CsrfV.secret_args.secret = NULL;
    CsrfV.secret_args.len = 5;
    Csrf.set_secret(protocore_csrf_span());
    CsrfV.issue_args.out = t;
    CsrfV.issue_args.cap = sizeof(t);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_EQUAL_INT(0, CsrfV.n);
}

// Reset restarts the counter as well as clearing the secret, so the first token after it is the
// counter-1 token again.
void test_reset_restarts_the_counter(void)
{
    char first[CSRF_TOKEN_BUF], again[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = first;
    CsrfV.issue_args.cap = sizeof(first);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    CsrfV.issue_args.out = again;
    CsrfV.issue_args.cap = sizeof(again);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    TEST_ASSERT_TRUE(strcmp(first, again) != 0);

    Csrf.reset(protocore_csrf_span());
    CsrfV.secret_args.secret = SECRET;
    CsrfV.secret_args.len = sizeof(SECRET);
    Csrf.set_secret(protocore_csrf_span());
    CsrfV.issue_args.out = again;
    CsrfV.issue_args.cap = sizeof(again);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    TEST_ASSERT_EQUAL_STRING(first, again);
}

// Anything that is not the documented shape is refused, without reading past what was handed in.
void test_malformed_tokens_are_refused(void)
{
    static const char *const BAD[] = {
        NULL,
        "",
        ".",
        "notatoken",
        "abcd.ef",
        "010000000000",                                // no separator, no signature
        "010000000000.",                               // empty signature
        "0100000000.0102030405060708090a0b0c0d0e",     // nonce field too short
        "010000000000ff.0102030405060708090a0b0c0d0e", // nonce field too long
        "zzzzzzzzzzzz.0102030405060708090a0b0c0d0e",   // right length, not hex
        "010000000000.0102030405",                     // signature too short
        "010000000000.0102030405060708090a0b0c0d0e00", // signature too long
        ".0102030405060708090a0b0c0d0e",               // empty nonce
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        CsrfV.verify_args.token = BAD[i];
        Csrf.verify(protocore_csrf_span());
        TEST_ASSERT_FALSE_MESSAGE(CsrfV.valid, BAD[i] ? BAD[i] : "(null)");
    }
}

// An odd-length nonce field cannot be hex octets, and is refused at the decode rather than being
// silently rounded to a shorter nonce.
void test_an_odd_length_nonce_is_refused(void)
{
    CsrfV.verify_args.token = "01000000000.0102030405060708090a0b0c0d0e";
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_FALSE(CsrfV.valid);
}

// A buffer that cannot hold the whole token yields 0 and writes no partial token: half a token is a
// token that fails to verify for the wrong reason.
void test_issue_refuses_a_short_buffer(void)
{
    char small[CSRF_TOKEN_BUF];
    for (size_t cap = 0; cap < CSRF_TOKEN_BUF; cap++)
    {
        memset(small, '#', sizeof(small));
        CsrfV.issue_args.out = small;
        CsrfV.issue_args.cap = cap;
        Csrf.issue(protocore_csrf_span());
        TEST_ASSERT_EQUAL_INT(0, CsrfV.n);
    }
    CsrfV.issue_args.out = small;
    CsrfV.issue_args.cap = CSRF_TOKEN_BUF;
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    CsrfV.issue_args.out = NULL;
    CsrfV.issue_args.cap = CSRF_TOKEN_BUF;
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_EQUAL_INT(0, CsrfV.n);
}

// The verify path holds nothing across calls: the same token verifies repeatedly, and a refused one
// in between does not poison the next.
void test_verify_holds_no_state(void)
{
    char t[CSRF_TOKEN_BUF];
    CsrfV.issue_args.out = t;
    CsrfV.issue_args.cap = sizeof(t);
    Csrf.issue(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.n > 0);
    CsrfV.verify_args.token = t;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.valid);
    CsrfV.verify_args.token = "010000000000.00000000000000000000000000ff";
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_FALSE(CsrfV.valid);
    CsrfV.verify_args.token = t;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.valid);
    CsrfV.verify_args.token = "garbage";
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_FALSE(CsrfV.valid);
    CsrfV.verify_args.token = t;
    Csrf.verify(protocore_csrf_span());
    TEST_ASSERT_TRUE(CsrfV.valid);
}
