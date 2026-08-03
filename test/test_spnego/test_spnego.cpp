// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SPNEGO GSS-API DER wrapping (network_drivers/application/smb/spnego): the InitialContextToken
// wrapping (byte-exact vs a hand-computed DER), the NegTokenResp round-trip, and extracting the
// responseToken from a realistic server NegTokenResp (skipping negState + supportedMech).

#include "network_drivers/application/smb/spnego.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {
        auto nib = [](char x) -> int { return x <= '9' ? x - '0' : (x | 0x20) - 'a' + 10; };
        out[n++] = (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
    }
    return n;
}
static void to_hex(const uint8_t *d, size_t n, char *out)
{
    static const char *H = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        out[i * 2] = H[d[i] >> 4];
        out[i * 2 + 1] = H[d[i] & 0xF];
    }
    out[n * 2] = 0;
}

// The InitialContextToken for the 4-byte token {01,02,03,04}, hand-computed DER:
//   60 24 | 06 06 (SPNEGO OID) | a0 1a | 30 18 | a0 0e | 30 0c | 06 0a (NTLM OID) | a2 06 | 04 04 <token>
void test_wrap_negotiate_bytes()
{
    const uint8_t tok[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t out[128];
    char hex[257];
    size_t n = pc_spnego_wrap_negotiate(tok, sizeof(tok), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(38, n);
    to_hex(out, n, hex);
    TEST_ASSERT_EQUAL_STRING("602406062b0601050502a01a3018a00e300c060a2b06010401823702020aa206040401020304", hex);
    // overflow fails closed
    TEST_ASSERT_EQUAL_size_t(0, pc_spnego_wrap_negotiate(tok, sizeof(tok), out, 20));
}

// wrap_authenticate produces a NegTokenResp [1]{SEQ{[2] OCTET(token)}}; parse_response extracts it.
void test_authenticate_roundtrip()
{
    uint8_t tok[200];
    for (int i = 0; i < (int)sizeof(tok); i++)
    {
        tok[i] = (uint8_t)(i * 7 + 1);
    }
    uint8_t out[256];
    size_t n = pc_spnego_wrap_authenticate(tok, sizeof(tok), out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(sizeof(tok), n);

    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_TRUE(pc_spnego_parse_response(out, n, &rt, &rl));
    TEST_ASSERT_EQUAL_size_t(sizeof(tok), rl);
    TEST_ASSERT_EQUAL_MEMORY(tok, rt, sizeof(tok));
}

// A realistic server NegTokenResp: [1]{SEQ{ negState[0], supportedMech[1], responseToken[2] }}.
// parse_response must skip the first two and return the responseToken {11,22,33,44}.
void test_parse_server_response()
{
    uint8_t blob[64];
    size_t n = unhex("a11d301ba0030a0101a10c060a2b06010401823702020aa206040411223344", blob);
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_TRUE(pc_spnego_parse_response(blob, n, &rt, &rl));
    TEST_ASSERT_EQUAL_size_t(4, rl);
    const uint8_t want[] = {0x11, 0x22, 0x33, 0x44};
    TEST_ASSERT_EQUAL_MEMORY(want, rt, 4);
}

void test_parse_rejects()
{
    uint8_t blob[64];
    size_t n = unhex("a11d301ba0030a0101a10c060a2b06010401823702020aa206040411223344", blob);
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    uint8_t bad[64];
    memcpy(bad, blob, n);
    bad[0] = 0x30; // not a NegTokenResp [1]
    TEST_ASSERT_FALSE(pc_spnego_parse_response(bad, n, &rt, &rl));
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, 10, &rt, &rl)); // truncated
    // a NegTokenResp with no responseToken (only negState) -> not found
    uint8_t nort[16];
    size_t m = unhex("a107300530030a0101", nort);
    TEST_ASSERT_FALSE(pc_spnego_parse_response(nort, m, &rt, &rl));
}

// A 300-byte token forces the two-byte definite length form (0x82) in wr_tag_len and der_len_size
// (spnego.cpp:40,42-44) on the way out, and the matching long-form der_read on the way back.
void test_wrap_len_2byte()
{
    uint8_t tok[300];
    for (int i = 0; i < (int)sizeof(tok); i++)
    {
        tok[i] = (uint8_t)(i * 5 + 2);
    }
    uint8_t out[512];
    size_t n = pc_spnego_wrap_authenticate(tok, sizeof(tok), out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(sizeof(tok), n);

    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_TRUE(pc_spnego_parse_response(out, n, &rt, &rl));
    TEST_ASSERT_EQUAL_size_t(sizeof(tok), rl);
    TEST_ASSERT_EQUAL_MEMORY(tok, rt, sizeof(tok));
}

// A >64 KB token forces the three-byte definite length form (0x83) in wr_tag_len / der_len_size
// (spnego.cpp:48-51) and the 3-byte long-form der_read on parse.
void test_wrap_len_3byte()
{
    static uint8_t tok[65540];
    static uint8_t out[65600];
    for (size_t i = 0; i < sizeof(tok); i++)
    {
        tok[i] = (uint8_t)(i * 7 + 1);
    }
    size_t n = pc_spnego_wrap_authenticate(tok, sizeof(tok), out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(sizeof(tok), n);

    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_TRUE(pc_spnego_parse_response(out, n, &rt, &rl));
    TEST_ASSERT_EQUAL_size_t(sizeof(tok), rl);
    TEST_ASSERT_EQUAL_MEMORY(tok, rt, sizeof(tok));
}

// pc_spnego_wrap_negotiate fails closed on a null token (spnego.cpp:84) and a null out buffer
// (the !out side of the overflow guard, spnego.cpp:93).
void test_wrap_negotiate_guards()
{
    const uint8_t tok[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_spnego_wrap_negotiate(nullptr, sizeof(tok), out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_spnego_wrap_negotiate(tok, sizeof(tok), nullptr, sizeof(out)));
}

// pc_spnego_wrap_authenticate fails closed on a null token (spnego.cpp:116), on overflow
// (total > cap, spnego.cpp:122), and on a null out buffer (the !out side, spnego.cpp:121).
void test_wrap_authenticate_guards()
{
    uint8_t tok[200];
    memset(tok, 0x33, sizeof(tok));
    uint8_t out[256];
    TEST_ASSERT_EQUAL_size_t(0, pc_spnego_wrap_authenticate(nullptr, sizeof(tok), out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_spnego_wrap_authenticate(tok, sizeof(tok), out, 20));
    TEST_ASSERT_EQUAL_size_t(0, pc_spnego_wrap_authenticate(tok, sizeof(tok), nullptr, sizeof(out)));
}

// pc_spnego_parse_response rejects null out-params (spnego.cpp:136-137).
void test_parse_null_args()
{
    const uint8_t blob[8] = {0xa1, 0x06, 0x30, 0x04, 0xa2, 0x02, 0x04, 0x00};
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_FALSE(pc_spnego_parse_response(nullptr, sizeof(blob), &rt, &rl));
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, sizeof(blob), nullptr, &rl));
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, sizeof(blob), &rt, nullptr));
}

// der_read rejects a TLV whose header does not even fit the buffer (spnego.cpp:60-61).
void test_parse_truncated_header()
{
    const uint8_t blob[2] = {0xa1, 0x02};
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, 1, &rt, &rl));
}

// der_read rejects bad long-form lengths: indefinite (nb == 0), oversized (nb > 4), and a length
// whose bytes run off the end (p + nb > len) - spnego.cpp:67-68.
void test_parse_bad_longform_len()
{
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    const uint8_t indef[2] = {0xa1, 0x80};
    TEST_ASSERT_FALSE(pc_spnego_parse_response(indef, sizeof(indef), &rt, &rl));
    const uint8_t big[2] = {0xa1, 0x85};
    TEST_ASSERT_FALSE(pc_spnego_parse_response(big, sizeof(big), &rt, &rl));
    const uint8_t trunc[3] = {0xa1, 0x82, 0x00};
    TEST_ASSERT_FALSE(pc_spnego_parse_response(trunc, sizeof(trunc), &rt, &rl));
}

// [1] wrapping something other than a SEQUENCE is rejected (spnego.cpp:148-149).
void test_parse_inner_not_seq()
{
    const uint8_t blob[5] = {0xa1, 0x03, 0x04, 0x01, 0xFF};
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, sizeof(blob), &rt, &rl));
}

// A malformed field inside the SEQUENCE (OCTET length runs off the end) fails the field walk
// (spnego.cpp:155-156).
void test_parse_field_malformed()
{
    const uint8_t blob[6] = {0xa1, 0x04, 0x30, 0x02, 0x04, 0x05};
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, sizeof(blob), &rt, &rl));
}

// A [2] responseToken whose inner element is not an OCTET STRING is rejected (spnego.cpp:163-164).
void test_parse_resptoken_not_octet()
{
    const uint8_t blob[9] = {0xa1, 0x07, 0x30, 0x05, 0xa2, 0x03, 0x05, 0x01, 0xFF};
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, sizeof(blob), &rt, &rl));
}

// The [1] NegTokenResp parses but its content is too short to hold the inner SEQUENCE TLV header:
// the second der_read fails (the call side of the guard at spnego.cpp:148, which
// test_parse_inner_not_seq only reaches through its tag side).
void test_parse_seq_header_truncated()
{
    const uint8_t blob[3] = {0xa1, 0x01, 0x30}; // [1] with a 1-byte content: a bare tag, no length
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, sizeof(blob), &rt, &rl));
    TEST_ASSERT_NULL(rt);
}

// A [2] responseToken whose content is too short to hold the inner OCTET STRING TLV header: the
// innermost der_read fails (the call side of the guard at spnego.cpp:163, which
// test_parse_resptoken_not_octet only reaches through its tag side).
void test_parse_resptoken_header_truncated()
{
    // [1]{ SEQ{ [2] with a 1-byte content: a bare 0x04 tag and no length byte } }
    const uint8_t blob[7] = {0xa1, 0x05, 0x30, 0x03, 0xa2, 0x01, 0x04};
    const uint8_t *rt = nullptr;
    size_t rl = 0;
    TEST_ASSERT_FALSE(pc_spnego_parse_response(blob, sizeof(blob), &rt, &rl));
    TEST_ASSERT_NULL(rt);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_wrap_negotiate_bytes);
    RUN_TEST(test_authenticate_roundtrip);
    RUN_TEST(test_parse_server_response);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_wrap_len_2byte);
    RUN_TEST(test_wrap_len_3byte);
    RUN_TEST(test_wrap_negotiate_guards);
    RUN_TEST(test_wrap_authenticate_guards);
    RUN_TEST(test_parse_null_args);
    RUN_TEST(test_parse_truncated_header);
    RUN_TEST(test_parse_bad_longform_len);
    RUN_TEST(test_parse_inner_not_seq);
    RUN_TEST(test_parse_field_malformed);
    RUN_TEST(test_parse_resptoken_not_octet);
    RUN_TEST(test_parse_seq_header_truncated);
    RUN_TEST(test_parse_resptoken_header_truncated);
    return UNITY_END();
}
