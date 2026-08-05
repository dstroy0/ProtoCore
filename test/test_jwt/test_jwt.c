// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the JWT HS256 verifier. The reference token below was produced
// with Python (hmac+hashlib) for secret "s3cr3t-key" and payload
// {"sub":"alice","role":"admin","exp":2000000000,"iat":1700000000}.

#include "crypto/mac/hmac_sha256.h"
#include "network_drivers/presentation/codec/base64/base64.h"
#include "services/security/jwt/jwt.h"
#include <stdio.h>

#include <unity.h>

static const char *SECRET = "s3cr3t-key";
static const char *TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                           "eyJzdWIiOiJhbGljZSIsInJvbGUiOiJhZG1pbiIsImV4cCI6MjAwMDAwMDAwMCwiaWF0IjoxNzAwMDAwMDAwfQ."
                           "oaEaMu7USfUlYDaLYQlogmRd_1ZPBr7cKrPIo5lXdxc";

static const uint8_t *sec()
{
    return (const uint8_t *)SECRET;
}
static size_t seclen()
{
    return strlen(SECRET);
}

void setUp()
{
}
void tearDown()
{
}

void test_valid_token_accepts()
{
    TEST_ASSERT_TRUE(pc_jwt_verify_hs256(TOKEN, strlen(TOKEN), sec(), seclen()));
}

void test_wrong_secret_rejects()
{
    const char *bad = "wrong-key";
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(TOKEN, strlen(TOKEN), (const uint8_t *)bad, strlen(bad)));
}

void test_tampered_payload_rejects()
{
    char t[400];
    strcpy(t, TOKEN);
    // Flip a character in the payload segment (after the first dot).
    char *dot = strchr(t, '.');
    dot[3] = (dot[3] == 'A') ? 'B' : 'A';
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
}

void test_tampered_signature_rejects()
{
    char t[400];
    strcpy(t, TOKEN);
    size_t n = strlen(t);
    t[n - 1] = (t[n - 1] == 'c') ? 'd' : 'c'; // last sig char
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
}

void test_malformed_rejected()
{
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256("not-a-jwt", 9, sec(), seclen()));
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256("only.one-dot", 12, sec(), seclen()));
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256("", 0, sec(), seclen()));
}

// RFC 7515 5.2: the verifier must check the header "alg". A token whose header
// declares a non-HS256 algorithm must be rejected even when its signature is a VALID
// HMAC-SHA256 over header.payload (algorithm-substitution defense). Built here with
// the library's own HMAC + base64url so it is self-contained.
void test_alg_not_hs256_rejected()
{
    const char *hdr_json = "{\"alg\":\"none\",\"typ\":\"JWT\"}";
    const char *pl_json = "{\"sub\":\"alice\"}";
    char hdr[64], payload[64];
    pc_base64url_encode((const uint8_t *)hdr_json, strlen(hdr_json), hdr);
    pc_base64url_encode((const uint8_t *)pl_json, strlen(pl_json), payload);

    char signing[160];
    int sl = snprintf(signing, sizeof(signing), "%s.%s", hdr, payload);
    uint8_t mac[PC_HMAC_SHA256_LEN];
    pc_hmac_sha256(sec(), seclen(), (const uint8_t *)signing, (size_t)sl, mac);
    char sig[48];
    pc_base64url_encode(mac, sizeof(mac), sig);

    char token[256];
    snprintf(token, sizeof(token), "%s.%s.%s", hdr, payload, sig);
    // The HMAC is valid for these bytes, but alg is "none" -> must be rejected.
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(token, strlen(token), sec(), seclen()));

    // Sanity: the same construction with alg "HS256" verifies (proves the test rig).
    const char *ok_hdr_json = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    char ok_hdr[64];
    pc_base64url_encode((const uint8_t *)ok_hdr_json, strlen(ok_hdr_json), ok_hdr);
    sl = snprintf(signing, sizeof(signing), "%s.%s", ok_hdr, payload);
    pc_hmac_sha256(sec(), seclen(), (const uint8_t *)signing, (size_t)sl, mac);
    pc_base64url_encode(mac, sizeof(mac), sig);
    snprintf(token, sizeof(token), "%s.%s.%s", ok_hdr, payload, sig);
    TEST_ASSERT_TRUE(pc_jwt_verify_hs256(token, strlen(token), sec(), seclen()));
}

void test_bearer_header()
{
    char hdr[400];
    snprintf(hdr, sizeof(hdr), "Bearer %s", TOKEN);
    TEST_ASSERT_TRUE(pc_jwt_bearer_valid(hdr, sec(), seclen()));
    // case-insensitive scheme
    snprintf(hdr, sizeof(hdr), "bearer %s", TOKEN);
    TEST_ASSERT_TRUE(pc_jwt_bearer_valid(hdr, sec(), seclen()));
    // missing scheme
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid(TOKEN, sec(), seclen()));
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid(NULL, sec(), seclen()));
}

void test_claim_int()
{
    long v = 0;
    TEST_ASSERT_TRUE(pc_jwt_claim_int(TOKEN, strlen(TOKEN), "exp", &v));
    TEST_ASSERT_EQUAL_INT32(2000000000, v);
    TEST_ASSERT_TRUE(pc_jwt_claim_int(TOKEN, strlen(TOKEN), "iat", &v));
    TEST_ASSERT_EQUAL_INT32(1700000000, v);
}

void test_claim_missing()
{
    long v = 0;
    TEST_ASSERT_FALSE(pc_jwt_claim_int(TOKEN, strlen(TOKEN), "nbf", &v));
}

void test_claim_str()
{
    char buf[32];
    TEST_ASSERT_TRUE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), "sub", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("alice", buf);
    TEST_ASSERT_TRUE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), "role", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("admin", buf);
    TEST_ASSERT_FALSE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), "nope", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), "exp", buf, sizeof(buf))); // numeric, not a string
}

void test_scope_allows()
{
    TEST_ASSERT_TRUE(pc_jwt_scope_allows("read write admin", "read"));
    TEST_ASSERT_TRUE(pc_jwt_scope_allows("read write admin", "write"));
    TEST_ASSERT_TRUE(pc_jwt_scope_allows("read write admin", "admin"));
    TEST_ASSERT_TRUE(pc_jwt_scope_allows("admin", "admin")); // single scope
    TEST_ASSERT_FALSE(pc_jwt_scope_allows("read write admin", "delete"));
    TEST_ASSERT_FALSE(pc_jwt_scope_allows("read write admin", "adm")); // whole-token match only
    TEST_ASSERT_FALSE(pc_jwt_scope_allows("read write admin", ""));
    TEST_ASSERT_FALSE(pc_jwt_scope_allows(NULL, "read"));
}

// RFC 4648 section 5 / RFC 7515: base64url decoding must use the '-'/'_' alphabet
// only. The standard '+'/'/' characters are rejected (return 0), while the URL
// forms round-trip exactly. 0xFB,0xFF encodes to "-_8" in base64url ("+/8" in std).
void test_base64url_strict_alphabet()
{
    uint8_t out[8];
    // URL-safe characters decode.
    TEST_ASSERT_EQUAL_size_t(2, pc_base64url_decode("-_8", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xFB, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
    // Standard-alphabet '+' and '/' are not valid base64url -> reject.
    TEST_ASSERT_EQUAL_size_t(0, pc_base64url_decode("+/8", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_base64url_decode("ab+c", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_base64url_decode("ab/c", 4, out, sizeof(out)));
    // Encode produces only the URL alphabet (no '+', '/', or '=').
    const uint8_t raw[] = {0xFB, 0xFF, 0xBF};
    char enc[8];
    size_t n = pc_base64url_encode(raw, sizeof(raw), enc);
    TEST_ASSERT_NULL(strpbrk(enc, "+/="));
    uint8_t back[8];
    TEST_ASSERT_EQUAL_size_t(sizeof(raw), pc_base64url_decode(enc, n, back, sizeof(back)));
    TEST_ASSERT_EQUAL_MEMORY(raw, back, sizeof(raw));
}

// Build "b64url(hdr).b64url(payload).sig" (signature validity irrelevant to the
// claim parsers; the verifier tests below use it for alg/shape checks).
static void mk_token(char *out, size_t cap, const char *hdr_json, const char *pl_json, const char *sig)
{
    char h[128], p[256];
    pc_base64url_encode((const uint8_t *)hdr_json, strlen(hdr_json), h);
    pc_base64url_encode((const uint8_t *)pl_json, strlen(pl_json), p);
    snprintf(out, cap, "%s.%s.%s", h, p, sig);
}

void test_verify_malformed_headers()
{
    const char *sig43 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; // 43 chars
    // A third dot is not a valid JWT shape.
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256("aaaa.b.c.d", 10, sec(), seclen()));
    // Header segment is not valid base64url.
    char t[256];
    snprintf(t, sizeof(t), "@@@.x.%s", sig43);
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
    // Header decodes but has no "alg".
    mk_token(t, sizeof(t), "{\"typ\":\"JWT\"}", "{}", sig43);
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
    // "alg" value is not a quoted string.
    mk_token(t, sizeof(t), "{\"alg\":123}", "{}", sig43);
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
    // Valid HS256 header but the signature is not 43 base64url chars.
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{}", "abc");
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
}

void test_bearer_extra_spaces()
{
    char hdr[400];
    snprintf(hdr, sizeof(hdr), "Bearer    %s", TOKEN); // extra spaces after the scheme
    TEST_ASSERT_TRUE(pc_jwt_bearer_valid(hdr, sec(), seclen()));
}

void test_claim_int_edges()
{
    long v = 0;
    TEST_ASSERT_FALSE(pc_jwt_claim_int(NULL, 5, "x", &v));
    TEST_ASSERT_FALSE(pc_jwt_claim_int(TOKEN, strlen(TOKEN), NULL, &v));
    TEST_ASSERT_FALSE(pc_jwt_claim_int(TOKEN, strlen(TOKEN), "exp", NULL));
    TEST_ASSERT_FALSE(pc_jwt_claim_int("nodots", 6, "x", &v));  // no first dot
    TEST_ASSERT_FALSE(pc_jwt_claim_int("a.b", 3, "x", &v));     // no second dot
    TEST_ASSERT_FALSE(pc_jwt_claim_int("x.@@@.y", 7, "x", &v)); // payload not base64url
    char longname[60];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_jwt_claim_int(TOKEN, strlen(TOKEN), longname, &v)); // name too long for key buffer

    char t[256];
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"neg\":-42}", "sig");
    TEST_ASSERT_TRUE(pc_jwt_claim_int(t, strlen(t), "neg", &v));
    TEST_ASSERT_EQUAL_INT(-42, v);
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"x\":\"str\"}", "sig");
    TEST_ASSERT_FALSE(pc_jwt_claim_int(t, strlen(t), "x", &v)); // value is not a number
}

void test_claim_str_edges()
{
    char buf[32];
    TEST_ASSERT_FALSE(pc_jwt_claim_str(NULL, 5, "x", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), "sub", buf, 0)); // zero cap
    TEST_ASSERT_FALSE(pc_jwt_claim_str("nodots", 6, "x", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_jwt_claim_str("a.b", 3, "x", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_jwt_claim_str("x.@@@.y", 7, "x", buf, sizeof(buf)));
    char longname[60];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), longname, buf, sizeof(buf)));

    char t[256];
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"s\":\"a\\/b\"}", "sig");
    TEST_ASSERT_TRUE(pc_jwt_claim_str(t, strlen(t), "s", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("a/b", buf); // backslash unescape
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"s\":\"abc", "sig");
    TEST_ASSERT_FALSE(pc_jwt_claim_str(t, strlen(t), "s", buf, sizeof(buf))); // unterminated string
}

// Build a genuinely HS256-signed token over the given payload JSON (secret = SECRET),
// so the *_at verifiers see a valid signature and only the time claims decide.
static void mk_signed(char *out, size_t cap, const char *pl_json)
{
    const char *hdr_json = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    char signing[400], sig[48];
    uint8_t mac[PC_HMAC_SHA256_LEN];
    char h[128], p[256];
    pc_base64url_encode((const uint8_t *)hdr_json, strlen(hdr_json), h);
    pc_base64url_encode((const uint8_t *)pl_json, strlen(pl_json), p);
    int sl = snprintf(signing, sizeof(signing), "%s.%s", h, p);
    pc_hmac_sha256(sec(), seclen(), (const uint8_t *)signing, (size_t)sl, mac);
    pc_base64url_encode(mac, sizeof(mac), sig);
    snprintf(out, cap, "%s.%s", signing, sig);
}

// now <= 0 (no wall clock) means the time claims are not evaluated: even a long-
// expired token passes pc_jwt_time_valid, and the signed verify reduces to the sig check.
void test_time_no_clock_skips_claims()
{
    char t[400];
    mk_signed(t, sizeof(t), "{\"sub\":\"a\",\"exp\":1000}"); // exp far in the past
    TEST_ASSERT_TRUE(pc_jwt_time_valid(t, strlen(t), 0, 0));
    TEST_ASSERT_TRUE(pc_jwt_verify_hs256_at(t, strlen(t), sec(), seclen(), 0, 0));
}

// exp (RFC 7519 4.1.4): valid before, expired after (with a leeway grace).
void test_time_exp_enforced()
{
    char t[400];
    mk_signed(t, sizeof(t), "{\"exp\":1700000000}");
    TEST_ASSERT_TRUE(pc_jwt_time_valid(t, strlen(t), 1699999000, 0));  // before exp
    TEST_ASSERT_FALSE(pc_jwt_time_valid(t, strlen(t), 1700000100, 0)); // past exp
    TEST_ASSERT_TRUE(pc_jwt_time_valid(t, strlen(t), 1700000030, 60)); // within leeway of exp
    // The signed verifier enforces it too (valid signature, time decides).
    TEST_ASSERT_TRUE(pc_jwt_verify_hs256_at(t, strlen(t), sec(), seclen(), 1699999000, 0));
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256_at(t, strlen(t), sec(), seclen(), 1700000100, 0));
}

// nbf (RFC 7519 4.1.5): not-yet-valid before, valid after (with a leeway grace).
void test_time_nbf_enforced()
{
    char t[400];
    mk_signed(t, sizeof(t), "{\"nbf\":1700000000}");
    TEST_ASSERT_FALSE(pc_jwt_time_valid(t, strlen(t), 1699999000, 0)); // before nbf
    TEST_ASSERT_TRUE(pc_jwt_time_valid(t, strlen(t), 1700000100, 0));  // after nbf
    TEST_ASSERT_TRUE(pc_jwt_time_valid(t, strlen(t), 1699999970, 60)); // within leeway before nbf
}

// A token with no exp/nbf has nothing to enforce -> valid whenever the clock is set.
void test_time_no_claims_valid()
{
    char t[400];
    mk_signed(t, sizeof(t), "{\"sub\":\"a\"}");
    TEST_ASSERT_TRUE(pc_jwt_time_valid(t, strlen(t), 1700000000, 0));
}

// pc_jwt_bearer_valid_at gates on both the signature and the time window.
void test_bearer_valid_at()
{
    char t[400], hdr[470];
    mk_signed(t, sizeof(t), "{\"exp\":1700000000}");
    snprintf(hdr, sizeof(hdr), "Bearer %s", t);
    TEST_ASSERT_TRUE(pc_jwt_bearer_valid_at(hdr, sec(), seclen(), 1699999000, 0));  // valid sig, in window
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid_at(hdr, sec(), seclen(), 1700000100, 0)); // valid sig, expired
    // A bad signature fails even inside the time window (signature is the primary gate).
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid_at(hdr, (const uint8_t *)"wrong", 5, 1699999000, 0));
}

void test_bearer_header_guards()
{
    uint8_t secret[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid(NULL, secret, sizeof(secret)));                  // null header
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid("Basic abc", secret, sizeof(secret)));           // not a Bearer
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid("Bearer    not.a.jwt", secret, sizeof(secret))); // leading spaces skipped
}

// Build a genuinely HS256-signed token with a caller-chosen header JSON, so the "alg" check is the
// only thing that can reject it.
static void mk_signed_hdr(char *out, size_t cap, const char *hdr_json, const char *pl_json)
{
    char h[128], p[256], signing[400], sig[48];
    uint8_t mac[PC_HMAC_SHA256_LEN];
    pc_base64url_encode((const uint8_t *)hdr_json, strlen(hdr_json), h);
    pc_base64url_encode((const uint8_t *)pl_json, strlen(pl_json), p);
    int sl = snprintf(signing, sizeof(signing), "%s.%s", h, p);
    pc_hmac_sha256(sec(), seclen(), (const uint8_t *)signing, (size_t)sl, mac);
    pc_base64url_encode(mac, sizeof(mac), sig);
    snprintf(out, cap, "%s.%s", signing, sig);
}

// The "alg" lookup skips spaces and tabs around the colon, but the value must be exactly "HS256":
// a name that merely *starts with* it is a different algorithm and is rejected even when the
// signature is a valid HMAC over the token.
void test_header_alg_whitespace_and_prefix()
{
    char t[400];
    mk_signed_hdr(t, sizeof(t), "{\"alg\" : \"HS256\"}", "{\"sub\":\"a\"}");
    TEST_ASSERT_TRUE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
    mk_signed_hdr(t, sizeof(t), "{\"alg\":\t\"HS256\"}", "{\"sub\":\"a\"}");
    TEST_ASSERT_TRUE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
    mk_signed_hdr(t, sizeof(t), "{\"alg\":\"HS256X\"}", "{\"sub\":\"a\"}");
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(t, strlen(t), sec(), seclen()));
}

// pc_jwt_verify_hs256() refuses a null token and one longer than PC_JWT_MAX_LEN before doing any
// parsing or HMAC work.
void test_verify_length_guards()
{
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(NULL, 20, sec(), seclen()));
    static char big[PC_JWT_MAX_LEN + 8];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    TEST_ASSERT_FALSE(pc_jwt_verify_hs256(big, PC_JWT_MAX_LEN + 1, sec(), seclen()));
}

// With no token there are no time claims to evaluate, so pc_jwt_time_valid() reports valid -
// exactly as it does with no wall clock.
void test_time_valid_null_token()
{
    TEST_ASSERT_TRUE(pc_jwt_time_valid(NULL, 0, 1700000000, 0));
}

// pc_jwt_bearer_valid_at() applies the same header guards as the untimed variant: a null header
// and a non-Bearer scheme are refused, and spaces after the scheme are skipped.
void test_bearer_valid_at_header_guards()
{
    char t[400], hdr[470];
    mk_signed(t, sizeof(t), "{\"exp\":1700000000}");
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid_at(NULL, sec(), seclen(), 1699999000, 0));
    TEST_ASSERT_FALSE(pc_jwt_bearer_valid_at("Basic abc", sec(), seclen(), 1699999000, 0));
    snprintf(hdr, sizeof(hdr), "Bearer    %s", t); // extra spaces after the scheme
    TEST_ASSERT_TRUE(pc_jwt_bearer_valid_at(hdr, sec(), seclen(), 1699999000, 0));
}

// pc_jwt_claim_int() skips spaces and tabs around the colon, and rejects a value whose first
// character is above '9' (a bare keyword) as firmly as one below '0' (a quoted string).
void test_claim_int_value_whitespace_and_non_numeric()
{
    long v = 0;
    char t[256];
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"exp\" : 1700000000}", "sig");
    TEST_ASSERT_TRUE(pc_jwt_claim_int(t, strlen(t), "exp", &v));
    TEST_ASSERT_EQUAL_INT32(1700000000, v);

    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"exp\":\t42}", "sig");
    TEST_ASSERT_TRUE(pc_jwt_claim_int(t, strlen(t), "exp", &v));
    TEST_ASSERT_EQUAL_INT32(42, v);

    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"exp\":true}", "sig");
    TEST_ASSERT_FALSE(pc_jwt_claim_int(t, strlen(t), "exp", &v));
}

// pc_jwt_claim_str() refuses a null claim name and a null output, skips spaces and tabs around
// the colon, and fails closed - leaving an empty result - when the value does not fit the output
// or when the JSON string ends on a dangling backslash.
void test_claim_str_guards_whitespace_and_truncation()
{
    char buf[32], t[256];
    TEST_ASSERT_FALSE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), NULL, buf, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_jwt_claim_str(TOKEN, strlen(TOKEN), "sub", NULL, sizeof(buf)));

    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"s\" : \"one\"}", "sig");
    TEST_ASSERT_TRUE(pc_jwt_claim_str(t, strlen(t), "s", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("one", buf);

    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"s\":\t\"two\"}", "sig");
    TEST_ASSERT_TRUE(pc_jwt_claim_str(t, strlen(t), "s", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("two", buf);

    // A value longer than the output buffer is a failure, not a silent truncation.
    char small[4];
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"s\":\"abcdefgh\"}", "sig");
    TEST_ASSERT_FALSE(pc_jwt_claim_str(t, strlen(t), "s", small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("", small);

    // A backslash as the last byte of the payload escapes nothing, so the string never closes.
    mk_token(t, sizeof(t), "{\"alg\":\"HS256\"}", "{\"s\":\"ab\\", "sig");
    TEST_ASSERT_FALSE(pc_jwt_claim_str(t, strlen(t), "s", buf, sizeof(buf)));
}

// pc_jwt_scope_allows() refuses a null required scope as firmly as a null claim or an empty
// requirement.
void test_scope_allows_null_required()
{
    TEST_ASSERT_FALSE(pc_jwt_scope_allows("read write", NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_header_alg_whitespace_and_prefix);
    RUN_TEST(test_verify_length_guards);
    RUN_TEST(test_time_valid_null_token);
    RUN_TEST(test_bearer_valid_at_header_guards);
    RUN_TEST(test_claim_int_value_whitespace_and_non_numeric);
    RUN_TEST(test_claim_str_guards_whitespace_and_truncation);
    RUN_TEST(test_scope_allows_null_required);
    RUN_TEST(test_base64url_strict_alphabet);
    RUN_TEST(test_verify_malformed_headers);
    RUN_TEST(test_bearer_extra_spaces);
    RUN_TEST(test_claim_int_edges);
    RUN_TEST(test_claim_str_edges);
    RUN_TEST(test_valid_token_accepts);
    RUN_TEST(test_wrong_secret_rejects);
    RUN_TEST(test_tampered_payload_rejects);
    RUN_TEST(test_tampered_signature_rejects);
    RUN_TEST(test_malformed_rejected);
    RUN_TEST(test_alg_not_hs256_rejected);
    RUN_TEST(test_bearer_header);
    RUN_TEST(test_claim_int);
    RUN_TEST(test_claim_missing);
    RUN_TEST(test_claim_str);
    RUN_TEST(test_scope_allows);
    RUN_TEST(test_time_no_clock_skips_claims);
    RUN_TEST(test_time_exp_enforced);
    RUN_TEST(test_time_nbf_enforced);
    RUN_TEST(test_time_no_claims_valid);
    RUN_TEST(test_bearer_valid_at);
    RUN_TEST(test_bearer_header_guards);
    return UNITY_END();
}
