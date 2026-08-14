// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OpenID Connect RS256 ID Token verifier (services/security/oidc/oidc.h).
//
// RFC 7515 Appendix A.2 prints a complete RSASSA-PKCS1-v1_5 SHA-256 JWS together with the RSA key
// that signed it, as a JWK. test_rfc7515_a2_signature is the load-bearing case: the token, the
// modulus and the exponent all come from that appendix, so a real 2048-bit RSA verification is
// performed against a signature this library did not produce. Everything else follows OpenID Connect
// Core 1.0 sec 3.1.3.7 - step 7 `alg`, step 2 `iss`, step 3 `aud`, step 9 `exp` - and RFC 7517 sec 5
// for the JWK Set scan.

#include "services/security/oidc/oidc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 7515 Appendix A.2, printed there across display lines with the breaks removed.
//   header  {"alg":"RS256"}
//   payload {"iss":"joe",CRLF " "exp":1300819380,CRLF " "http://example.com/is_root":true}
static const char RFC7515_A2[] = "eyJhbGciOiJSUzI1NiJ9"
                                 ".eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFt"
                                 "cGxlLmNvbS9pc19yb290Ijp0cnVlfQ"
                                 ".cC4hiUPoj9Eetdgtv3hF80EGrhuB__dzERat0XF9g2VtQgr9PJbu3XOiZj5RZmh7"
                                 "AAuHIm4Bh-0Qc_lF5YKt_O8W2Fp5jujGbds9uJdbF9CUAr7t1dnZcAcQjbKBYNX4"
                                 "BAynRFdiuB--f_nZLgrnbyTyWzO75vRK5h6xBArLIARNPvkSjtQBMHlb1L07Qe7K"
                                 "0GarZRmB_eSN9383LcOLn6_dO--xi12jzDwusC-eOkHWEsqtFZESc6BfI7noOPqv"
                                 "hJ1phCnvWh6IeYI2w9QOYEUipUTI8np6LbgGY9Fs98rqVt5AXLIhWkWywlVmtVrB"
                                 "p0igcN_IoypGlUPQGe77Rw";

// The public half of the same appendix's JWK, wrapped in the JWK Set of RFC 7517 sec 5.1. `n` and
// `e` are that appendix's values verbatim; `kid` is added here because the appendix carries none and
// the selection path needs one to select on (RFC 7517 sec 4.5).
static const char JWKS[] = "{\"keys\":[{\"kty\":\"RSA\",\"kid\":\"rfc7515-a2\",\"e\":\"AQAB\",\"n\":"
                           "\"ofgWCuLjybRlzo0tZWJjNiuSfb4p4fAkd_wWJcyQoTbji9k0l8W26mPddx"
                           "HmfHQp-Vaw-4qPCJrcS2mJPMEzP1Pt0Bm4d4QlL-yRT-SFd2lZS-pCgNMs"
                           "D1W_YpRPEwOWvG6b32690r2jZ47soMZo9wGzjb_7OMg0LOL-bSf63kpaSH"
                           "SXndS5z5rexMdbBYUsLA9e-KXBdQOS-UTo7WTBEMa2R2CapHg665xsmtdV"
                           "MTBQY4uDZlxvb3qCo5ZwKh9kG4LT6_I5IhlJH7aGhyxXFvUK-DWNmoudF8"
                           "NAco9_h9iaGNj8q2ethFkMLs91kzk2PAcDTW9gb54h4FRWyuXpoQ\"}]}";

// The appendix's `exp` is 1300819380, so anything before that instant is inside the window.
#define BEFORE_EXP 1300819000u

static protocore_oidc_result verify_a2(const char *token, const char *iss, const char *aud, uint32_t now)
{
    Oidc.key.jwks = JWKS;
    Oidc.key.kid = NULL;
    Oidc.jwks_find(Oidc.internal);
    TEST_ASSERT_TRUE(Oidc.ok);

    Oidc.token = token;
    Oidc.token_len = strlen(token);
    Oidc.expect.iss = iss;
    Oidc.expect.aud = aud;
    Oidc.expect.now_unix = now;
    Oidc.verify_with_key(Oidc.internal);
    return Oidc.result;
}

// The RFC's token against the RFC's key. Nothing else in this file proves the RSA verification.
void test_rfc7515_a2_signature(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, verify_a2(RFC7515_A2, "joe", NULL, BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT64(1300819380LL, Oidc.claims.exp);
}

// RFC 7517 sec 5.1: the JWK Set is a "keys" array. A find loads `n` and `e` as big-endian octets,
// right-aligned; the appendix's `e` is AQAB, the three octets 01 00 01.
void test_jwks_find_loads_the_rsa_key(void)
{
    Oidc.key.jwks = JWKS;
    Oidc.key.kid = "rfc7515-a2";
    Oidc.jwks_find(Oidc.internal);
    TEST_ASSERT_TRUE(Oidc.ok);
    TEST_ASSERT_TRUE(Oidc.key.rsa.loaded);
    static const uint8_t E[4] = {0x00, 0x01, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(E, Oidc.key.rsa.e, 4);
    // The modulus of a 2048-bit key has its top bit set, so the leading octet is not zero.
    TEST_ASSERT_TRUE(Oidc.key.rsa.n[0] >= 0x80);

    // A `kid` that is in no JWK selects nothing, and the key is left unloaded.
    Oidc.key.kid = "not-this-one";
    Oidc.jwks_find(Oidc.internal);
    TEST_ASSERT_FALSE(Oidc.ok);
    TEST_ASSERT_FALSE(Oidc.key.rsa.loaded);

    // An empty `kid` takes the first RSA JWK, which is what a token with no `kid` header falls to.
    Oidc.key.kid = "";
    Oidc.jwks_find(Oidc.internal);
    TEST_ASSERT_TRUE(Oidc.ok);

    Oidc.key.jwks = NULL;
    Oidc.jwks_find(Oidc.internal);
    TEST_ASSERT_FALSE(Oidc.ok);

    Oidc.key.jwks = "{\"keys\":[]}";
    Oidc.key.kid = NULL;
    Oidc.jwks_find(Oidc.internal);
    TEST_ASSERT_FALSE(Oidc.ok);
}

// RFC 7515 sec 4.1.4: `kid` in the JOSE Header names the key that signed. Appendix A.2's header
// carries none.
void test_token_kid(void)
{
    Oidc.token = RFC7515_A2;
    Oidc.token_len = strlen(RFC7515_A2);
    Oidc.token_kid(Oidc.internal);
    TEST_ASSERT_FALSE(Oidc.ok);
    TEST_ASSERT_EQUAL_STRING("", Oidc.text);

    // {"alg":"RS256","kid":"2011-04-29"} - the `kid` value RFC 7517 Appendix A.1 uses.
    static const char WITH_KID[] = "eyJhbGciOiJSUzI1NiIsImtpZCI6IjIwMTEtMDQtMjkifQ.eyJpc3MiOiJqb2UifQ.AAAA";
    Oidc.token = WITH_KID;
    Oidc.token_len = strlen(WITH_KID);
    Oidc.token_kid(Oidc.internal);
    TEST_ASSERT_TRUE(Oidc.ok);
    TEST_ASSERT_EQUAL_STRING("2011-04-29", Oidc.text);

    Oidc.token = NULL;
    Oidc.token_len = 0;
    Oidc.token_kid(Oidc.internal);
    TEST_ASSERT_FALSE(Oidc.ok);
}

// A full verify resolves the key from the token's own `kid` and then validates. Appendix A.2's
// header carries no `kid`, so the sole RSA JWK of the set is used.
void test_verify_resolves_the_key_itself(void)
{
    Oidc.key.jwks = JWKS;
    Oidc.token = RFC7515_A2;
    Oidc.token_len = strlen(RFC7515_A2);
    Oidc.expect.iss = "joe";
    Oidc.expect.aud = NULL;
    Oidc.expect.now_unix = BEFORE_EXP;
    Oidc.verify(Oidc.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, Oidc.result);

    // A JWK Set that does not carry the key is a key failure, not a signature failure.
    Oidc.key.jwks = "{\"keys\":[]}";
    Oidc.verify(Oidc.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_KEY, Oidc.result);
}

// Any change to the signing input or the signature breaks the RSASSA-PKCS1-v1_5 check.
void test_tampered_token_fails_the_signature(void)
{
    char bad[640];
    const size_t n = strlen(RFC7515_A2);

    // The last character of the signature segment.
    memcpy(bad, RFC7515_A2, n + 1);
    bad[n - 1] = (bad[n - 1] == 'A') ? 'B' : 'A';
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_SIGNATURE, verify_a2(bad, "joe", NULL, BEFORE_EXP));

    // A character of the payload segment: the signing input changes, so the digest does.
    memcpy(bad, RFC7515_A2, n + 1);
    bad[30] = (bad[30] == 'A') ? 'B' : 'A';
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_SIGNATURE, verify_a2(bad, "joe", NULL, BEFORE_EXP));

    // A signature verified under a different modulus is not this signature. One flipped bit in the
    // modulus is enough, and the key is restored by the next find.
    Oidc.key.jwks = JWKS;
    Oidc.key.kid = NULL;
    Oidc.jwks_find(Oidc.internal);
    TEST_ASSERT_TRUE(Oidc.ok);
    Oidc.key.rsa.n[255] = (uint8_t)(Oidc.key.rsa.n[255] ^ 0x01);
    Oidc.token = RFC7515_A2;
    Oidc.token_len = n;
    Oidc.expect.iss = "joe";
    Oidc.expect.aud = NULL;
    Oidc.expect.now_unix = BEFORE_EXP;
    Oidc.verify_with_key(Oidc.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_SIGNATURE, Oidc.result);
}

// OIDC Core sec 3.1.3.7 step 7 makes RS256 the default `alg`, and step 8 sends a MAC-based one
// elsewhere. Anything but RS256 is refused before the signature is even decoded.
void test_alg_must_be_rs256(void)
{
    static const char *const HEADERS[] = {
        "eyJhbGciOiJub25lIn0",  // {"alg":"none"}
        "eyJhbGciOiJIUzI1NiJ9", // {"alg":"HS256"}
        "eyJhbGciOiJSUzUxMiJ9", // {"alg":"RS512"}
        "eyJ0eXAiOiJKV1QifQ",   // {"typ":"JWT"}, no alg at all
    };
    const char *rest = strchr(RFC7515_A2, '.');
    for (size_t i = 0; i < sizeof(HEADERS) / sizeof(HEADERS[0]); i++)
    {
        char token[640];
        strcpy(token, HEADERS[i]);
        strcat(token, rest);
        TEST_ASSERT_EQUAL_INT_MESSAGE(PROTOCORE_OIDC_ERR_ALG, verify_a2(token, "joe", NULL, BEFORE_EXP), HEADERS[i]);
    }
}

// RFC 7515 sec 7.1: three segments, and each must be base64url. The RSA signature must decode to
// exactly the 256 octets a 2048-bit modulus takes (RFC 7518 sec 3.3).
void test_malformed_tokens(void)
{
    static const char *const BAD[] = {
        "",
        "..",
        "eyJhbGciOiJSUzI1NiJ9",                        // one segment
        "eyJhbGciOiJSUzI1NiJ9.eyJpc3MiOiJqb2UifQ",     // two segments
        "eyJhbGciOiJSUzI1NiJ9.eyJpc3MiOiJqb2UifQ.AAA", // a signature far short of 256 octets
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        const protocore_oidc_result r = verify_a2(BAD[i], "joe", NULL, BEFORE_EXP);
        TEST_ASSERT_TRUE_MESSAGE(r == PROTOCORE_OIDC_ERR_FORMAT || r == PROTOCORE_OIDC_ERR_ALG, BAD[i]);
    }

    // A token longer than the module accepts is refused on its length alone.
    Oidc.token = RFC7515_A2;
    Oidc.token_len = PROTOCORE_OIDC_MAX_LEN + 1;
    Oidc.verify_with_key(Oidc.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_FORMAT, Oidc.result);

    // No key loaded is a format refusal before anything is read.
    Oidc.key.rsa.loaded = PROTO_FALSE;
    Oidc.token = RFC7515_A2;
    Oidc.token_len = strlen(RFC7515_A2);
    Oidc.verify_with_key(Oidc.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_FORMAT, Oidc.result);
}

// OIDC Core sec 3.1.3.7 step 2: `iss` must equal the Issuer Identifier. The claim here is "joe".
void test_issuer_must_match(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, verify_a2(RFC7515_A2, "joe", NULL, BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_ISS, verify_a2(RFC7515_A2, "Joe", NULL, BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_ISS, verify_a2(RFC7515_A2, "jo", NULL, BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_ISS, verify_a2(RFC7515_A2, "joel", NULL, BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_ISS, verify_a2(RFC7515_A2, "https://example.com", NULL, BEFORE_EXP));

    // A null or empty Issuer Identifier states the caller is not checking it.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, verify_a2(RFC7515_A2, NULL, NULL, BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, verify_a2(RFC7515_A2, "", NULL, BEFORE_EXP));
}

// OIDC Core sec 3.1.3.7 step 3: `aud` must contain the client_id. Appendix A.2's token carries no
// `aud`, so demanding one is a refusal.
void test_audience_must_contain_the_client_id(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_AUD, verify_a2(RFC7515_A2, "joe", "client-1", BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, verify_a2(RFC7515_A2, "joe", "", BEFORE_EXP));
}

// OIDC Core sec 3.1.3.7 step 9 / RFC 7519 sec 4.1.4: the current time must be before `exp`. The
// appendix's token expires at 1300819380.
void test_expiry(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, verify_a2(RFC7515_A2, "joe", NULL, 1300819379u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_EXPIRED, verify_a2(RFC7515_A2, "joe", NULL, 1300819380u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_EXPIRED, verify_a2(RFC7515_A2, "joe", NULL, 1300819381u));
}

// The Claims are reported only once every check has passed, and the buffers are cleared before each
// validation so a refused token cannot leave a previous token's `sub` behind.
void test_claims_are_cleared_before_each_validation(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_OK, verify_a2(RFC7515_A2, "joe", NULL, BEFORE_EXP));
    TEST_ASSERT_EQUAL_INT64(1300819380LL, Oidc.claims.exp);
    TEST_ASSERT_EQUAL_STRING("", Oidc.claims.sub);   // the appendix's payload carries no `sub`
    TEST_ASSERT_EQUAL_STRING("", Oidc.claims.email); // nor an `email`
    TEST_ASSERT_EQUAL_INT64(0LL, Oidc.claims.iat);   // nor an `iat`

    TEST_ASSERT_EQUAL_INT(PROTOCORE_OIDC_ERR_EXPIRED, verify_a2(RFC7515_A2, "joe", NULL, 1300819381u));
    TEST_ASSERT_EQUAL_INT64(0LL, Oidc.claims.exp);
    TEST_ASSERT_EQUAL_STRING("", Oidc.claims.sub);
}
