// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HS256 JWT verifier (services/security/jwt/jwt.h).
//
// RFC 7515 Appendix A.1 prints a complete HS256 JWS: the JOSE Header, the JWS Payload, the symmetric
// key as a JWK, the MAC octets, and the finished Compact Serialization. test_rfc7515_a1_example is
// therefore the load-bearing case: it feeds the RFC's own token and the RFC's own key to the
// verifier, so nothing here depends on this HMAC being right about itself. The `alg` cases come from
// RFC 7515 sec 4.1.1 and RFC 8725 sec 3.1, the Bearer cases from RFC 6750 sec 2.1, the time claims
// from RFC 7519 sec 4.1.4 and 4.1.5, and the scope syntax from RFC 6749 sec 3.3.

#include "services/security/jwt/jwt.h"
#include <string.h>

#include <unity.h>

static uint8_t jwt_work[16]; // the borrow an entry takes; Jwt never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 7515 Appendix A.1: the Compact Serialization, printed there across three display lines.
//   header  {"typ":"JWT",CRLF " "alg":"HS256"}
//   payload {"iss":"joe",CRLF " "exp":1300819380,CRLF " "http://example.com/is_root":true}
static const char RFC7515_A1[] = "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9"
                                 ".eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFt"
                                 "cGxlLmNvbS9pc19yb290Ijp0cnVlfQ"
                                 ".dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";

// The same appendix's JWK: {"kty":"oct","k":"AyM1SysPpbyDfgZld3umj1qzKObwVMkoqQ-EstJQLr_T-1qS0gZH75
// aKtMN3Yj0iPS4hcgUuTwjAzZr1Z9CAow"}. These are that `k` value base64url-decoded, 64 octets.
static const uint8_t RFC7515_A1_KEY[64] = {0x03, 0x23, 0x35, 0x4B, 0x2B, 0x0F, 0xA5, 0xBC, 0x83, 0x7E, 0x06, 0x65, 0x77,
                                           0x7B, 0xA6, 0x8F, 0x5A, 0xB3, 0x28, 0xE6, 0xF0, 0x54, 0xC9, 0x28, 0xA9, 0x0F,
                                           0x84, 0xB2, 0xD2, 0x50, 0x2E, 0xBF, 0xD3, 0xFB, 0x5A, 0x92, 0xD2, 0x06, 0x47,
                                           0xEF, 0x96, 0x8A, 0xB4, 0xC3, 0x77, 0x62, 0x3D, 0x22, 0x3D, 0x2E, 0x21, 0x72,
                                           0x05, 0x2E, 0x4F, 0x08, 0xC0, 0xCD, 0x9A, 0xF5, 0x67, 0xD0, 0x80, 0xA3};

static proto_bool verify(const char *jws, const uint8_t *key, size_t key_len)
{
    Jwt.token.jws = jws;
    Jwt.token.jws_len = strlen(jws);
    Jwt.key.secret = key;
    Jwt.key.secret_len = key_len;
    Jwt.verify_mac(jwt_work);
    return Jwt.ok;
}

// The RFC's token under the RFC's key. Nothing else in this file proves the MAC.
void test_rfc7515_a1_example(void)
{
    TEST_ASSERT_TRUE(verify(RFC7515_A1, RFC7515_A1_KEY, sizeof(RFC7515_A1_KEY)));
}

// A different key, or a truncated one, computes a different MAC over the same signing input.
void test_wrong_key_is_refused(void)
{
    uint8_t bad[sizeof(RFC7515_A1_KEY)];
    memcpy(bad, RFC7515_A1_KEY, sizeof(bad));
    bad[0] = (uint8_t)(bad[0] ^ 0x01);
    TEST_ASSERT_FALSE(verify(RFC7515_A1, bad, sizeof(bad)));

    TEST_ASSERT_FALSE(verify(RFC7515_A1, RFC7515_A1_KEY, sizeof(RFC7515_A1_KEY) - 1));
    TEST_ASSERT_FALSE(verify(RFC7515_A1, NULL, sizeof(RFC7515_A1_KEY)));
}

// RFC 7515 sec 5.2: the signature is validated over the JWS Signing Input, so a change to any
// character of any of the three segments must fail.
void test_any_altered_character_is_refused(void)
{
    const size_t n = strlen(RFC7515_A1);
    for (size_t i = 0; i < n; i++)
    {
        if (RFC7515_A1[i] == '.')
        {
            continue; // moving a delimiter is a different serialization, tested separately
        }
        char bad[256];
        memcpy(bad, RFC7515_A1, n + 1);
        bad[i] = (bad[i] == 'A') ? 'B' : 'A';
        TEST_ASSERT_FALSE_MESSAGE(verify(bad, RFC7515_A1_KEY, sizeof(RFC7515_A1_KEY)), bad);
    }
}

// RFC 7515 sec 4.1.1 makes `alg` name the algorithm that secures the JWS, and RFC 8725 sec 3.1
// requires the verifier to perform exactly that one. Anything but HS256 is refused before the MAC
// runs, which is what settles the `none` substitution.
void test_alg_must_name_hs256(void)
{
    static const char *const HEADERS[] = {
        "eyJhbGciOiJub25lIn0",  // {"alg":"none"}
        "eyJhbGciOiJSUzI1NiJ9", // {"alg":"RS256"}, the header of RFC 7515 Appendix A.2
        "eyJhbGciOiJIUzM4NCJ9", // {"alg":"HS384"}
        "eyJ0eXAiOiJKV1QifQ",   // {"typ":"JWT"}, no alg at all
    };
    const char *rest = strchr(RFC7515_A1, '.');
    for (size_t i = 0; i < sizeof(HEADERS) / sizeof(HEADERS[0]); i++)
    {
        char token[256];
        strcpy(token, HEADERS[i]);
        strcat(token, rest);
        TEST_ASSERT_FALSE_MESSAGE(verify(token, RFC7515_A1_KEY, sizeof(RFC7515_A1_KEY)), HEADERS[i]);
    }
}

// RFC 7515 sec 7.1: exactly two periods, three segments. Anything else is not this serialization.
void test_malformed_serializations_are_refused(void)
{
    static const char *const BAD[] = {
        "",
        ".",
        "..",
        "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9",                    // one segment
        "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJqb2UifQ", // two segments
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(verify(BAD[i], RFC7515_A1_KEY, sizeof(RFC7515_A1_KEY)), BAD[i]);
    }

    // A fourth segment is the JWE serialization, not a JWS one.
    char four[256];
    strcpy(four, RFC7515_A1);
    strcat(four, ".AAAA");
    TEST_ASSERT_FALSE(verify(four, RFC7515_A1_KEY, sizeof(RFC7515_A1_KEY)));

    // A signature segment that is not the 43 characters an unpadded base64url 256-bit MAC takes.
    char short_sig[256];
    const size_t n = strlen(RFC7515_A1);
    memcpy(short_sig, RFC7515_A1, n);
    short_sig[n - 1] = '\0';
    TEST_ASSERT_FALSE(verify(short_sig, RFC7515_A1_KEY, sizeof(RFC7515_A1_KEY)));

    TEST_ASSERT_FALSE(verify(RFC7515_A1, RFC7515_A1_KEY, 0));
    Jwt.token.jws = NULL;
    Jwt.token.jws_len = 0;
    Jwt.verify_mac(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
}

// RFC 6750 sec 2.1: credentials = "Bearer" 1*SP b64token. RFC 7235 sec 2.1 makes the scheme name a
// case-insensitive token, so every spelling of it is the same scheme.
void test_bearer_credentials(void)
{
    static const char *const SCHEMES[] = {"Bearer ", "bearer ", "BEARER ", "BeArEr ", "Bearer    "};
    for (size_t i = 0; i < sizeof(SCHEMES) / sizeof(SCHEMES[0]); i++)
    {
        char field[256];
        strcpy(field, SCHEMES[i]);
        strcat(field, RFC7515_A1);
        Jwt.token.credentials = field;
        Jwt.key.secret = RFC7515_A1_KEY;
        Jwt.key.secret_len = sizeof(RFC7515_A1_KEY);
        Jwt.verify_bearer(jwt_work);
        TEST_ASSERT_TRUE_MESSAGE(Jwt.ok, SCHEMES[i]);
        // The token that was found is left on the handle.
        TEST_ASSERT_EQUAL_STRING(RFC7515_A1, Jwt.token.jws);
    }

    static const char *const NOT_BEARER[] = {"Basic czZCaGRSa3F0Mw==", "Bearer", "Token abc", "", "Bear "};
    for (size_t i = 0; i < sizeof(NOT_BEARER) / sizeof(NOT_BEARER[0]); i++)
    {
        Jwt.token.credentials = NOT_BEARER[i];
        Jwt.key.secret = RFC7515_A1_KEY;
        Jwt.key.secret_len = sizeof(RFC7515_A1_KEY);
        Jwt.verify_bearer(jwt_work);
        TEST_ASSERT_FALSE_MESSAGE(Jwt.ok, NOT_BEARER[i]);
    }

    Jwt.token.credentials = NULL;
    Jwt.verify_bearer(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
}

// RFC 7519 sec 4: the claims are the members of the JSON object the payload carries. Appendix A.1's
// payload is {"iss":"joe", "exp":1300819380, "http://example.com/is_root":true}.
void test_rfc7515_a1_claims(void)
{
    Jwt.token.jws = RFC7515_A1;
    Jwt.token.jws_len = strlen(RFC7515_A1);

    Jwt.claim.name = "exp";
    Jwt.claim_int(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    TEST_ASSERT_EQUAL_INT32(1300819380L, Jwt.num);

    char out[64];
    Jwt.claim.name = "iss";
    Jwt.claim.out = out;
    Jwt.claim.out_cap = sizeof(out);
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    TEST_ASSERT_EQUAL_STRING("joe", out);

    // An absent claim is reported, not invented.
    Jwt.claim.name = "sub";
    Jwt.claim_int(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
    Jwt.claim.name = "sub";
    Jwt.claim.out = out;
    Jwt.claim.out_cap = sizeof(out);
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
    TEST_ASSERT_EQUAL_STRING("", out);

    // A claim whose value is not a string does not read as one.
    Jwt.claim.name = "exp";
    Jwt.claim.out = out;
    Jwt.claim.out_cap = sizeof(out);
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
}

// A payload carrying every time claim, so the window can be walked without a MAC:
//   {"sub":"1234567890","exp":2000000000,"nbf":1000000000,"iat":1000000001,
//    "scope":"read write admin","role":"a\"b"}
static const char TIMED[] = "eyJhbGciOiJIUzI1NiJ9"
                            ".eyJzdWIiOiIxMjM0NTY3ODkwIiwiZXhwIjoyMDAwMDAwMDAwLCJuYmYiOjEwMDAwMDAwMDAs"
                            "ImlhdCI6MTAwMDAwMDAwMSwic2NvcGUiOiJyZWFkIHdyaXRlIGFkbWluIiwicm9sZSI6ImFc"
                            "ImIifQ"
                            ".AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

static proto_bool time_valid(long now, long leeway)
{
    Jwt.token.jws = TIMED;
    Jwt.token.jws_len = strlen(TIMED);
    Jwt.time.now = now;
    Jwt.time.leeway_s = leeway;
    Jwt.time_claims_valid(jwt_work);
    return Jwt.ok;
}

// RFC 7519 sec 4.1.4: the token MUST NOT be accepted on or after `exp`. Sec 4.1.5: nor before `nbf`.
// Both sections allow a small leeway for clock skew.
void test_time_claims_window(void)
{
    TEST_ASSERT_TRUE(time_valid(1500000000L, 0));  // inside [nbf, exp)
    TEST_ASSERT_TRUE(time_valid(1000000000L, 0));  // exactly nbf, which is not "before" it
    TEST_ASSERT_FALSE(time_valid(999999999L, 0));  // one second before nbf
    TEST_ASSERT_TRUE(time_valid(999999999L, 1));   // the same instant, one second of skew allowed
    TEST_ASSERT_TRUE(time_valid(1999999999L, 0));  // one second before exp
    TEST_ASSERT_FALSE(time_valid(2000000001L, 0)); // one second after exp
    TEST_ASSERT_TRUE(time_valid(2000000001L, 1));  // the same instant, one second of skew allowed

    // now <= 0 states there is no wall clock, so neither claim can be judged.
    TEST_ASSERT_TRUE(time_valid(0, 0));
    TEST_ASSERT_TRUE(time_valid(-1, 0));

    // A payload with no time claims is unconstrained by them: RFC 7515 A.1 carries no nbf, and its
    // exp of 1300819380 is in the past, so only exp can refuse it.
    Jwt.token.jws = RFC7515_A1;
    Jwt.token.jws_len = strlen(RFC7515_A1);
    Jwt.time.now = 1300819379L;
    Jwt.time.leeway_s = 0;
    Jwt.time_claims_valid(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    Jwt.time.now = 1300819381L;
    Jwt.time_claims_valid(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
}

// verify_mac_at is the MAC and then the window: a token that fails either is not accepted.
void test_verify_mac_at_needs_both(void)
{
    Jwt.token.jws = RFC7515_A1;
    Jwt.token.jws_len = strlen(RFC7515_A1);
    Jwt.key.secret = RFC7515_A1_KEY;
    Jwt.key.secret_len = sizeof(RFC7515_A1_KEY);
    Jwt.time.leeway_s = 0;

    Jwt.time.now = 1300819379L; // one second before exp
    Jwt.verify_mac_at(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);

    Jwt.time.now = 1300819381L; // one second after exp: the MAC still passes, the window does not
    Jwt.verify_mac_at(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);

    char field[256];
    strcpy(field, "Bearer ");
    strcat(field, RFC7515_A1);
    Jwt.token.credentials = field;
    Jwt.time.now = 1300819379L;
    Jwt.verify_bearer_at(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    Jwt.time.now = 1300819381L;
    Jwt.verify_bearer_at(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
}

// A backslash escape inside a string claim is dropped and the character after it taken literally,
// so the `role` claim "a\"b" reads back as the three characters a " b.
void test_claim_str_escapes_and_bounds(void)
{
    char out[64];
    Jwt.token.jws = TIMED;
    Jwt.token.jws_len = strlen(TIMED);
    Jwt.claim.name = "role";
    Jwt.claim.out = out;
    Jwt.claim.out_cap = sizeof(out);
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    TEST_ASSERT_EQUAL_STRING("a\"b", out);

    Jwt.claim.name = "sub";
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    TEST_ASSERT_EQUAL_STRING("1234567890", out);

    // A buffer too small for the value reports failure and leaves nothing partial behind.
    char small[4];
    Jwt.claim.name = "sub";
    Jwt.claim.out = small;
    Jwt.claim.out_cap = sizeof(small);
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
    TEST_ASSERT_EQUAL_STRING("", small);

    Jwt.claim.out = NULL;
    Jwt.claim.out_cap = sizeof(out);
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);

    // iat is read like any other integer claim (RFC 7519 sec 4.1.6).
    Jwt.claim.name = "iat";
    Jwt.claim_int(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    TEST_ASSERT_EQUAL_INT32(1000000001L, Jwt.num);
}

// RFC 6749 sec 3.3: scope is space-delimited, case-sensitive strings. A prefix of a token is not
// that token, which is what stops "read" from being satisfied by "readonly".
void test_scope_matches_whole_tokens(void)
{
    struct
    {
        const char *claim;
        const char *required;
        proto_bool want;
    } static const CASES[] = {
        {"read write admin", "read", PROTO_TRUE},
        {"read write admin", "write", PROTO_TRUE},
        {"read write admin", "admin", PROTO_TRUE},
        {"read write admin", "rea", PROTO_FALSE},  // a prefix of a token
        {"read write admin", "ead", PROTO_FALSE},  // a suffix of a token
        {"read write admin", "READ", PROTO_FALSE}, // case sensitive
        {"read write admin", "read write", PROTO_FALSE},
        {"readonly", "read", PROTO_FALSE},
        {"read", "read", PROTO_TRUE},
        {"  read   write  ", "write", PROTO_TRUE}, // runs of spaces are still delimiters
        {"", "read", PROTO_FALSE},
        {"read", "", PROTO_FALSE},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        Jwt.scope.claim = CASES[i].claim;
        Jwt.scope.required = CASES[i].required;
        Jwt.scope_allows(jwt_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CASES[i].want, Jwt.ok, CASES[i].required);
    }

    Jwt.scope.claim = NULL;
    Jwt.scope.required = "read";
    Jwt.scope_allows(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
    Jwt.scope.claim = "read";
    Jwt.scope.required = NULL;
    Jwt.scope_allows(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
}

// The scope claim read out of a token feeds the scope check: the two calls compose.
void test_scope_claim_then_check(void)
{
    char scope[64];
    Jwt.token.jws = TIMED;
    Jwt.token.jws_len = strlen(TIMED);
    Jwt.claim.name = "scope";
    Jwt.claim.out = scope;
    Jwt.claim.out_cap = sizeof(scope);
    Jwt.claim_str(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);
    TEST_ASSERT_EQUAL_STRING("read write admin", scope);

    Jwt.scope.claim = scope;
    Jwt.scope.required = "admin";
    Jwt.scope_allows(jwt_work);
    TEST_ASSERT_TRUE(Jwt.ok);

    Jwt.scope.required = "delete";
    Jwt.scope_allows(jwt_work);
    TEST_ASSERT_FALSE(Jwt.ok);
}
